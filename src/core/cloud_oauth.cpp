/**
 * @file  cloud_oauth.cpp
 * @brief HTTP OAuth loopback server and token exchange for CloudRedirect providers
 */
#include "cloud_oauth.hpp"
#include "cloudredirect.hpp"
#include "curl.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
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

static std::string UrlEncode(const std::string& str)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(str.size() * 3);
    for (unsigned char c : str)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

static std::string UrlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '+')
        {
            out += ' ';
        }
        else if (in[i] == '%' && i + 2 < in.size() &&
                 std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(in[i + 2])))
        {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out += static_cast<char>((hexVal(in[i + 1]) << 4) | hexVal(in[i + 2]));
            i += 2;
        }
        else
        {
            out += in[i];
        }
    }
    return out;
}

struct OAuthCallbackParams_t
{
    std::string code;
    std::string state;
    std::string error;
    std::string errorDescription;
};

static OAuthCallbackParams_t ParseCallbackQuery(const std::string& query)
{
    OAuthCallbackParams_t params;
    size_t start = 0;
    while (start < query.size())
    {
        size_t amp = query.find('&', start);
        std::string pair = query.substr(start, (amp == std::string::npos) ? std::string::npos : amp - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos)
        {
            std::string key = pair.substr(0, eq);
            std::string val = UrlDecode(pair.substr(eq + 1));
            if (key == "code") params.code = val;
            else if (key == "state") params.state = val;
            else if (key == "error") params.error = val;
            else if (key == "error_description") params.errorDescription = val;
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return params;
}

static std::string RenderSuccessPage()
{
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <title>Navi - Authorization Successful</title>\n"
           "  <style>\n"
           "    body { font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif; background: #171a21; color: #c7d5e0; text-align: center; padding-top: 60px; margin: 0; }\n"
           "    .card { background: #1b2838; border-radius: 8px; display: inline-block; padding: 40px; box-shadow: 0 4px 12px rgba(0,0,0,0.5); max-width: 480px; }\n"
           "    h1 { color: #66c0f4; margin-top: 0; }\n"
           "    p { color: #8f98a0; font-size: 16px; line-height: 1.5; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <div class=\"card\">\n"
           "    <h1>Authorization Successful</h1>\n"
           "    <p>Navi Cloud Saves has successfully connected to your cloud provider. You can safely close this browser window and return to Steam.</p>\n"
           "  </div>\n"
           "</body>\n"
           "</html>\n";
}

static std::string RenderErrorPage(const std::string& errorMsg)
{
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <title>Navi - Authorization Failed</title>\n"
           "  <style>\n"
           "    body { font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif; background: #171a21; color: #c7d5e0; text-align: center; padding-top: 60px; margin: 0; }\n"
           "    .card { background: #1b2838; border-radius: 8px; display: inline-block; padding: 40px; box-shadow: 0 4px 12px rgba(0,0,0,0.5); max-width: 480px; }\n"
           "    h1 { color: #ff6b6b; margin-top: 0; }\n"
           "    p { color: #8f98a0; font-size: 16px; line-height: 1.5; }\n"
           "    .err { color: #ff8787; font-family: monospace; font-size: 14px; background: rgba(0,0,0,0.2); padding: 8px; border-radius: 4px; word-break: break-all; margin: 16px 0; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <div class=\"card\">\n"
           "    <h1>Authorization Failed</h1>\n"
           "    <p>Navi was unable to complete cloud provider authorization:</p>\n"
           "    <div class=\"err\">" + errorMsg + "</div>\n"
           "    <p>Please return to Steam and try signing in again.</p>\n"
           "  </div>\n"
           "</body>\n"
           "</html>\n";
}

static bool ReadHttpRequest(int clientFd, int wakeFd, const std::atomic<bool>& stopRequested, std::string& outRawRequest)
{
    outRawRequest.clear();
    char buf[2048];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!stopRequested.load())
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;

        int remMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        struct pollfd pfds[2];
        pfds[0].fd = clientFd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        int nfds = 1;
        if (wakeFd >= 0)
        {
            pfds[1].fd = wakeFd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }

        int pr = poll(pfds, nfds, std::min(remMs, 1000));
        if (pr <= 0)
        {
            if (pr < 0 && errno == EINTR) continue;
            if (pr == 0) continue;
            return false;
        }

        if (nfds > 1 && (pfds[1].revents & (POLLIN | POLLERR | POLLHUP)))
        {
            return false;
        }

        if (pfds[0].revents & (POLLIN | POLLHUP))
        {
            ssize_t n = read(clientFd, buf, sizeof(buf));
            if (n <= 0)
            {
                break;
            }
            outRawRequest.append(buf, static_cast<size_t>(n));
            if (outRawRequest.find("\r\n\r\n") != std::string::npos ||
                outRawRequest.find("\n\n") != std::string::npos)
            {
                return true;
            }
            if (outRawRequest.size() > 16384)
            {
                return false;
            }
        }
        else if (pfds[0].revents & POLLERR)
        {
            return false;
        }
    }
    return !outRawRequest.empty();
}

