/**
 * @file  test_cloud_oauth.cpp
 * @brief Unit tests for CloudRedirect OAuth loopback server and token exchange
 */
#include <gtest/gtest.h>
#include "cloud_oauth.hpp"
#include "cloudredirect.hpp"
#include "curl.hpp"
#include "libmem/libmem.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" lm_bool_t LM_CALL LM_FindModule(lm_string_t, lm_module_t*) __attribute__((weak));
extern "C" lm_bool_t LM_CALL LM_FindModule(lm_string_t, lm_module_t*) { return LM_FALSE; }

namespace fs = std::filesystem;
using namespace Navi::CloudRedirect;

struct ScopedOAuthTestEnv
{
    fs::path tempDir;

    ScopedOAuthTestEnv()
    {
        tempDir = fs::path(std::string("/tmp/test_cloud_oauth_") + std::to_string(getpid()) + "_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(tempDir, ec);
    }

    ~ScopedOAuthTestEnv()
    {
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }
};

TEST(CloudOAuthTest, PkceVerifierChallengeAndStateGeneration)
{
    std::string state1 = COAuthEngine::GenerateRandomState();
    std::string state2 = COAuthEngine::GenerateRandomState();

    // 32 bytes hex = 64 hex characters
    EXPECT_EQ(state1.size(), 64u);
    EXPECT_EQ(state2.size(), 64u);
    EXPECT_NE(state1, state2);

    for (char c : state1)
    {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    // Code verifier: 32 bytes base64url unpadded = 43 characters
    std::string verifier1 = COAuthEngine::GenerateCodeVerifier();
    std::string verifier2 = COAuthEngine::GenerateCodeVerifier();

    EXPECT_EQ(verifier1.size(), 43u);
    EXPECT_EQ(verifier2.size(), 43u);
    EXPECT_NE(verifier1, verifier2);

    auto isBase64Url = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    };

    for (char c : verifier1)
    {
        EXPECT_TRUE(isBase64Url(c));
    }

    // Code challenge: SHA256 of verifier, base64url unpadded = 43 characters
    std::string challenge1 = COAuthEngine::ComputeCodeChallenge(verifier1);
    std::string challenge1_repeat = COAuthEngine::ComputeCodeChallenge(verifier1);
    std::string challenge2 = COAuthEngine::ComputeCodeChallenge(verifier2);

    EXPECT_EQ(challenge1.size(), 43u);
    EXPECT_EQ(challenge1, challenge1_repeat); // Deterministic
    EXPECT_NE(challenge1, challenge2);

    for (char c : challenge1)
    {
        EXPECT_TRUE(isBase64Url(c));
    }
}

TEST(CloudOAuthTest, TokenFilePathAndPermissions)
{
    ScopedOAuthTestEnv env;
    auto& engine = COAuthEngine::Instance();
    engine.SetTokenDirForTest(env.tempDir);

    fs::path gdrivePath = engine.GetTokenFilePath(OAuthProvider::GoogleDrive);
    fs::path onedrivePath = engine.GetTokenFilePath(OAuthProvider::OneDrive);

    EXPECT_EQ(gdrivePath.filename(), "tokens_gdrive.json");
    EXPECT_EQ(onedrivePath.filename(), "tokens_onedrive.json");

    std::string dummyTokenJson = "{\"access_token\":\"xyz123\",\"refresh_token\":\"ref456\"}";

    EXPECT_TRUE(engine.WriteTokenFile(OAuthProvider::GoogleDrive, dummyTokenJson));
    EXPECT_TRUE(fs::exists(gdrivePath));

    // Verify file permission is mode 0600 (-rw-------)
    struct stat st{};
    ASSERT_EQ(stat(gdrivePath.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    // Verify parent directory permission is mode 0700 (drwx------)
    struct stat dirSt{};
    ASSERT_EQ(stat(env.tempDir.c_str(), &dirSt), 0);
    EXPECT_EQ(dirSt.st_mode & 0777, 0700);

    // Verify content written
    std::ifstream ifs(gdrivePath);
    std::string readContent;
    std::getline(ifs, readContent);
    EXPECT_EQ(readContent, dummyTokenJson);

    // Verify SignOut deletes the token file
    EXPECT_TRUE(engine.SignOut(OAuthProvider::GoogleDrive));
    EXPECT_FALSE(fs::exists(gdrivePath));

    engine.SetTokenDirForTest({});
}

TEST(CloudOAuthTest, PortConflictCheckAndAuthFlow)
{
    auto& engine = COAuthEngine::Instance();

    // Occupy GoogleDrive loopback port 53692
    int conflictSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(conflictSock, 0);

    int opt = 1;
    setsockopt(conflictSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53692);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(conflictSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 &&
        listen(conflictSock, 1) == 0)
    {
        std::string err;
        std::string url = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err);

        // Binding should fail due to port conflict
        EXPECT_TRUE(url.empty());
        EXPECT_FALSE(err.empty());
        EXPECT_NE(err.find("53692"), std::string::npos);

        struct linger sl = {1, 0};
        setsockopt(conflictSock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
        shutdown(conflictSock, SHUT_RDWR);
        close(conflictSock);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    else
    {
        close(conflictSock);
    }

    // Now with port free, StartAuthFlow should succeed
    std::string err2;
    std::string validUrl;
    for (int retry = 0; retry < 10; ++retry)
    {
        validUrl = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err2);
        if (!validUrl.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_FALSE(validUrl.empty());
    EXPECT_TRUE(err2.empty());
    EXPECT_NE(validUrl.find("accounts.google.com"), std::string::npos);
    EXPECT_NE(validUrl.find("code_challenge="), std::string::npos);
    EXPECT_NE(validUrl.find("code_challenge_method=S256"), std::string::npos);

    EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));

    // Cancel in-flight auth
    engine.CancelAuthFlow(OAuthProvider::GoogleDrive);
    EXPECT_FALSE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));
}

TEST(CloudOAuthTest, ExchangeCodeForTokenMocked)
{
    // Default path: no env override, so the user's own client ID is used and
    // no client secret is sent (none is hardcoded anymore).
    unsetenv("NAVI_GDRIVE_CLIENT_ID");
    unsetenv("NAVI_GDRIVE_CLIENT_SECRET");

    // 1. Google Drive token exchange mock
    Curl::HttpRequest_t capturedReq;
    Curl::setHttpTransportOverride([&capturedReq](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) {
        capturedReq = req;
        resp.statusCode = 200;
        std::string body = "{\"access_token\":\"ya29.gdrive_mock_tok\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"refresh_token\":\"1//mock_refresh\"}";
        resp.body.assign(body.begin(), body.end());
        return true;
    });

    std::string tokenJson, err;
    bool ok = COAuthEngine::ExchangeCodeForToken(OAuthProvider::GoogleDrive,
                                                "mock_code_123",
                                                "mock_verifier_abc",
                                                53692,
                                                tokenJson,
                                                err);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(err.empty());
    EXPECT_NE(tokenJson.find("ya29.gdrive_mock_tok"), std::string::npos);
    EXPECT_EQ(capturedReq.method, "POST");
    EXPECT_EQ(capturedReq.url, "https://oauth2.googleapis.com/token");
    std::string bodyStr(capturedReq.body.begin(), capturedReq.body.end());
    EXPECT_NE(bodyStr.find("client_id=850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com"), std::string::npos);
    EXPECT_EQ(bodyStr.find("client_secret="), std::string::npos);
    EXPECT_NE(bodyStr.find("grant_type=authorization_code"), std::string::npos);
    EXPECT_NE(bodyStr.find("code=mock_code_123"), std::string::npos);
    EXPECT_NE(bodyStr.find("code_verifier=mock_verifier_abc"), std::string::npos);
    EXPECT_NE(bodyStr.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A53692%2Fcallback"), std::string::npos);

    // 2. OneDrive token exchange mock
    ok = COAuthEngine::ExchangeCodeForToken(OAuthProvider::OneDrive,
                                            "mock_code_ms",
                                            "mock_verifier_ms",
                                            53682,
                                            tokenJson,
                                            err);
    EXPECT_TRUE(ok);
    EXPECT_EQ(capturedReq.url, "https://login.microsoftonline.com/consumers/oauth2/v2.0/token");
    bodyStr.assign(capturedReq.body.begin(), capturedReq.body.end());
    EXPECT_NE(bodyStr.find("client_id=d3590ed6-52b3-4102-aeff-aad2292ab01c"), std::string::npos);
    EXPECT_EQ(bodyStr.find("client_secret="), std::string::npos);
    EXPECT_NE(bodyStr.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A53682%2Fcallback"), std::string::npos);

    // 3. Error handling: HTTP error from token endpoint
    Curl::setHttpTransportOverride([](const Curl::HttpRequest_t&, Curl::HttpResponse_t& resp) {
        resp.statusCode = 400;
        std::string errBody = "{\"error\":\"invalid_grant\",\"error_description\":\"Malformed code\"}";
        resp.body.assign(errBody.begin(), errBody.end());
        return true;
    });

    ok = COAuthEngine::ExchangeCodeForToken(OAuthProvider::GoogleDrive, "bad_code", "v", 53692, tokenJson, err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("400"), std::string::npos);
    EXPECT_NE(err.find("invalid_grant"), std::string::npos);

    // 4. Error handling: Transport error
    Curl::setHttpTransportOverride([](const Curl::HttpRequest_t&, Curl::HttpResponse_t& resp) {
        resp.errorMsg = "Connection refused";
        return false;
    });

    ok = COAuthEngine::ExchangeCodeForToken(OAuthProvider::GoogleDrive, "code", "v", 53692, tokenJson, err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("Connection refused"), std::string::npos);

    // 5. Empty inputs
    ok = COAuthEngine::ExchangeCodeForToken(OAuthProvider::GoogleDrive, "", "v", 53692, tokenJson, err);
    EXPECT_FALSE(ok);
    ok = COAuthEngine::ExchangeCodeForToken(OAuthProvider::GoogleDrive, "c", "", 53692, tokenJson, err);
    EXPECT_FALSE(ok);

    Curl::setHttpTransportOverride(nullptr);
}

TEST(CloudOAuthTest, GoogleDriveClientCredentialsFromEnv)
{
    // Regression: the Google client ID/secret must come from the environment
    // when set, and no secret may be hardcoded when they are not.
    unsetenv("NAVI_GDRIVE_CLIENT_ID");
    unsetenv("NAVI_GDRIVE_CLIENT_SECRET");

    // Defaults: the user's own client ID, no hardcoded secret.
    EXPECT_EQ(COAuthEngine::GetProviderClientId(OAuthProvider::GoogleDrive),
              "850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com");
    EXPECT_TRUE(COAuthEngine::GetProviderClientSecret(OAuthProvider::GoogleDrive).empty());

    // Env override wins over the defaults.
    setenv("NAVI_GDRIVE_CLIENT_ID", "env-test-client.apps.googleusercontent.com", 1);
    setenv("NAVI_GDRIVE_CLIENT_SECRET", "env-test-secret", 1);
    EXPECT_EQ(COAuthEngine::GetProviderClientId(OAuthProvider::GoogleDrive),
              "env-test-client.apps.googleusercontent.com");
    EXPECT_EQ(COAuthEngine::GetProviderClientSecret(OAuthProvider::GoogleDrive),
              "env-test-secret");

    // Restore the defaults so later tests are not affected.
    unsetenv("NAVI_GDRIVE_CLIENT_ID");
    unsetenv("NAVI_GDRIVE_CLIENT_SECRET");
    EXPECT_EQ(COAuthEngine::GetProviderClientId(OAuthProvider::GoogleDrive),
              "850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com");
    EXPECT_TRUE(COAuthEngine::GetProviderClientSecret(OAuthProvider::GoogleDrive).empty());
}

TEST(CloudOAuthTest, LoopbackCallbackListenerSuccessAndTokenStorage)
{
    ScopedOAuthTestEnv env;
    auto& engine = COAuthEngine::Instance();
    engine.SetTokenDirForTest(env.tempDir);

    const std::string mockTokenPayload =
        "{\"access_token\":\"ya29.loopback_success_test\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"refresh_token\":\"1//loopback_refresh\"}";

    Curl::setHttpTransportOverride([&mockTokenPayload](const Curl::HttpRequest_t&, Curl::HttpResponse_t& resp) {
        resp.statusCode = 200;
        resp.body.assign(mockTokenPayload.begin(), mockTokenPayload.end());
        return true;
    });

    std::string err;
    std::string authUrl = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err);
    ASSERT_FALSE(authUrl.empty());
    EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));

