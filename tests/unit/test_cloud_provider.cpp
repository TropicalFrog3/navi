#include <gtest/gtest.h>

#include "cloud_provider.hpp"
#include "curl.hpp"
#include "sync_crypto.hpp"
#include "sync_manifest.hpp"

#include <chrono>
#include <map>
#include <string>
#include <vector>

class CloudProviderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Reset any existing override
        Curl::setHttpTransportOverride(nullptr);
    }

    void TearDown() override
    {
        Curl::setHttpTransportOverride(nullptr);
    }
};

// ── Test 1: Google Drive RFC 8628 Device Flow ────────────────────────────────

TEST_F(CloudProviderTest, GoogleDriveDeviceFlow)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId = "test_client_id.apps.googleusercontent.com";
    cfg.oauthClientSecret = "test_client_secret";
    cfg.baseRetryDelayMs = 1;

    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        if (req.url == cfg.oauthDeviceAuthEndpoint)
        {
            resp.statusCode = 200;
            std::string body = "{\"device_code\":\"dev_code_abc123\",\"user_code\":\"WXYZ-1234\","
                               "\"verification_url\":\"https://www.google.com/device\","
                               "\"expires_in\":1800,\"interval\":5}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }
        else if (req.url == cfg.oauthTokenEndpoint)
        {
            std::string bodyStr(req.body.begin(), req.body.end());
            if (bodyStr.find("device_code=pending_code") != std::string::npos)
            {
                resp.statusCode = 400;
                std::string body = "{\"error\":\"authorization_pending\",\"error_description\":\"Waiting for user\"}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
            else if (bodyStr.find("device_code=dev_code_abc123") != std::string::npos)
            {
                resp.statusCode = 200;
                std::string body = "{\"access_token\":\"ya29.test_token\",\"refresh_token\":\"1//test_refresh\","
                                   "\"token_type\":\"Bearer\",\"expires_in\":3600}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
        }
        resp.statusCode = 404;
        return true;
    });

    Sync::GoogleDriveAppDataProvider provider(cfg);

    // Start device auth flow
    Sync::DeviceCodePrompt_t prompt;
    EXPECT_EQ(provider.startDeviceAuthFlow(prompt), Sync::ECloudError::k_Ok);
    EXPECT_EQ(prompt.deviceCode, "dev_code_abc123");
    EXPECT_EQ(prompt.userCode, "WXYZ-1234");
    EXPECT_EQ(prompt.verificationUrl, "https://www.google.com/device");
    EXPECT_EQ(prompt.expiresInSec, 1800);
    EXPECT_EQ(prompt.intervalSec, 5);

    // Poll while pending
    EXPECT_EQ(provider.pollDeviceAuthToken("pending_code"), Sync::ECloudError::k_RateLimited);

    // Poll success
    EXPECT_EQ(provider.pollDeviceAuthToken("dev_code_abc123"), Sync::ECloudError::k_Ok);
    EXPECT_EQ(provider.config().accessToken, "ya29.test_token");
    EXPECT_EQ(provider.config().refreshToken, "1//test_refresh");
}

// ── Test 2: Google Drive 401 Refresh & 429 Retry-After Handling ──────────────

TEST_F(CloudProviderTest, GoogleDriveAutoRefreshAndRetry)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId = "client_id";
    cfg.oauthClientSecret = "client_secret";
    cfg.accessToken = "expired_token";
    cfg.refreshToken = "valid_refresh_token";
    cfg.baseRetryDelayMs = 1;
    cfg.maxRetries = 2;

    int quotaAttempts = 0;
    int refreshCalls = 0;

    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        if (req.url == cfg.oauthTokenEndpoint)
        {
            refreshCalls++;
            resp.statusCode = 200;
            std::string body = "{\"access_token\":\"new_shiny_token\",\"expires_in\":3600}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }
        else if (req.url.find("/about?fields=storageQuota") != std::string::npos)
        {
            quotaAttempts++;
            // Check auth header
            std::string auth;
            for (const auto& [k, v] : req.headers)
            {
                if (k == "Authorization") auth = v;
            }

            if (auth == "Bearer expired_token")
            {
                resp.statusCode = 401;
                std::string body = "{\"error\":{\"code\":401,\"message\":\"Invalid Credentials\"}}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
            else if (auth == "Bearer new_shiny_token")
            {
                if (quotaAttempts == 2)
                {
                    // Trigger a 429 once to test Retry-After
                    resp.statusCode = 429;
                    resp.headers["retry-after"] = "0";
                    std::string body = "{\"error\":{\"code\":429,\"message\":\"Too Many Requests\"}}";
                    resp.body.assign(body.begin(), body.end());
                    return true;
                }
                // Then succeed
                resp.statusCode = 200;
                std::string body = "{\"storageQuota\":{\"limit\":\"5368709120\",\"usage\":\"1073741824\"}}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
        }
        resp.statusCode = 404;
        return true;
    });

    Sync::GoogleDriveAppDataProvider provider(cfg);

    Sync::CloudQuotaInfo_t quota;
    EXPECT_EQ(provider.queryQuota(quota), Sync::ECloudError::k_Ok);
    EXPECT_EQ(refreshCalls, 1);
    EXPECT_EQ(quota.limitBytes, 5368709120ULL);
    EXPECT_EQ(quota.usedBytes, 1073741824ULL);
    EXPECT_EQ(provider.config().accessToken, "new_shiny_token");
}