static void SendHttpResponse(int clientFd, int statusCode, const std::string& statusText, const std::string& htmlBody)
{
    std::string resp = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n"
                       "Content-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: " + std::to_string(htmlBody.size()) + "\r\n"
                       "Connection: close\r\n"
                       "Cache-Control: no-store\r\n\r\n" +
                       htmlBody;
    size_t totalSent = 0;
    while (totalSent < resp.size())
    {
        ssize_t n = write(clientFd, resp.data() + totalSent, resp.size() - totalSent);
        if (n <= 0)
        {
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            break;
        }
        totalSent += static_cast<size_t>(n);
    }
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

std::string COAuthEngine::GetProviderTokenEndpoint(OAuthProvider provider)
{
    switch (provider)
    {
        case OAuthProvider::GoogleDrive:
            return "https://oauth2.googleapis.com/token";
        case OAuthProvider::OneDrive:
            return "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
        default:
            return "https://oauth2.googleapis.com/token";
    }
}

std::string COAuthEngine::GetProviderClientId(OAuthProvider provider)
{
    switch (provider)
    {
        case OAuthProvider::GoogleDrive:
            // The client ID is a public identifier (it appears in the auth URL).
            // NAVI_GDRIVE_CLIENT_ID overrides the default so a user can point
            // Navi at their own Google Cloud OAuth client without rebuilding.
            if (const char* envId = std::getenv("NAVI_GDRIVE_CLIENT_ID");
                envId != nullptr && envId[0] != '\0')
            {
                return envId;
            }
            return "850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com";
        case OAuthProvider::OneDrive:
            return "d3590ed6-52b3-4102-aeff-aad2292ab01c";
        default:
            return "850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com";
    }
}

std::string COAuthEngine::GetProviderClientSecret(OAuthProvider provider)
{
    switch (provider)
    {
        case OAuthProvider::GoogleDrive:
            // No hardcoded secret: a client secret must never be committed.
            // The previously hardcoded secret was exposed in git history and
            // revoked. Supply the current secret for this client at runtime
            // via NAVI_GDRIVE_CLIENT_SECRET. When it is unset, the token
            // exchange omits client_secret (see ExchangeCodeForToken).
            if (const char* envSecret = std::getenv("NAVI_GDRIVE_CLIENT_SECRET");
                envSecret != nullptr && envSecret[0] != '\0')
            {
                return envSecret;
            }
            return {};
        case OAuthProvider::OneDrive:
        default:
            // Microsoft public clients use PKCE without a client secret.
            return {};
    }
}

bool COAuthEngine::ExchangeCodeForToken(OAuthProvider provider,
                                        const std::string& code,
                                        const std::string& codeVerifier,
                                        uint16_t port,
                                        std::string& outTokenJson,
                                        std::string& outError)
{
    outTokenJson.clear();
    outError.clear();

    if (code.empty())
    {
        outError = "Authorization code is empty";
        return false;
    }
    if (codeVerifier.empty())
    {
        outError = "Code verifier is empty";
        return false;
    }

    std::string tokenUrl = GetProviderTokenEndpoint(provider);
    std::string clientId = GetProviderClientId(provider);
    std::string clientSecret = GetProviderClientSecret(provider);
    std::string redirectUri = "http://127.0.0.1:" + std::to_string(port) + "/callback";

    std::string postBody = "client_id=" + UrlEncode(clientId);
    if (!clientSecret.empty())
    {
        postBody += "&client_secret=" + UrlEncode(clientSecret);
    }
    postBody += std::string("&grant_type=authorization_code") +
                "&code=" + UrlEncode(code) +
                "&code_verifier=" + UrlEncode(codeVerifier) +
                "&redirect_uri=" + UrlEncode(redirectUri);

    Curl::HttpRequest_t req;
    req.method = "POST";
    req.url = tokenUrl;
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    req.body.assign(postBody.begin(), postBody.end());
    req.timeoutSec = 15;

    Curl::HttpResponse_t resp;
    if (!Curl::request(req, resp))
    {
        outError = "Token exchange HTTP request failed: " + resp.errorMsg;
        return false;
    }

    if (resp.statusCode != 200)
    {
        std::string bodyStr(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
        outError = "Token endpoint returned HTTP " + std::to_string(resp.statusCode) + ": " + bodyStr;
        return false;
    }

    outTokenJson.assign(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    return true;
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
    std::unique_lock<std::mutex> lock(m_mutex);
    StopListenerLocked(lock);
}

void COAuthEngine::StopListenerLocked(std::unique_lock<std::mutex>& lock)
{
    m_stopRequested = true;
    int wfd = m_wakeFd.load();
    if (wfd >= 0)
    {
        uint64_t val = 1;
        ssize_t w = write(wfd, &val, sizeof(val));
        (void)w;
    }
    int lsock = m_listenSocket.exchange(-1);
    if (lsock >= 0)
    {
        shutdown(lsock, SHUT_RDWR);
        close(lsock);
    }
    if (m_listenerThread.joinable())
    {
        lock.unlock();
        m_listenerThread.join();
        lock.lock();
    }
    int curWfd = m_wakeFd.exchange(-1);
    if (curWfd >= 0)
    {
        close(curWfd);
    }
    m_stopRequested = false;
}

void COAuthEngine::SetTokenDirForTest(const fs::path& dir)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_testTokenDir = dir;
}

void COAuthEngine::SetSessionTimeoutForTest(int seconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionTimeoutSec = (seconds > 0) ? seconds : 300;
}

const OAuthSession_t* COAuthEngine::GetActiveSessionForTest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeSession.get();
}

AuthSessionStatus COAuthEngine::GetAuthStatus(OAuthProvider provider) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeSession || m_activeSession->provider != provider)
    {
        return AuthSessionStatus::Idle;
    }
    return m_activeSession->status;
}