    const auto* session = engine.GetActiveSessionForTest();
    ASSERT_NE(session, nullptr);
    std::string state = session->state;
    uint16_t port = session->port;

    // Connect via loopback TCP socket
    int clientSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(clientSock, 0);

    struct sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

    ASSERT_EQ(connect(clientSock, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr)), 0);

    // Send HTTP GET /callback?code=mock_code_xyz&state=<state>
    std::string req = "GET /callback?code=mock_code_xyz&state=" + state + " HTTP/1.1\r\n"
                      "Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
                      "Connection: close\r\n\r\n";
    ssize_t w = write(clientSock, req.data(), req.size());
    ASSERT_EQ(static_cast<size_t>(w), req.size());

    // Read HTTP response
    std::string resp;
    char buf[1024];
    ssize_t n;
    while ((n = read(clientSock, buf, sizeof(buf))) > 0)
    {
        resp.append(buf, n);
    }
    close(clientSock);

    EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp.find("Content-Type: text/html"), std::string::npos);
    EXPECT_NE(resp.find("Authorization Successful"), std::string::npos);

    // Wait for worker thread completion
    EXPECT_TRUE(engine.WaitForAuthCompletion(OAuthProvider::GoogleDrive, 3000));
    EXPECT_FALSE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));
    EXPECT_EQ(engine.GetAuthStatus(OAuthProvider::GoogleDrive), AuthSessionStatus::Success);

    // Verify token file was stored with 0600 permissions
    fs::path tokenFile = env.tempDir / "tokens_gdrive.json";
    ASSERT_TRUE(fs::exists(tokenFile));

    struct stat st{};
    ASSERT_EQ(stat(tokenFile.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    std::ifstream ifs(tokenFile);
    std::string savedContent;
    std::getline(ifs, savedContent);
    EXPECT_EQ(savedContent, mockTokenPayload);

    engine.SetTokenDirForTest({});
    Curl::setHttpTransportOverride(nullptr);
}

TEST(CloudOAuthTest, LoopbackCallbackStateMismatchAndIgnoredPaths)
{
    auto& engine = COAuthEngine::Instance();

    std::string err;
    std::string authUrl = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err);
    ASSERT_FALSE(authUrl.empty());
    EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));

    const auto* session = engine.GetActiveSessionForTest();
    ASSERT_NE(session, nullptr);
    std::string state = session->state;
    uint16_t port = session->port;

    // 1. Non-callback path (/favicon.ico) returns 404 and does not abort listener
    {
        int clientSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        ASSERT_GE(clientSock, 0);
        struct sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        ASSERT_EQ(connect(clientSock, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr)), 0);

        std::string req = "GET /favicon.ico HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        (void)write(clientSock, req.data(), req.size());

        std::string resp;
        char buf[1024];
        ssize_t n;
        while ((n = read(clientSock, buf, sizeof(buf))) > 0) resp.append(buf, n);
        close(clientSock);

        EXPECT_NE(resp.find("404 Not Found"), std::string::npos);
        EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));
    }

    // 2. State mismatch returns 400 Bad Request and does not consume valid session
    {
        int clientSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        ASSERT_GE(clientSock, 0);
        struct sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        ASSERT_EQ(connect(clientSock, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr)), 0);

        std::string req = "GET /callback?code=fake_code&state=invalid_mismatched_state HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        (void)write(clientSock, req.data(), req.size());

        std::string resp;
        char buf[1024];
        ssize_t n;
        while ((n = read(clientSock, buf, sizeof(buf))) > 0) resp.append(buf, n);
        close(clientSock);

        EXPECT_NE(resp.find("400 Bad Request"), std::string::npos);
        EXPECT_NE(resp.find("State verification failed"), std::string::npos);
        EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));
    }

    // 3. User denial / cancel error query returns HTML and consumes session with Failed status
    {
        int clientSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        ASSERT_GE(clientSock, 0);
        struct sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        ASSERT_EQ(connect(clientSock, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr)), 0);

        std::string req = "GET /callback?error=access_denied&error_description=User%20declined%20permission&state=" + state + " HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\nConnection: close\r\n\r\n";
        (void)write(clientSock, req.data(), req.size());

        std::string resp;
        char buf[1024];
        ssize_t n;
        while ((n = read(clientSock, buf, sizeof(buf))) > 0) resp.append(buf, n);
        close(clientSock);

        EXPECT_NE(resp.find("200 OK"), std::string::npos);
        EXPECT_NE(resp.find("User declined permission"), std::string::npos);

        EXPECT_TRUE(engine.WaitForAuthCompletion(OAuthProvider::GoogleDrive, 3000));
        EXPECT_FALSE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));
        EXPECT_EQ(engine.GetAuthStatus(OAuthProvider::GoogleDrive), AuthSessionStatus::Failed);
        EXPECT_NE(engine.GetLastAuthError(OAuthProvider::GoogleDrive).find("User declined permission"), std::string::npos);
    }
}