// ── Test 3: Google Drive Quota Exceeded and E2EE Manifest Commit/Fetch ────────

TEST_F(CloudProviderTest, GoogleDriveManifestAndQuotaExceeded)
{
    Sync::MasterKey_t masterKey{};
    ASSERT_TRUE(Sync::generateMasterKey(masterKey));

    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.masterKey = masterKey;
    cfg.keyEpoch = 1;
    cfg.accessToken = "valid_token";
    cfg.baseRetryDelayMs = 1;

    uint64_t steamId64 = 76561198000000001ULL;
    std::string currentETag = "\"etag_v1\"";
    std::vector<uint8_t> storedBlob;
    bool quotaExceeded = false;

    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        if (quotaExceeded)
        {
            resp.statusCode = 403;
            std::string body = "{\"error\":{\"errors\":[{\"reason\":\"storageQuotaExceeded\"}],"
                               "\"code\":403,\"message\":\"The user Drive storage quota has been exceeded.\"}}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }

        // File search
        if (req.method == "GET" && req.url.find("/files?spaces=drive") != std::string::npos)
        {
            resp.statusCode = 200;
            if (req.url.find("application%2Fvnd.google-apps.folder") != std::string::npos ||
                req.url.find("application/vnd.google-apps.folder") != std::string::npos)
            {
                std::string body = "{\"files\":[{\"id\":\"folder_navi_root\",\"name\":\"Navi\"}]}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
            if (storedBlob.empty())
            {
                std::string body = "{\"files\":[]}";
                resp.body.assign(body.begin(), body.end());
            }
            else
            {
                std::string body = "{\"files\":[{\"id\":\"file_manifest_1\",\"headRevisionId\":\"" + currentETag + "\"}]}";
                resp.body.assign(body.begin(), body.end());
            }
            return true;
        }

        // File creation
        if (req.method == "POST" && req.url.find("/files") != std::string::npos)
        {
            resp.statusCode = 200;
            std::string bodyStr(req.body.begin(), req.body.end());
            if (bodyStr.find("application/vnd.google-apps.folder") != std::string::npos)
            {
                std::string body = "{\"id\":\"folder_navi_root\",\"name\":\"Navi\"}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
            std::string body = "{\"id\":\"file_manifest_1\"}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }

        // File upload (PATCH)
        if (req.method == "PATCH" && req.url.find("/files/file_manifest_1") != std::string::npos)
        {
            // Check If-Match header if present
            auto it = std::find_if(req.headers.begin(), req.headers.end(), [](const auto& h) {
                return h.first == "If-Match";
            });
            if (it != req.headers.end())
            {
                if (it->second != currentETag)
                {
                    resp.statusCode = 412; // Precondition Failed
                    return true;
                }
            }

            storedBlob = req.body;
            currentETag = "\"etag_v2\"";
            resp.statusCode = 200;
            resp.headers["etag"] = currentETag;
            std::string body = "{\"headRevisionId\":\"" + currentETag + "\"}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }

        // File download (GET media)
        if (req.method == "GET" && req.url.find("/files/file_manifest_1?alt=media") != std::string::npos)
        {
            if (storedBlob.empty())
            {
                resp.statusCode = 404;
                return true;
            }
            resp.statusCode = 200;
            resp.headers["etag"] = currentETag;
            resp.body = storedBlob;
            return true;
        }

        resp.statusCode = 404;
        return true;
    });

    Sync::GoogleDriveAppDataProvider provider(cfg);

    // Prepare a test manifest
    Sync::SyncStateManifest_t manifest;
    manifest.schemaVersion = 1;
    manifest.steamId64 = steamId64;
    manifest.keyEpoch = 1;
    manifest.deviceId = "deck_charlie";
    manifest.globalGeneration = 1;

    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_ManifestsAndLua;
    entry.appId = 440;
    entry.logicalPath = "steamapps://appmanifest_440.acf";
    entry.sha256Hex = "1111111111111111111111111111111111111111111111111111111111111111";
    entry.sizeBytes = 500;
    manifest.entries.push_back(entry);

    // Initial commit (no expected ETag)
    std::string newETag;
    EXPECT_EQ(provider.commitRemoteManifest(steamId64, manifest, "", newETag), Sync::ECloudError::k_Ok);
    EXPECT_EQ(newETag, "\"etag_v2\"");

    // Fetch remote manifest
    Sync::SyncStateManifest_t fetchedManifest;
    std::string fetchedETag;
    EXPECT_EQ(provider.fetchRemoteManifest(steamId64, fetchedManifest, fetchedETag), Sync::ECloudError::k_Ok);
    EXPECT_EQ(fetchedETag, "\"etag_v2\"");
    EXPECT_EQ(fetchedManifest.steamId64, steamId64);
    EXPECT_EQ(fetchedManifest.deviceId, "deck_charlie");
    ASSERT_EQ(fetchedManifest.entries.size(), 1);
    EXPECT_EQ(fetchedManifest.entries[0].appId, 440);

    // Stale ETag commit should fail with k_PreconditionFailed
    manifest.globalGeneration = 2;
    std::string conflictETag;
    EXPECT_EQ(provider.commitRemoteManifest(steamId64, manifest, "\"stale_etag_v0\"", conflictETag),
              Sync::ECloudError::k_PreconditionFailed);

    // Test quota exceeded mapping
    quotaExceeded = true;
    std::string quotaNewETag;
    EXPECT_EQ(provider.commitRemoteManifest(steamId64, manifest, "", quotaNewETag),
              Sync::ECloudError::k_QuotaExceeded);

    // Test upfront quota pre-check on pushEncryptedObject
    std::vector<uint8_t> dummyBlob = {1, 2, 3};
    EXPECT_EQ(provider.pushEncryptedObject(steamId64, "obj.enc", dummyBlob),
              Sync::ECloudError::k_QuotaExceeded);
}

// ── Test 4: WebDAV PROPFIND Quota & Object Push/Pull ─────────────────────────

TEST_F(CloudProviderTest, WebDavQuotaAndObjectOperations)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_WebDav;
    cfg.webDavBaseUrl = "https://webdav.example.com/sync/";
    cfg.webDavUsername = "user1";
    cfg.webDavPasswordOrToken = "pass1";
    cfg.baseRetryDelayMs = 1;

    std::map<std::string, std::vector<uint8_t>> storage;
    bool quotaRestricted = false;

    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        // Basic auth check
        std::string auth;
        for (const auto& [k, v] : req.headers)
        {
            if (k == "Authorization") auth = v;
        }
        if (auth.rfind("Basic ", 0) != 0)
        {
            resp.statusCode = 401;
            return true;
        }

        if (req.method == "PROPFIND")
        {
            resp.statusCode = 207;
            std::string availBytes = quotaRestricted ? "1024" : "104857600";
            std::string body =
                "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                "<D:multistatus xmlns:D=\"DAV:\">\n"
                "  <D:response>\n"
                "    <D:propstat>\n"
                "      <D:prop>\n"
                "        <D:quota-available-bytes>" + availBytes + "</D:quota-available-bytes>\n"
                "        <D:quota-used-bytes>10485760</D:quota-used-bytes>\n"
                "      </D:prop>\n"
                "      <D:status>HTTP/1.1 200 OK</D:status>\n"
                "    </D:propstat>\n"
                "  </D:response>\n"
                "</D:multistatus>\n";
            resp.body.assign(body.begin(), body.end());
            return true;
        }
        else if (req.method == "MKCOL")
        {
            resp.statusCode = 201;
            return true;
        }
        else if (req.method == "PUT")
        {
            storage[req.url] = req.body;
            resp.statusCode = 201;
            return true;
        }
        else if (req.method == "GET")
        {
            auto it = storage.find(req.url);
            if (it == storage.end())
            {
                resp.statusCode = 404;
                return true;
            }
            resp.statusCode = 200;
            resp.body = it->second;
            return true;
        }

        resp.statusCode = 404;
        return true;
    });

    Sync::WebDavCloudProvider provider(cfg);

    // Query quota
    Sync::CloudQuotaInfo_t quota;
    EXPECT_EQ(provider.queryQuota(quota), Sync::ECloudError::k_Ok);
    EXPECT_EQ(quota.usedBytes, 10485760ULL);
    EXPECT_EQ(quota.limitBytes, 10485760ULL + 104857600ULL);

    uint64_t steamId64 = 76561198000000002ULL;
    std::string objectKey = "blob_test_hash.enc";
    std::vector<uint8_t> testBlob = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    // Push object
    EXPECT_EQ(provider.pushEncryptedObject(steamId64, objectKey, testBlob), Sync::ECloudError::k_Ok);

    // Pull object
    std::vector<uint8_t> pulledBlob;
    EXPECT_EQ(provider.pullEncryptedObject(steamId64, objectKey, pulledBlob), Sync::ECloudError::k_Ok);
    EXPECT_EQ(pulledBlob, testBlob);

    // Pull nonexistent
    std::vector<uint8_t> missingBlob;
    EXPECT_EQ(provider.pullEncryptedObject(steamId64, "missing.enc", missingBlob), Sync::ECloudError::k_NotFound);

    // Quota restricted: only 1024 bytes available, attempting to upload 8000 bytes
    quotaRestricted = true;
    std::vector<uint8_t> largeBlob(8000, 0xFF);
    EXPECT_EQ(provider.pushEncryptedObject(steamId64, "large.enc", largeBlob), Sync::ECloudError::k_QuotaExceeded);
}

