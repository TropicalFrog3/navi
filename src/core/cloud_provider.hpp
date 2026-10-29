#pragma once

#include "curl.hpp"
#include "sync_crypto.hpp"
#include "sync_manifest.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Sync
{
    // Bundled default Google OAuth2 Desktop (Installed Application) Client ID and Secret for zero-config 1-click sign-in
    inline constexpr const char* k_DefaultGDriveClientId = "850090375719-94dc73t67m8fti52at746r65rask4j4n.apps.googleusercontent.com";
    inline constexpr const char* k_DefaultGDriveClientSecret = "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";
    inline constexpr const char* DEFAULT_GOOGLE_DRIVE_CLIENT_ID = k_DefaultGDriveClientId;
    inline constexpr const char* DEFAULT_GOOGLE_DRIVE_CLIENT_SECRET = k_DefaultGDriveClientSecret;

    enum class ECloudError : uint8_t
    {
        k_Ok = 0,
        k_NetworkError,
        k_AuthExpired,
        k_QuotaExceeded,
        k_RateLimited,
        k_PreconditionFailed,
        k_NotFound,
        k_CorruptPayload,
        k_InvalidConfig
    };

    struct CloudQuotaInfo_t
    {
        uint64_t usedBytes = 0;
        uint64_t limitBytes = 0;

        bool hasSpaceFor(uint64_t additionalBytes) const
        {
            return limitBytes == 0 || (usedBytes + additionalBytes + (4ULL << 20)) <= limitBytes;
        }
    };

    struct CloudProviderConfig_t
    {
        ECloudProviderType type = ECloudProviderType::k_None;
        MasterKey_t masterKey{};
        uint32_t keyEpoch = 1;

        /* Google Drive OAuth2 */
        std::string oauthClientId = k_DefaultGDriveClientId;
        std::string oauthClientSecret = k_DefaultGDriveClientSecret;
        std::string oauthAuthEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
        std::string oauthTokenEndpoint = "https://oauth2.googleapis.com/token";
        std::string oauthDeviceAuthEndpoint = "https://oauth2.googleapis.com/device/code";
        std::string gdriveApiBaseUrl = "https://www.googleapis.com/drive/v3";
        std::string gdriveUploadBaseUrl = "https://www.googleapis.com/upload/drive/v3";
        std::string accessToken;
        std::string refreshToken;
        int64_t tokenExpiresAtUnixSec = 0;

        /* WebDAV / S3 */
        std::string webDavBaseUrl;
        std::string webDavUsername;
        std::string webDavPasswordOrToken;

        /* Backoff config */
        int maxRetries = 3;
        int baseRetryDelayMs = 50;
    };

    struct DeviceCodePrompt_t
    {
        std::string deviceCode;
        std::string userCode;
        std::string verificationUrl;
        int expiresInSec = 1800;
        int intervalSec = 5;
    };

    struct PkcePair_t
    {
        std::string codeVerifier;
        std::string codeChallenge;
    };

    PkcePair_t generatePkcePair();
    std::string generateOAuthStateHex(size_t byteCount = 32);
    std::string resolveGDriveClientId(const std::string& customClientId = "");
    std::string resolveGDriveClientSecret(const std::string& customClientSecret = "", const std::string& clientId = "");

    class ICloudStorageProvider
    {
    public:
        virtual ~ICloudStorageProvider() = default;

        virtual ECloudProviderType providerType() const = 0;
        virtual ECloudError refreshAuthTokenIfNeeded() = 0;
        virtual ECloudError queryQuota(CloudQuotaInfo_t& outQuota) = 0;
        virtual ECloudError fetchRemoteManifest(uint64_t steamId64, SyncStateManifest_t& outManifest, std::string& outETag) = 0;
        virtual ECloudError pushEncryptedObject(uint64_t steamId64, const std::string& objectKey, const std::vector<uint8_t>& blob) = 0;
        virtual ECloudError pullEncryptedObject(uint64_t steamId64, const std::string& objectKey, std::vector<uint8_t>& outBlob) = 0;
        virtual ECloudError deleteRemoteObject(uint64_t /*steamId64*/, const std::string& /*objectKey*/) { return ECloudError::k_Ok; }
        virtual ECloudError commitRemoteManifest(uint64_t steamId64, const SyncStateManifest_t& manifest, const std::string& expectedETag, std::string& outNewETag) = 0;
    };

    class GoogleDriveAppDataProvider : public ICloudStorageProvider
    {
    public:
        explicit GoogleDriveAppDataProvider(CloudProviderConfig_t cfg);
        ~GoogleDriveAppDataProvider() override = default;

        ECloudProviderType providerType() const override { return ECloudProviderType::k_GoogleDrive; }
        ECloudError refreshAuthTokenIfNeeded() override;
        ECloudError queryQuota(CloudQuotaInfo_t& outQuota) override;
        ECloudError fetchRemoteManifest(uint64_t steamId64, SyncStateManifest_t& outManifest, std::string& outETag) override;
        ECloudError pushEncryptedObject(uint64_t steamId64, const std::string& objectKey, const std::vector<uint8_t>& blob) override;
        ECloudError pullEncryptedObject(uint64_t steamId64, const std::string& objectKey, std::vector<uint8_t>& outBlob) override;
        ECloudError deleteRemoteObject(uint64_t steamId64, const std::string& objectKey) override;
        ECloudError commitRemoteManifest(uint64_t steamId64, const SyncStateManifest_t& manifest, const std::string& expectedETag, std::string& outNewETag) override;

        // RFC 8628 Device Flow
        ECloudError startDeviceAuthFlow(DeviceCodePrompt_t& outPrompt);
        ECloudError pollDeviceAuthToken(const std::string& deviceCode);

        // 1-Click Browser Flow (RFC 7636 PKCE + Loopback Redirect)
        std::string buildBrowserAuthUrl(const std::string& redirectUri,
                                        const std::string& state,
                                        const std::string& codeChallenge) const;
        ECloudError exchangeAuthCodeForToken(const std::string& authCode,
                                             const std::string& codeVerifier,
                                             const std::string& redirectUri);

        const CloudProviderConfig_t& config() const { return m_cfg; }

    private:
        CloudProviderConfig_t m_cfg;
        mutable std::string m_naviFolderId;
        mutable std::map<std::string, std::string> m_folderIdCache;

        bool executeWithAuthAndRetry(Curl::HttpRequest_t req, Curl::HttpResponse_t& outResp, ECloudError& outErr);
        ECloudError getOrCreateNaviFolderId(std::string& outFolderId);
        ECloudError getOrCreateFolderPath(const std::string& folderPath, std::string& outFolderId);
        ECloudError findFileIdByName(const std::string& name, std::string& outFileId, std::string& outETag);
    };

    class WebDavCloudProvider : public ICloudStorageProvider
    {
    public:
        explicit WebDavCloudProvider(CloudProviderConfig_t cfg);
        ~WebDavCloudProvider() override = default;

        ECloudProviderType providerType() const override { return ECloudProviderType::k_WebDav; }
        ECloudError refreshAuthTokenIfNeeded() override;
        ECloudError queryQuota(CloudQuotaInfo_t& outQuota) override;
        ECloudError fetchRemoteManifest(uint64_t steamId64, SyncStateManifest_t& outManifest, std::string& outETag) override;
        ECloudError pushEncryptedObject(uint64_t steamId64, const std::string& objectKey, const std::vector<uint8_t>& blob) override;
        ECloudError pullEncryptedObject(uint64_t steamId64, const std::string& objectKey, std::vector<uint8_t>& outBlob) override;
        ECloudError commitRemoteManifest(uint64_t steamId64, const SyncStateManifest_t& manifest, const std::string& expectedETag, std::string& outNewETag) override;

        const CloudProviderConfig_t& config() const { return m_cfg; }

    private:
        CloudProviderConfig_t m_cfg;

        std::string getAuthHeader() const;
        std::string normalizedBaseUrl() const;
        ECloudError ensureDirectories(uint64_t steamId64);
        bool executeWithRetry(Curl::HttpRequest_t req, Curl::HttpResponse_t& outResp, ECloudError& outErr);
    };

    std::unique_ptr<ICloudStorageProvider> createCloudProvider(const CloudProviderConfig_t& cfg);

} // namespace Sync
