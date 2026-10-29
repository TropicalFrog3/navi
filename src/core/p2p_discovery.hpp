#pragma once

#include "sync_manifest.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Sync
{
    struct DiscoveredPeer_t
    {
        std::string deviceId;
        std::string hostname;
        ESyncRole role = ESyncRole::k_Disabled;
        std::string ipAddress;
        uint16_t tlsPort = 0;
        uint64_t globalGeneration = 0;
        uint32_t keyEpoch = 1;
        std::string sidHashHex; // 16-char lowercase hex of HMAC-SHA256(key="navi-mdns-sid-v1-" + day, data=steamId64_le)
        int64_t lastSeenUnixMs = 0;

        bool operator==(const DiscoveredPeer_t&) const = default;
    };

    // Helper functions
    std::string computeSteamIdDiscoveryHash(uint64_t steamId64, int64_t unixTimeSec = 0);
    bool matchesSteamIdDiscoveryHash(uint64_t localSteamId64, std::string_view peerSidHash, int64_t unixTimeSec = 0);

    std::string serializeDiscoveryBeacon(const DiscoveredPeer_t& selfInfo);
    bool parseDiscoveryBeacon(std::string_view packet, const std::string& senderIp, DiscoveredPeer_t& outPeer);

    class P2PDiscoveryService
    {
    public:
        P2PDiscoveryService(uint64_t steamId64,
                            DiscoveredPeer_t localInfo,
                            uint16_t udpPort = 5353,
                            std::string multicastGroup = "224.0.0.251",
                            int announceIntervalMs = 200);
        ~P2PDiscoveryService();

        // Non-copyable, non-movable
        P2PDiscoveryService(const P2PDiscoveryService&) = delete;
        P2PDiscoveryService& operator=(const P2PDiscoveryService&) = delete;

        bool start();
        void stop();
        bool isRunning() const { return m_running.load(); }

        std::vector<DiscoveredPeer_t> getDiscoveredPeers(int64_t maxAgeMs = 10000) const;
        uint16_t boundUdpPort() const { return m_boundPort; }

        void sendBeaconTo(const std::string& destIp, uint16_t destUdpPort);
        void updateLocalInfo(const DiscoveredPeer_t& newLocalInfo);

    private:
        uint64_t m_steamId64 = 0;
        DiscoveredPeer_t m_localInfo;
        uint16_t m_requestedPort = 5353;
        uint16_t m_boundPort = 0;
        std::string m_multicastGroup;
        int m_announceIntervalMs = 200;

        std::atomic<bool> m_running{false};
        int m_socketFd = -1;
        std::thread m_workerThread;
        mutable std::mutex m_peersMutex;
        mutable std::mutex m_localInfoMutex;
        std::unordered_map<std::string, DiscoveredPeer_t> m_peers;

        void workerLoop();
        void sendMulticastBeacon();
    };

} // namespace Sync