// ── Test 5: WebDAV Manifest Commit/Fetch, 412 Conflict & Cross-SteamID ────────

TEST_F(CloudProviderTest, WebDavManifestE2EERoundTripAndSecurity)
{
    Sync::MasterKey_t masterKey{};
    ASSERT_TRUE(Sync::generateMasterKey(masterKey));

    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_WebDav;
    cfg.webDavBaseUrl = "https://dav.test.local/sync";
    cfg.webDavPasswordOrToken = "bearer_secret_token";
    cfg.masterKey = masterKey;
    cfg.keyEpoch = 1;
    cfg.baseRetryDelayMs = 1;

    uint64_t steamId64 = 76561198000000003ULL;
    std::string remoteETag = "\"init_etag\"";
    std::vector<uint8_t> remoteManifestBlob;

    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        // Bearer auth check
        std::string auth;
        for (const auto& [k, v] : req.headers)
        {
            if (k == "Authorization") auth = v;
        }
        if (auth != "Bearer bearer_secret_token")
        {
            resp.statusCode = 401;
            return true;
        }

        if (req.method == "MKCOL")
        {
            resp.statusCode = 201;
            return true;
        }

        if (req.method == "PUT" && req.url.find("/manifest.enc") != std::string::npos)
        {
            auto it = std::find_if(req.headers.begin(), req.headers.end(), [](const auto& h) {
                return h.first == "If-Match";
            });
            if (it != req.headers.end())
            {
                if (it->second != remoteETag)
                {
                    resp.statusCode = 412; // Precondition Failed
                    return true;
                }
            }

            remoteManifestBlob = req.body;
            remoteETag = "\"etag_gen2\"";
            resp.statusCode = 200;
            resp.headers["etag"] = remoteETag;
            return true;
        }

        if (req.method == "GET" && req.url.find("/manifest.enc") != std::string::npos)
        {
            if (remoteManifestBlob.empty())
            {
                resp.statusCode = 404;
                return true;
            }
            resp.statusCode = 200;
            resp.headers["etag"] = remoteETag;
            resp.body = remoteManifestBlob;
            return true;
        }

        resp.statusCode = 404;
        return true;
    });

    Sync::WebDavCloudProvider provider(cfg);

    Sync::SyncStateManifest_t manifest;
    manifest.schemaVersion = 1;
    manifest.steamId64 = steamId64;
    manifest.keyEpoch = 1;
    manifest.deviceId = "pc_master";
    manifest.globalGeneration = 5;

    Sync::SyncFileEntry_t e;
    e.category = Sync::ESyncCategory::k_CloudSaves;
    e.appId = 730;
    e.logicalPath = "userdata://12345/730/remote/cs.cfg";
    e.sha256Hex = "2222222222222222222222222222222222222222222222222222222222222222";
    e.sizeBytes = 256;
    manifest.entries.push_back(e);

    // Initial commit
    std::string newETag;
    EXPECT_EQ(provider.commitRemoteManifest(steamId64, manifest, "", newETag), Sync::ECloudError::k_Ok);
    EXPECT_EQ(newETag, "\"etag_gen2\"");

    // Fetch and verify decryption
    Sync::SyncStateManifest_t fetched;
    std::string fetchedETag;
    EXPECT_EQ(provider.fetchRemoteManifest(steamId64, fetched, fetchedETag), Sync::ECloudError::k_Ok);
    EXPECT_EQ(fetchedETag, "\"etag_gen2\"");
    EXPECT_EQ(fetched.steamId64, steamId64);
    EXPECT_EQ(fetched.deviceId, "pc_master");
    ASSERT_EQ(fetched.entries.size(), 1);
    EXPECT_EQ(fetched.entries[0].appId, 730);

    // 412 Conflict on stale ETag
    manifest.globalGeneration = 6;
    std::string conflictETag;
    EXPECT_EQ(provider.commitRemoteManifest(steamId64, manifest, "\"stale_etag\"", conflictETag),
              Sync::ECloudError::k_PreconditionFailed);

    // Cross-SteamID rejection: fetching for steamId64 = 9999999999ULL against the same blob
    // Decryption / metadata check fails closed
    Sync::SyncStateManifest_t badFetched;
    std::string badETag;
    EXPECT_EQ(provider.fetchRemoteManifest(9999999999ULL, badFetched, badETag),
              Sync::ECloudError::k_CorruptPayload);
}