std::string COAuthEngine::GetLastAuthError(OAuthProvider provider) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeSession || m_activeSession->provider != provider)
    {
        return "";
    }
    return m_activeSession->lastError;
}

bool COAuthEngine::WaitForAuthCompletion(OAuthProvider provider, int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!IsAuthInFlight(provider))
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_listenerThread.joinable())
            {
                lock.unlock();
                m_listenerThread.join();
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !IsAuthInFlight(provider);
}

fs::path COAuthEngine::GetTokenFilePath(OAuthProvider provider) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
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
    if (m_activeSession->status != AuthSessionStatus::InFlight) return false;

    auto now = std::chrono::steady_clock::now();
    return now < m_activeSession->expiresAt;
}

void COAuthEngine::CancelAuthFlow(OAuthProvider provider)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_activeSession && m_activeSession->provider == provider)
    {
        m_activeSession->consumed = true;
        m_activeSession->status = AuthSessionStatus::Canceled;
        StopListenerLocked(lock);
        m_activeSession.reset();
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
    std::unique_lock<std::mutex> lock(m_mutex);
    outError.clear();

    StopListenerLocked(lock);

    uint16_t port = GetProviderPort(provider);

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

    int wfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_wakeFd = wfd;
    m_stopRequested = false;

    // Generate PKCE parameters
    auto session = std::make_unique<OAuthSession_t>();
    session->provider = provider;
    session->port = port;
    session->state = GenerateRandomState();
    session->codeVerifier = GenerateCodeVerifier();
    session->codeChallenge = ComputeCodeChallenge(session->codeVerifier);
    session->expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(m_sessionTimeoutSec);
    session->consumed = false;
    session->status = AuthSessionStatus::InFlight;

    std::string clientId = GetProviderClientId(provider);
    std::string authUrl;
    if (provider == OAuthProvider::GoogleDrive)
    {
        authUrl = "https://accounts.google.com/o/oauth2/v2/auth?"
                  "client_id=" + clientId +
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
                  "client_id=" + clientId +
                  "&redirect_uri=http%3A%2F%2F127.0.0.1%3A" + std::to_string(port) + "%2Fcallback"
                  "&response_type=code"
                  "&scope=Files.ReadWrite.AppFolder%20offline_access"
                  "&code_challenge=" + session->codeChallenge +
                  "&code_challenge_method=S256"
                  "&state=" + session->state;
    }

    m_activeSession = std::move(session);
    m_listenerThread = std::thread(&COAuthEngine::ListenerWorker, this);

    return authUrl;
}

