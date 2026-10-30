#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace Navi::CloudRedirect
{

enum class OAuthProvider : uint8_t
{
    GoogleDrive, ///< Port 53692, tokens_gdrive.json
    OneDrive     ///< Port 53682, tokens_onedrive.json
};

struct OAuthSession_t
{
    std::string state;           ///< 32 bytes CSPRNG hex string (64 chars)
    std::string codeVerifier;    ///< 32 bytes CSPRNG base64url string (43 chars)
    std::string codeChallenge;   ///< SHA256(verifier) base64url
    OAuthProvider provider{OAuthProvider::GoogleDrive};
    uint16_t port{0};
    std::chrono::steady_clock::time_point expiresAt;
    bool consumed{false};
};

class COAuthEngine
{
public:
    static COAuthEngine& Instance();

    COAuthEngine(const COAuthEngine&) = delete;
    COAuthEngine& operator=(const COAuthEngine&) = delete;

    std::string StartAuthFlow(OAuthProvider provider, std::string& outError);
    bool IsAuthInFlight(OAuthProvider provider) const;
    void CancelAuthFlow(OAuthProvider provider);
    bool SignOut(OAuthProvider provider);

    static std::string GenerateRandomState();
    static std::string GenerateCodeVerifier();
    static std::string ComputeCodeChallenge(const std::string& verifier);
    static uint16_t GetProviderPort(OAuthProvider provider);
    static std::string GetProviderTokenFilename(OAuthProvider provider);

    fs::path GetTokenFilePath(OAuthProvider provider) const;
    bool WriteTokenFile(OAuthProvider provider, const std::string& tokenJson);

    void SetTokenDirForTest(const fs::path& dir);
    const OAuthSession_t* GetActiveSessionForTest() const;

private:
    COAuthEngine();
    ~COAuthEngine();

    mutable std::mutex m_mutex;
    std::unique_ptr<OAuthSession_t> m_activeSession;
    int m_listenSocket{-1};
    fs::path m_testTokenDir;
};

OAuthProvider StringToOAuthProvider(const std::string& str);
std::string OAuthProviderToString(OAuthProvider provider);

} // namespace Navi::CloudRedirect