// ── Test 6: Cloud Provider Factory ────────────────────────────────────────────

TEST_F(CloudProviderTest, ProviderFactoryCreation)
{
    Sync::CloudProviderConfig_t gdriveCfg;
    gdriveCfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    auto gdrive = Sync::createCloudProvider(gdriveCfg);
    ASSERT_NE(gdrive, nullptr);
    EXPECT_EQ(gdrive->providerType(), Sync::ECloudProviderType::k_GoogleDrive);

    Sync::CloudProviderConfig_t webdavCfg;
    webdavCfg.type = Sync::ECloudProviderType::k_WebDav;
    auto webdav = Sync::createCloudProvider(webdavCfg);
    ASSERT_NE(webdav, nullptr);
    EXPECT_EQ(webdav->providerType(), Sync::ECloudProviderType::k_WebDav);

    Sync::CloudProviderConfig_t noneCfg;
    noneCfg.type = Sync::ECloudProviderType::k_None;
    auto none = Sync::createCloudProvider(noneCfg);
    EXPECT_EQ(none, nullptr);
}

// ── Test 7: Escaped Quotes in JSON ───────────────────────────────────────────

TEST_F(CloudProviderTest, JsonEscapedQuotesHandling)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId = "client_id";
    cfg.oauthClientSecret = "client_secret";
    cfg.baseRetryDelayMs = 1;

    // Simulate device flow where user_code or verification_url has escaped characters
    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        resp.statusCode = 200;
        std::string body = "{\"device_code\":\"dev_\\\"special\\\"_123\",\"user_code\":\"ABCD-EFGH\","
                           "\"verification_url\":\"https://www.google.com/device?hint=\\\"code\\\"\",\"expires_in\":1800,\"interval\":5}";
        resp.body.assign(body.begin(), body.end());
        return true;
    });

    Sync::GoogleDriveAppDataProvider provider(cfg);
    Sync::DeviceCodePrompt_t prompt;
    EXPECT_EQ(provider.startDeviceAuthFlow(prompt), Sync::ECloudError::k_Ok);
    EXPECT_EQ(prompt.deviceCode, "dev_\\\"special\\\"_123");
    EXPECT_EQ(prompt.verificationUrl, "https://www.google.com/device?hint=\\\"code\\\"");
}