TEST(CloudOAuthTest, CleanThreadShutdownAndPortRelease)
{
    auto& engine = COAuthEngine::Instance();

    std::string err;
    std::string authUrl = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err);
    ASSERT_FALSE(authUrl.empty());
    EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));

    // Cancel in-flight auth
    engine.CancelAuthFlow(OAuthProvider::GoogleDrive);
    EXPECT_FALSE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));

    // Verify port 53692 is released immediately and can be bound again
    int testSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(testSock, 0);
    int opt = 1;
    setsockopt(testSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53692);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    EXPECT_EQ(bind(testSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);
    close(testSock);
}

TEST(CloudOAuthTest, TimeoutSafetyAndSessionExpiration)
{
    auto& engine = COAuthEngine::Instance();
    engine.SetSessionTimeoutForTest(1);

    std::string err;
    std::string authUrl = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err);
    ASSERT_FALSE(authUrl.empty());
    EXPECT_TRUE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));

    // Wait for timeout expiration (timeout was set to 1 sec)
    EXPECT_TRUE(engine.WaitForAuthCompletion(OAuthProvider::GoogleDrive, 2500));
    EXPECT_FALSE(engine.IsAuthInFlight(OAuthProvider::GoogleDrive));
    EXPECT_EQ(engine.GetAuthStatus(OAuthProvider::GoogleDrive), AuthSessionStatus::Expired);

    // Verify port 53692 was released
    int testSock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(testSock, 0);
    int opt = 1;
    setsockopt(testSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53692);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    EXPECT_EQ(bind(testSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);
    close(testSock);

    engine.SetSessionTimeoutForTest(300);
}