void COAuthEngine::ListenerWorker()
{
    int listenFd = m_listenSocket.load();
    int wakeFd = m_wakeFd.load();
    if (listenFd < 0) return;

    std::chrono::steady_clock::time_point expiresAt;
    OAuthProvider provider;
    std::string expectedState;
    std::string codeVerifier;
    uint16_t port = 0;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_activeSession) return;
        expiresAt = m_activeSession->expiresAt;
        provider = m_activeSession->provider;
        expectedState = m_activeSession->state;
        codeVerifier = m_activeSession->codeVerifier;
        port = m_activeSession->port;
    }

    while (!m_stopRequested.load())
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= expiresAt)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_activeSession && m_activeSession->state == expectedState)
            {
                m_activeSession->consumed = true;
                m_activeSession->status = AuthSessionStatus::Expired;
                m_activeSession->lastError = "OAuth authorization timed out";
            }
            break;
        }

        auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(expiresAt - now).count();
        int pollTimeout = std::min<int>(500, static_cast<int>(std::max<int64_t>(1, remMs)));

        struct pollfd fds[2];
        fds[0].fd = listenFd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        int nfds = 1;
        if (wakeFd >= 0)
        {
            fds[1].fd = wakeFd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            nfds = 2;
        }

        int pr = poll(fds, nfds, pollTimeout);
        if (pr < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0)
        {
            continue;
        }

        if (nfds > 1 && (fds[1].revents & (POLLIN | POLLERR | POLLHUP)))
        {
            break;
        }

        if (m_stopRequested.load()) break;

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            break;
        }

        if (fds[0].revents & POLLIN)
        {
            struct sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept4(listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen, SOCK_CLOEXEC);
            if (clientFd < 0 && errno == ENOSYS)
            {
                clientFd = accept(listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
                if (clientFd >= 0)
                {
                    fcntl(clientFd, F_SETFD, FD_CLOEXEC);
                }
            }

            if (clientFd < 0)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                break;
            }

            struct timeval tv{};
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            std::string rawRequest;
            if (!ReadHttpRequest(clientFd, wakeFd, m_stopRequested, rawRequest))
            {
                close(clientFd);
                continue;
            }

            size_t lineEnd = rawRequest.find("\r\n");
            if (lineEnd == std::string::npos) lineEnd = rawRequest.find('\n');
            if (lineEnd == std::string::npos)
            {
                SendHttpResponse(clientFd, 400, "Bad Request", RenderErrorPage("Malformed HTTP request"));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);
                continue;
            }

            std::string reqLine = rawRequest.substr(0, lineEnd);
            size_t space1 = reqLine.find(' ');
            size_t space2 = (space1 != std::string::npos) ? reqLine.find(' ', space1 + 1) : std::string::npos;
            if (space1 == std::string::npos || space2 == std::string::npos)
            {
                SendHttpResponse(clientFd, 400, "Bad Request", RenderErrorPage("Malformed request line"));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);
                continue;
            }

            std::string method = reqLine.substr(0, space1);
            std::string uri = reqLine.substr(space1 + 1, space2 - space1 - 1);

            size_t qmark = uri.find('?');
            std::string path = (qmark != std::string::npos) ? uri.substr(0, qmark) : uri;
            std::string query = (qmark != std::string::npos) ? uri.substr(qmark + 1) : "";
            size_t hashPos = query.find('#');
            if (hashPos != std::string::npos) query = query.substr(0, hashPos);

            if (path != "/callback")
            {
                SendHttpResponse(clientFd, 404, "Not Found", RenderErrorPage("Not Found"));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);
                continue;
            }

            auto params = ParseCallbackQuery(query);

            if (params.state.empty() || params.state != expectedState)
            {
                SendHttpResponse(clientFd, 400, "Bad Request", RenderErrorPage("State verification failed. Mismatched or missing state token."));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);
                continue;
            }

            if (!params.error.empty())
            {
                std::string errDesc = params.errorDescription.empty() ? params.error : params.errorDescription;
                SendHttpResponse(clientFd, 200, "OK", RenderErrorPage("Authorization was denied or canceled: " + errDesc));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_activeSession && m_activeSession->state == expectedState)
                    {
                        m_activeSession->consumed = true;
                        m_activeSession->status = AuthSessionStatus::Failed;
                        m_activeSession->lastError = errDesc;
                    }
                }
                break;
            }

            if (params.code.empty())
            {
                SendHttpResponse(clientFd, 400, "Bad Request", RenderErrorPage("Missing authorization code."));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);
                continue;
            }

            std::string tokenJson, tokenErr;
            bool exchangeOk = ExchangeCodeForToken(provider, params.code, codeVerifier, port, tokenJson, tokenErr);

            if (!exchangeOk)
            {
                SendHttpResponse(clientFd, 500, "Internal Server Error", RenderErrorPage("Token exchange failed: " + tokenErr));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_activeSession && m_activeSession->state == expectedState)
                    {
                        m_activeSession->consumed = true;
                        m_activeSession->status = AuthSessionStatus::Failed;
                        m_activeSession->lastError = tokenErr;
                    }
                }
                break;
            }

            bool writeOk = WriteTokenFile(provider, tokenJson);
            if (!writeOk)
            {
                SendHttpResponse(clientFd, 500, "Internal Server Error", RenderErrorPage("Failed to write token file to secure storage."));
                shutdown(clientFd, SHUT_WR);
                close(clientFd);

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_activeSession && m_activeSession->state == expectedState)
                    {
                        m_activeSession->consumed = true;
                        m_activeSession->status = AuthSessionStatus::Failed;
                        m_activeSession->lastError = "Failed to write token file";
                    }
                }
                break;
            }

            SendHttpResponse(clientFd, 200, "OK", RenderSuccessPage());
            shutdown(clientFd, SHUT_WR);
            close(clientFd);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_activeSession && m_activeSession->state == expectedState)
                {
                    m_activeSession->consumed = true;
                    m_activeSession->status = AuthSessionStatus::Success;
                    m_activeSession->lastError.clear();
                }
            }
            break;
        }
    }

    int lsock = m_listenSocket.exchange(-1);
    if (lsock >= 0)
    {
        shutdown(lsock, SHUT_RDWR);
        close(lsock);
    }
}

} // namespace Navi::CloudRedirect