// ── Test 8: PKCE & OAuth State Generation ────────────────────────────────────

TEST_F(CloudProviderTest, PkceAndOAuthStateGeneration)
{
    auto pkce = Sync::generatePkcePair();
    EXPECT_GE(pkce.codeVerifier.size(), 43u);
    EXPECT_LE(pkce.codeVerifier.size(), 128u);
    EXPECT_GE(pkce.codeChallenge.size(), 43u);

    // Verify unreserved characters for Base64Url
    auto isBase64Url = [](const std::string& s) {
        for (char c : s)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
                return false;
        }
        return true;
    };
    EXPECT_TRUE(isBase64Url(pkce.codeVerifier));
    EXPECT_TRUE(isBase64Url(pkce.codeChallenge));

    // Verify state generation
    std::string state = Sync::generateOAuthStateHex(32);
    EXPECT_EQ(state.size(), 64u);
    for (char c : state)
    {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
    }
}

// ── Test 9: Browser Auth URL Building ────────────────────────────────────────

TEST_F(CloudProviderTest, GoogleDriveBrowserAuthUrlBuilding)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId = "my_custom_client_id.apps.googleusercontent.com";
    cfg.oauthClientSecret = "secret123";

    Sync::GoogleDriveAppDataProvider provider(cfg);
    std::string redirectUri = "http://127.0.0.1:9001/sync/gdrive/callback";
    std::string state = "random_state_hex_abcdef";
    std::string challenge = "random_pkce_challenge_123456";

    std::string url = provider.buildBrowserAuthUrl(redirectUri, state, challenge);
    EXPECT_NE(url.find("https://accounts.google.com/o/oauth2/v2/auth?"), std::string::npos);
    EXPECT_NE(url.find("client_id=my_custom_client_id.apps.googleusercontent.com"), std::string::npos);
    EXPECT_NE(url.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A9001%2Fsync%2Fgdrive%2Fcallback"), std::string::npos);
    EXPECT_NE(url.find("response_type=code"), std::string::npos);
    EXPECT_NE(url.find("scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.file"), std::string::npos);
    EXPECT_NE(url.find("access_type=offline"), std::string::npos);
    EXPECT_NE(url.find("prompt=select_account+consent"), std::string::npos);
    EXPECT_NE(url.find("state=random_state_hex_abcdef"), std::string::npos);
    EXPECT_NE(url.find("code_challenge=random_pkce_challenge_123456"), std::string::npos);
    EXPECT_NE(url.find("code_challenge_method=S256"), std::string::npos);
}

// ── Test 10: Auth Code Exchange for Access & Refresh Tokens ─────────────────

