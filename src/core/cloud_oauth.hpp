/**
 * @file  cloud_oauth.hpp
 * @brief Class declaration and state definitions for OAuth loopback authentication
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace Navi::CloudRedirect
{

enum class OAuthProvider : uint8_t
{
    GoogleDrive, ///< Port 53692, tokens_gdrive.json
    OneDrive     ///< Port 53682, tokens_onedrive.json
};

enum class AuthSessionStatus : uint8_t
{
    Idle,
    InFlight,
    Success,
    Failed,
    Expired,
    Canceled
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
    AuthSessionStatus status{AuthSessionStatus::InFlight};
    std::string lastError;
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
    static std::string GetProviderTokenEndpoint(OAuthProvider provider);
    static std::string GetProviderClientId(OAuthProvider provider);
    static std::string GetProviderClientSecret(OAuthProvider provider);

    static bool ExchangeCodeForToken(OAuthProvider provider,
                                     const std::string& code,
                                     const std::string& codeVerifier,
                                     uint16_t port,
                                     std::string& outTokenJson,
                                     std::string& outError);

    fs::path GetTokenFilePath(OAuthProvider provider) const;
    bool WriteTokenFile(OAuthProvider provider, const std::string& tokenJson);

    AuthSessionStatus GetAuthStatus(OAuthProvider provider) const;
    std::string GetLastAuthError(OAuthProvider provider) const;

    bool WaitForAuthCompletion(OAuthProvider provider, int timeoutMs = 3000);

    void SetTokenDirForTest(const fs::path& dir);
    void SetSessionTimeoutForTest(int seconds);
    const OAuthSession_t* GetActiveSessionForTest() const;

private:
    COAuthEngine();
    ~COAuthEngine();

    void ListenerWorker();
    void StopListenerLocked(std::unique_lock<std::mutex>& lock);

    mutable std::mutex m_mutex;
    std::unique_ptr<OAuthSession_t> m_activeSession;
    std::atomic<int> m_listenSocket{-1};
    std::atomic<int> m_wakeFd{-1};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_listenerThread;
    fs::path m_testTokenDir;
    int m_sessionTimeoutSec{300};
};

OAuthProvider StringToOAuthProvider(const std::string& str);
std::string OAuthProviderToString(OAuthProvider provider);

} // namespace Navi::CloudRedirect