TEST(CloudRedirectBridgeTest, LoadEngineDisabledAndKillswitch)
{
    const char* origHome = getenv("HOME");
    const char* origXdgConfig = getenv("XDG_CONFIG_HOME");
    const char* origXdgData = getenv("XDG_DATA_HOME");

    fs::path tempDir = fs::path(std::string("/tmp/test_cr_load_") + std::to_string(getpid()) + "_" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    setenv("HOME", tempDir.c_str(), 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");

    auto& bridge = CCloudRedirectBridge::Instance();

    // 1. Disabled when cloud_enabled flag is missing
    EXPECT_FALSE(bridge.IsEnabled());
    EXPECT_FALSE(bridge.LoadEngine());

    // 2. Disabled when killswitch file exists even if cloud_enabled exists
    const auto& paths = bridge.GetPaths();
    fs::create_directories(paths.naviConfigDir, ec);
    fs::create_directories(paths.crConfigDir, ec);

    std::ofstream flag(paths.naviEnabledFlag);
    flag << "1";
    flag.close();

    std::ofstream killswitch(paths.crKillswitchFile);
    killswitch << "1";
    killswitch.close();

    EXPECT_FALSE(bridge.IsEnabled());
    EXPECT_FALSE(bridge.LoadEngine());

    // 3. Enabled but engineSo missing
    fs::remove(paths.crKillswitchFile, ec);
    EXPECT_TRUE(bridge.IsEnabled());
    EXPECT_FALSE(bridge.LoadEngine());

    // Clean up
    fs::remove_all(tempDir, ec);
    if (origHome) setenv("HOME", origHome, 1); else unsetenv("HOME");
    if (origXdgConfig) setenv("XDG_CONFIG_HOME", origXdgConfig, 1); else unsetenv("XDG_CONFIG_HOME");
    if (origXdgData) setenv("XDG_DATA_HOME", origXdgData, 1); else unsetenv("XDG_DATA_HOME");
}
