#include "cloud_oauth.hpp"
#include "cloudredirect.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

namespace Navi::CloudRedirect
{

static const char kBase64UrlChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";

static std::string Base64UrlEncodeNoPad(const unsigned char* data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len)
    {
        uint32_t triple = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kBase64UrlChars[(triple >> 18) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 12) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 6) & 0x3F]);
        out.push_back(kBase64UrlChars[triple & 0x3F]);
        i += 3;
    }

    if (i + 1 == len)
    {
        uint32_t single = data[i] << 16;
        out.push_back(kBase64UrlChars[(single >> 18) & 0x3F]);
        out.push_back(kBase64UrlChars[(single >> 12) & 0x3F]);
    }
    else if (i + 2 == len)
    {
        uint32_t pair = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(kBase64UrlChars[(pair >> 18) & 0x3F]);
        out.push_back(kBase64UrlChars[(pair >> 12) & 0x3F]);
        out.push_back(kBase64UrlChars[(pair >> 6) & 0x3F]);
    }

    return out;
}

std::string COAuthEngine::GenerateRandomState()
{
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < sizeof(buf); ++i)
    {
        ss << std::setw(2) << static_cast<unsigned int>(buf[i]);
    }
    return ss.str();
}

std::string COAuthEngine::GenerateCodeVerifier()
{
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    return Base64UrlEncodeNoPad(buf, sizeof(buf));
}

std::string COAuthEngine::ComputeCodeChallenge(const std::string& verifier)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size(), hash);
    return Base64UrlEncodeNoPad(hash, SHA256_DIGEST_LENGTH);
}

uint16_t COAuthEngine::GetProviderPort(OAuthProvider provider)
{
    switch (provider)
    {
        case OAuthProvider::GoogleDrive: return 53692;
        case OAuthProvider::OneDrive:    return 53682;
        default: return 53692;
    }
}

std::string COAuthEngine::GetProviderTokenFilename(OAuthProvider provider)
{
    switch (provider)
    {
        case OAuthProvider::GoogleDrive: return "tokens_gdrive.json";
        case OAuthProvider::OneDrive:    return "tokens_onedrive.json";
        default: return "tokens_gdrive.json";
    }
}

OAuthProvider StringToOAuthProvider(const std::string& str)
{
    if (str == "onedrive" || str == "OneDrive")
    {
        return OAuthProvider::OneDrive;
    }
    return OAuthProvider::GoogleDrive;
}

std::string OAuthProviderToString(OAuthProvider provider)
{
    switch (provider)
    {
        case OAuthProvider::OneDrive: return "onedrive";
        case OAuthProvider::GoogleDrive:
        default: return "gdrive";
    }
}

COAuthEngine& COAuthEngine::Instance()
{
    static COAuthEngine instance;
    return instance;
}

COAuthEngine::COAuthEngine() = default;

COAuthEngine::~COAuthEngine()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_listenSocket >= 0)
    {
        close(m_listenSocket);
        m_listenSocket = -1;
    }
}

void COAuthEngine::SetTokenDirForTest(const fs::path& dir)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_testTokenDir = dir;
}

const OAuthSession_t* COAuthEngine::GetActiveSessionForTest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeSession.get();
}

fs::path COAuthEngine::GetTokenFilePath(OAuthProvider provider) const
{
    if (!m_testTokenDir.empty())
    {
        return m_testTokenDir / GetProviderTokenFilename(provider);
    }
    return CCloudRedirectBridge::Instance().GetPaths().crConfigDir / GetProviderTokenFilename(provider);
}

bool COAuthEngine::WriteTokenFile(OAuthProvider provider, const std::string& tokenJson)
{
    fs::path target = GetTokenFilePath(provider);
    fs::path parent = target.parent_path();

    std::error_code ec;
    fs::create_directories(parent, ec);
    chmod(parent.c_str(), 0700);

    int fd = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        return false;
    }

    ssize_t written = write(fd, tokenJson.data(), tokenJson.size());
    close(fd);

    if (written < 0 || static_cast<size_t>(written) != tokenJson.size())
    {
        return false;
    }

    chmod(target.c_str(), 0600);
    return true;
}

bool COAuthEngine::IsAuthInFlight(OAuthProvider provider) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeSession) return false;
    if (m_activeSession->provider != provider) return false;
    if (m_activeSession->consumed) return false;

    auto now = std::chrono::steady_clock::now();
    return now < m_activeSession->expiresAt;
}

void COAuthEngine::CancelAuthFlow(OAuthProvider provider)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeSession && m_activeSession->provider == provider)
    {
        m_activeSession.reset();
    }
    if (m_listenSocket >= 0)
    {
        close(m_listenSocket);
        m_listenSocket = -1;
    }
}

bool COAuthEngine::SignOut(OAuthProvider provider)
{
    CancelAuthFlow(provider);
    fs::path p = GetTokenFilePath(provider);
    std::error_code ec;
    if (fs::exists(p, ec))
    {
        return fs::remove(p, ec);
    }
    return true;
}

std::string COAuthEngine::StartAuthFlow(OAuthProvider provider, std::string& outError)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    outError.clear();

    uint16_t port = GetProviderPort(provider);

    // Close any previous socket
    if (m_listenSocket >= 0)
    {
        close(m_listenSocket);
        m_listenSocket = -1;
    }

    // Socket pre-check and binding test
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        outError = "Failed to create socket";
        return "";
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        int errNo = errno;
        close(sock);
        outError = "Port " + std::to_string(port) + " is already in use (socket conflict): " + strerror(errNo);
        return "";
    }

    if (listen(sock, 1) < 0)
    {
        close(sock);
        outError = "Failed to listen on port " + std::to_string(port);
        return "";
    }

    m_listenSocket = sock;

    // Generate PKCE parameters
    auto session = std::make_unique<OAuthSession_t>();
    session->provider = provider;
    session->port = port;
    session->state = GenerateRandomState();
    session->codeVerifier = GenerateCodeVerifier();
    session->codeChallenge = ComputeCodeChallenge(session->codeVerifier);
    session->expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    session->consumed = false;

    std::string authUrl;
    if (provider == OAuthProvider::GoogleDrive)
    {
        authUrl = "https://accounts.google.com/o/oauth2/v2/auth?"
                  "client_id=1072944905499-vm2v2i5dvn0a0d2o4ca36i1vge8cvbn0.apps.googleusercontent.com"
                  "&redirect_uri=http%3A%2F%2F127.0.0.1%3A" + std::to_string(port) + "%2Fcallback"
                  "&response_type=code"
                  "&scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.file"
                  "&code_challenge=" + session->codeChallenge +
                  "&code_challenge_method=S256"
                  "&state=" + session->state +
                  "&access_type=offline"
                  "&prompt=consent";
    }
    else
    {
        authUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize?"
                  "client_id=d3590ed6-52b3-4102-aeff-aad2292ab01c"
                  "&redirect_uri=http%3A%2F%2F127.0.0.1%3A" + std::to_string(port) + "%2Fcallback"
                  "&response_type=code"
                  "&scope=Files.ReadWrite.AppFolder%20offline_access"
                  "&code_challenge=" + session->codeChallenge +
                  "&code_challenge_method=S256"
                  "&state=" + session->state;
    }

    m_activeSession = std::move(session);
    return authUrl;
}

} // namespace Navi::CloudRedirect