TEST_F(CloudProviderTest, GoogleDriveExchangeAuthCodeForToken)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId = "test_client_id";
    cfg.oauthClientSecret = "test_client_secret";

    std::string capturedBody;
    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        if (req.url == cfg.oauthTokenEndpoint)
        {
            capturedBody.assign(req.body.begin(), req.body.end());
            if (capturedBody.find("code=valid_code_123") != std::string::npos)
            {
                resp.statusCode = 200;
                std::string body = "{\"access_token\":\"ya29.browser_access_token\",\"refresh_token\":\"1//browser_refresh_token\","
                                   "\"token_type\":\"Bearer\",\"expires_in\":3600}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
            else
            {
                resp.statusCode = 400;
                std::string body = "{\"error\":\"invalid_grant\",\"error_description\":\"Bad code\"}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
        }
        resp.statusCode = 404;
        return true;
    });

    Sync::GoogleDriveAppDataProvider provider(cfg);

    // Error case: invalid code
    EXPECT_EQ(provider.exchangeAuthCodeForToken("bad_code", "verifier_xyz", "http://127.0.0.1:9001/sync/gdrive/callback"),
              Sync::ECloudError::k_AuthExpired);

    // Success case
    EXPECT_EQ(provider.exchangeAuthCodeForToken("valid_code_123", "verifier_xyz", "http://127.0.0.1:9001/sync/gdrive/callback"),
              Sync::ECloudError::k_Ok);
    EXPECT_EQ(provider.config().accessToken, "ya29.browser_access_token");
    EXPECT_EQ(provider.config().refreshToken, "1//browser_refresh_token");
    EXPECT_GT(provider.config().tokenExpiresAtUnixSec, 0);

    // Verify request payload params
    EXPECT_NE(capturedBody.find("client_id=test_client_id"), std::string::npos);
    EXPECT_NE(capturedBody.find("client_secret=test_client_secret"), std::string::npos);
    EXPECT_NE(capturedBody.find("code=valid_code_123"), std::string::npos);
    EXPECT_NE(capturedBody.find("code_verifier=verifier_xyz"), std::string::npos);
    EXPECT_NE(capturedBody.find("grant_type=authorization_code"), std::string::npos);
}

// ── Test 11: Bundled Default Desktop OAuth2 Client ID Constant ──────────────

TEST_F(CloudProviderTest, GoogleDriveDefaultClientIdConstant)
{
    EXPECT_FALSE(std::string(Sync::k_DefaultGDriveClientId).empty());
    EXPECT_STREQ(Sync::k_DefaultGDriveClientId, Sync::DEFAULT_GOOGLE_DRIVE_CLIENT_ID);
    EXPECT_NE(std::string(Sync::k_DefaultGDriveClientId).find(".apps.googleusercontent.com"), std::string::npos);
    EXPECT_STREQ(Sync::k_DefaultGDriveClientId, "850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com");

    EXPECT_FALSE(std::string(Sync::k_DefaultGDriveClientSecret).empty());
    EXPECT_STREQ(Sync::k_DefaultGDriveClientSecret, Sync::DEFAULT_GOOGLE_DRIVE_CLIENT_SECRET);
    EXPECT_STREQ(Sync::k_DefaultGDriveClientSecret, "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");
}

// ── Test 12: Google Drive Default Client ID Fallback ─────────────────────────

TEST_F(CloudProviderTest, GoogleDriveDefaultClientIdFallback)
{
    unsetenv("NAVI_GDRIVE_CLIENT_ID");
    unsetenv("NAVI_GDRIVE_CLIENT_SECRET");

    EXPECT_EQ(Sync::resolveGDriveClientId(""), Sync::k_DefaultGDriveClientId);
    EXPECT_EQ(Sync::resolveGDriveClientId("   \t\n"), Sync::k_DefaultGDriveClientId);
    EXPECT_EQ(Sync::resolveGDriveClientSecret(""), Sync::k_DefaultGDriveClientSecret);
    EXPECT_EQ(Sync::resolveGDriveClientSecret("   \t\n"), Sync::k_DefaultGDriveClientSecret);
    EXPECT_EQ(Sync::resolveGDriveClientSecret("", Sync::k_DefaultGDriveClientId), Sync::k_DefaultGDriveClientSecret);

    Sync::CloudProviderConfig_t cfgDefault;
    EXPECT_EQ(cfgDefault.oauthClientId, Sync::k_DefaultGDriveClientId);
    EXPECT_EQ(cfgDefault.oauthClientSecret, Sync::k_DefaultGDriveClientSecret);

    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId.clear();
    cfg.oauthClientSecret.clear();

    Sync::GoogleDriveAppDataProvider provider(cfg);
    EXPECT_EQ(provider.config().oauthClientId, Sync::k_DefaultGDriveClientId);
    EXPECT_EQ(provider.config().oauthClientSecret, Sync::k_DefaultGDriveClientSecret);

    std::string redirectUri = "http://127.0.0.1:9001/sync/gdrive/callback";
    std::string state = "state_fallback_test";
    std::string challenge = "challenge_fallback_test";

    std::string url = provider.buildBrowserAuthUrl(redirectUri, state, challenge);
    EXPECT_NE(url.find(std::string("client_id=") + Sync::k_DefaultGDriveClientId), std::string::npos);
    EXPECT_NE(url.find("response_type=code"), std::string::npos);
    EXPECT_NE(url.find("access_type=offline"), std::string::npos);
    EXPECT_NE(url.find("prompt=select_account+consent"), std::string::npos);
}

// ── Test 13: Google Drive Explicit Custom Client ID Override ─────────────────

