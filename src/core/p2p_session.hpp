#pragma once

#include "cloud_provider.hpp"
#include "sync_crypto.hpp"
#include "sync_manifest.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward declarations for OpenSSL types
typedef struct ssl_ctx_st SSL_CTX;

namespace Sync
{
    enum class EP2PError : uint8_t
    {
        k_Ok = 0,
        k_SocketError,
        k_Timeout,
        k_TlsHandshakeFailed,
        k_SteamIdMismatch,
        k_PinInvalid,
        k_LockedOut,
        k_UntrustedPeer,
        k_ProtocolError,
        k_CryptoError
    };

    std::string p2pErrorToString(EP2PError err);

    struct P2PPairingProvisionBundle_t
    {
        uint64_t steamId64 = 0;
        uint32_t keyEpoch = 1;
        MasterKey_t masterKey{};
        ECloudProviderType activeCloudProvider = ECloudProviderType::k_None;
        std::string webDavUrl;
        std::string webDavUser;
        std::vector<uint8_t> encryptedCloudAuthToken;
        std::string overriderDeviceId;
        std::string overriderPubKeyFingerprintHex;

        bool operator==(const P2PPairingProvisionBundle_t&) const = default;
    };

    std::string serializeProvisionBundle(const P2PPairingProvisionBundle_t& bundle);
    bool deserializeProvisionBundle(const std::string& yamlStr, P2PPairingProvisionBundle_t& outBundle);

    struct TrustedPeerEntry_t
    {
        std::string deviceId;
        std::string hostname;
        uint64_t steamId64 = 0;
        std::string certSha256FingerprintHex;
        int64_t pairedAtUnixSec = 0;

        bool operator==(const TrustedPeerEntry_t&) const = default;
    };

    bool generateSelfSignedIdentityCert(std::string& outCertPem,
                                        std::string& outKeyPem,
                                        std::string& outCertSha256Hex);

    class TrustedPeerStore
    {
    public:
        explicit TrustedPeerStore(std::string trustedPeersYamlPath);

        bool load();
        bool save() const;

        bool isPeerTrusted(uint64_t steamId64, const std::string& deviceId, const std::string& certSha256Hex) const;
        bool addTrustedPeer(const TrustedPeerEntry_t& entry);
        bool removeTrustedPeer(uint64_t steamId64, const std::string& deviceId);
        std::vector<TrustedPeerEntry_t> getTrustedPeers(uint64_t steamId64) const;

        const std::string& filePath() const { return m_filePath; }

    private:
        std::string m_filePath;
        mutable std::mutex m_mutex;
        std::vector<TrustedPeerEntry_t> m_peers;
    };

    std::string generateSixDigitPin();

    struct P2PServerConfig_t
    {
        uint64_t steamId64 = 0;
        std::string deviceId;
        std::string hostname;
        std::string certPem;
        std::string keyPem;
        std::shared_ptr<TrustedPeerStore> trustedStore;
        P2PPairingProvisionBundle_t provisionBundle;
        std::string activePin;
        ICloudStorageProvider* optionalCloudRelay = nullptr;
        SyncStateManifest_t syncManifest;
        std::unordered_map<std::string, std::vector<uint8_t>> syncBlobs;
    };

    class P2PSessionServer
    {
    public:
        explicit P2PSessionServer(P2PServerConfig_t cfg);
        ~P2PSessionServer();

        P2PSessionServer(const P2PSessionServer&) = delete;
        P2PSessionServer& operator=(const P2PSessionServer&) = delete;

        bool start(uint16_t port = 0, const std::string& bindIp = "127.0.0.1");
        void stop();
        bool isRunning() const { return m_running.load(); }
        uint16_t boundPort() const { return m_boundPort; }

        void setActivePin(const std::string& pin);
        std::string activePin() const;
        void regeneratePin();

        void setProvisionBundle(const P2PPairingProvisionBundle_t& bundle);
        void setSyncData(const SyncStateManifest_t& manifest,
                         const std::unordered_map<std::string, std::vector<uint8_t>>& blobs,
                         ICloudStorageProvider* cloudRelay = nullptr);

        bool isIpLockedOut(const std::string& ip) const;
        int failedAttemptsForIp(const std::string& ip) const;
        const std::string& certSha256Hex() const { return m_certSha256Hex; }

    private:
        P2PServerConfig_t m_cfg;
        std::string m_certSha256Hex;
        uint16_t m_boundPort = 0;
        std::atomic<bool> m_running{false};
        int m_listenFd = -1;
        std::thread m_acceptThread;
        std::vector<std::thread> m_clientThreads;
        mutable std::mutex m_threadsMutex;
        mutable std::mutex m_mutex;
        SSL_CTX* m_sslCtx = nullptr;

        struct FailInfo
        {
            int attempts = 0;
            int64_t lockoutUntilUnixSec = 0;
            int64_t windowStartUnixSec = 0;
        };
        std::unordered_map<std::string, FailInfo> m_failMap;

        void acceptLoop();
        void handleConnection(int clientFd, const std::string& clientIp);
        void recordFailedAttempt(const std::string& ip);
    };

    struct P2PClientConfig_t
    {
        uint64_t steamId64 = 0;
        std::string deviceId;
        std::string hostname;
        std::string certPem;
        std::string keyPem;
        std::shared_ptr<TrustedPeerStore> trustedStore;
    };

    class P2PSessionClient
    {
    public:
        explicit P2PSessionClient(P2PClientConfig_t cfg);
        ~P2PSessionClient();

        P2PSessionClient(const P2PSessionClient&) = delete;
        P2PSessionClient& operator=(const P2PSessionClient&) = delete;

        EP2PError pairWithPin(const std::string& serverIp,
                              uint16_t serverPort,
                              const std::string& pin6Digits,
                              P2PPairingProvisionBundle_t& outProvision);

        EP2PError syncWithPeer(const std::string& serverIp,
                               uint16_t serverPort,
                               const MasterKey_t& masterKey,
                               const SyncStateManifest_t& localManifest,
                               const std::unordered_map<std::string, std::vector<uint8_t>>& outgoingBlobs,
                               SyncStateManifest_t& outRemoteManifest,
                               std::unordered_map<std::string, std::vector<uint8_t>>& outIncomingBlobs);

        const std::string& certSha256Hex() const { return m_certSha256Hex; }

    private:
        P2PClientConfig_t m_cfg;
        std::string m_certSha256Hex;
        SSL_CTX* m_sslCtx = nullptr;

        int connectSocket(const std::string& serverIp, uint16_t serverPort);
    };

} // namespace Sync
