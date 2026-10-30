#include <gtest/gtest.h>
#include "cloud_oauth.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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
    std::string validUrl = engine.StartAuthFlow(OAuthProvider::GoogleDrive, err2);
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