TEST_F(CloudProviderTest, GoogleDriveExplicitClientIdOverride)
{
    unsetenv("NAVI_GDRIVE_CLIENT_ID");

    const std::string customId = "998877665544-custom-override.apps.googleusercontent.com";
    EXPECT_EQ(Sync::resolveGDriveClientId(customId), customId);

    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.oauthClientId = customId;
    cfg.oauthClientSecret = "custom_secret_abc";

    Sync::GoogleDriveAppDataProvider provider(cfg);
    EXPECT_EQ(provider.config().oauthClientId, customId);

    std::string authUrl = provider.buildBrowserAuthUrl("http://127.0.0.1:9001/sync/gdrive/callback", "s1", "c1");
    EXPECT_NE(authUrl.find("client_id=" + customId), std::string::npos);
    EXPECT_EQ(authUrl.find(Sync::k_DefaultGDriveClientId), std::string::npos);
}

// ── Test 14: Google Drive Environment Variable Override & Precedence ─────────

TEST_F(CloudProviderTest, GoogleDriveEnvironmentVariableClientIdOverride)
{
    const std::string envId = "112233445566-env-override.apps.googleusercontent.com";
    setenv("NAVI_GDRIVE_CLIENT_ID", envId.c_str(), 1);

    EXPECT_EQ(Sync::resolveGDriveClientId(""), envId);

    Sync::CloudProviderConfig_t envCfg;
    envCfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    envCfg.oauthClientId = "";

    Sync::GoogleDriveAppDataProvider envProvider(envCfg);
    EXPECT_EQ(envProvider.config().oauthClientId, envId);

    std::string authUrl = envProvider.buildBrowserAuthUrl("http://127.0.0.1:9001/sync/gdrive/callback", "s_env", "c_env");
    EXPECT_NE(authUrl.find("client_id=" + envId), std::string::npos);

    const std::string explicitId = "777777777777-explicit-wins.apps.googleusercontent.com";
    EXPECT_EQ(Sync::resolveGDriveClientId(explicitId), explicitId);

    Sync::CloudProviderConfig_t explicitCfg;
    explicitCfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    explicitCfg.oauthClientId = explicitId;

    Sync::GoogleDriveAppDataProvider explicitProvider(explicitCfg);
    EXPECT_EQ(explicitProvider.config().oauthClientId, explicitId);

    std::string explicitAuthUrl = explicitProvider.buildBrowserAuthUrl("http://127.0.0.1:9001/sync/gdrive/callback", "s_exp", "c_exp");
    EXPECT_NE(explicitAuthUrl.find("client_id=" + explicitId), std::string::npos);
    EXPECT_EQ(explicitAuthUrl.find(envId), std::string::npos);

    unsetenv("NAVI_GDRIVE_CLIENT_ID");
    EXPECT_EQ(Sync::resolveGDriveClientId(""), Sync::k_DefaultGDriveClientId);
}

// ── Test 15: Google Drive Client Secret Resolution & Precedence ──────────────

TEST_F(CloudProviderTest, GoogleDriveClientSecretResolutionAndPrecedence)
{
    unsetenv("NAVI_GDRIVE_CLIENT_SECRET");
    unsetenv("NAVI_GDRIVE_CLIENT_ID");

    // 1. Explicit secret wins over everything
    EXPECT_EQ(Sync::resolveGDriveClientSecret("my_secret_123"), "my_secret_123");
    EXPECT_EQ(Sync::resolveGDriveClientSecret("my_secret_123", "some_id"), "my_secret_123");

    // 2. NAVI_GDRIVE_CLIENT_SECRET env var wins over default
    setenv("NAVI_GDRIVE_CLIENT_SECRET", "env_secret_456", 1);
    EXPECT_EQ(Sync::resolveGDriveClientSecret(""), "env_secret_456");
    EXPECT_EQ(Sync::resolveGDriveClientSecret("   "), "env_secret_456");
    EXPECT_EQ(Sync::resolveGDriveClientSecret("explicit_wins"), "explicit_wins");
    unsetenv("NAVI_GDRIVE_CLIENT_SECRET");

    // 3. Fallback to default secret if using default client ID (or empty)
    EXPECT_EQ(Sync::resolveGDriveClientSecret("", Sync::k_DefaultGDriveClientId), Sync::k_DefaultGDriveClientSecret);
    EXPECT_EQ(Sync::resolveGDriveClientSecret(""), Sync::k_DefaultGDriveClientSecret);

    // 4. Custom client ID without secret does NOT get default secret
    EXPECT_EQ(Sync::resolveGDriveClientSecret("", "custom_client_id.apps.googleusercontent.com"), "");
}

// ── Test 16: Google Drive Navi Root Folder Creation & Parent Scoping ─────────

TEST_F(CloudProviderTest, GoogleDriveNaviRootFolderCreationAndParentScoping)
{
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.accessToken = "valid_token";
    cfg.baseRetryDelayMs = 1;

    bool naviFolderQueried = false;
    bool naviFolderCreated = false;
    bool configFolderCreated = false;
    bool fileCreatedWithConfigParent = false;
    bool fileSearchedWithConfigParent = false;

    Curl::setHttpTransportOverride([&](const Curl::HttpRequest_t& req, Curl::HttpResponse_t& resp) -> bool {
        // 1. Folder query
        if (req.method == "GET" && req.url.find("application%2Fvnd.google-apps.folder") != std::string::npos)
        {
            if (req.url.find("name+%3D+%27Navi%27") != std::string::npos)
            {
                naviFolderQueried = true;
                resp.statusCode = 200;
                if (!naviFolderCreated)
                {
                    std::string body = "{\"files\":[]}";
                    resp.body.assign(body.begin(), body.end());
                }
                else
                {
                    std::string body = "{\"files\":[{\"id\":\"folder_navi_999\",\"name\":\"Navi\"}]}";
                    resp.body.assign(body.begin(), body.end());
                }
                return true;
            }
            if (req.url.find("name+%3D+%27config%27") != std::string::npos)
            {
                resp.statusCode = 200;
                if (!configFolderCreated)
                {
                    std::string body = "{\"files\":[]}";
                    resp.body.assign(body.begin(), body.end());
                }
                else
                {
                    std::string body = "{\"files\":[{\"id\":\"folder_cfg_888\",\"name\":\"config\"}]}";
                    resp.body.assign(body.begin(), body.end());
                }
                return true;
            }
        }

        // 2. Folder creation or File creation
        if (req.method == "POST" && req.url.find("/files") != std::string::npos)
        {
            std::string bodyStr(req.body.begin(), req.body.end());
            if (bodyStr.find("application/vnd.google-apps.folder") != std::string::npos)
            {
                if (bodyStr.find("\"name\":\"Navi\"") != std::string::npos &&
                    bodyStr.find("\"parents\":[\"root\"]") != std::string::npos)
                {
                    naviFolderCreated = true;
                    resp.statusCode = 200;
                    std::string body = "{\"id\":\"folder_navi_999\",\"name\":\"Navi\"}";
                    resp.body.assign(body.begin(), body.end());
                    return true;
                }
                if (bodyStr.find("\"name\":\"config\"") != std::string::npos &&
                    bodyStr.find("\"parents\":[\"folder_navi_999\"]") != std::string::npos)
                {
                    configFolderCreated = true;
                    resp.statusCode = 200;
                    std::string body = "{\"id\":\"folder_cfg_888\",\"name\":\"config\"}";
                    resp.body.assign(body.begin(), body.end());
                    return true;
                }
            }
            if (bodyStr.find("\"parents\":[\"folder_cfg_888\"]") != std::string::npos &&
                bodyStr.find("\"name\":\"config.yaml\"") != std::string::npos)
            {
                fileCreatedWithConfigParent = true;
                resp.statusCode = 200;
                std::string body = "{\"id\":\"file_in_cfg_123\",\"name\":\"config.yaml\"}";
                resp.body.assign(body.begin(), body.end());
                return true;
            }
        }

        // 3. File search within folder
        if (req.method == "GET" && req.url.find("folder_cfg_888") != std::string::npos &&
            req.url.find("in+parents") != std::string::npos)
        {
            fileSearchedWithConfigParent = true;
            resp.statusCode = 200;
            if (fileCreatedWithConfigParent)
            {
                std::string body = "{\"files\":[{\"id\":\"file_in_cfg_123\",\"name\":\"config.yaml\",\"version\":\"1\"}]}";
                resp.body.assign(body.begin(), body.end());
            }
            else
            {
                std::string body = "{\"files\":[]}";
                resp.body.assign(body.begin(), body.end());
            }
            return true;
        }

        // 4. File upload (PATCH media)
        if (req.method == "PATCH" && req.url.find("/files/file_in_cfg_123") != std::string::npos)
        {
            resp.statusCode = 200;
            std::string body = "{\"id\":\"file_in_cfg_123\",\"version\":\"1\"}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }

        // About / Quota
        if (req.method == "GET" && req.url.find("/about") != std::string::npos)
        {
            resp.statusCode = 200;
            std::string body = "{\"storageQuota\":{\"limit\":\"1000000000\",\"usage\":\"1000\"}}";
            resp.body.assign(body.begin(), body.end());
            return true;
        }

        resp.statusCode = 404;
        return true;
    });

    Sync::GoogleDriveAppDataProvider provider(cfg);

    std::vector<uint8_t> payload = {'c', 'o', 'n', 'f', 'i', 'g'};
    EXPECT_EQ(provider.pushEncryptedObject(0, "config/config.yaml", payload), Sync::ECloudError::k_Ok);

    EXPECT_TRUE(naviFolderQueried);
    EXPECT_TRUE(naviFolderCreated);
    EXPECT_TRUE(configFolderCreated);
    EXPECT_TRUE(fileCreatedWithConfigParent);
    EXPECT_TRUE(fileSearchedWithConfigParent);
}


