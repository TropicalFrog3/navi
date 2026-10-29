#include "p2p_discovery.hpp"

#include <arpa/inet.h>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

namespace Sync
{
    static bool computeHmacSha256Internal(const void* key, size_t keyLen,
                                          const void* data, size_t dataLen,
                                          uint8_t outMd[32])
    {
        EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
        if (!mac) return false;

        EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
        EVP_MAC_free(mac);
        if (!ctx) return false;

        char digestName[] = "SHA256";
        OSSL_PARAM params[2];
        params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digestName, 0);
        params[1] = OSSL_PARAM_construct_end();

        bool ok = (EVP_MAC_init(ctx, reinterpret_cast<const unsigned char*>(key), keyLen, params) > 0) &&
                  (EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(data), dataLen) > 0);

        size_t outLen = 32;
        if (ok)
        {
            ok = (EVP_MAC_final(ctx, outMd, &outLen, 32) > 0) && (outLen == 32);
        }

        EVP_MAC_CTX_free(ctx);
        return ok;
    }

    std::string computeSteamIdDiscoveryHash(uint64_t steamId64, int64_t unixTimeSec)
    {
        if (steamId64 == 0) return "";

        const int64_t nowSec = (unixTimeSec > 0) ? unixTimeSec : static_cast<int64_t>(time(nullptr));
        const int64_t utcDay = nowSec / 86400;

        const std::string key = "navi-mdns-sid-v1-" + std::to_string(utcDay);
        uint8_t sidLe[8];
        for (int i = 0; i < 8; ++i)
        {
            sidLe[i] = static_cast<uint8_t>((steamId64 >> (8 * i)) & 0xFF);
        }

        uint8_t md[32];
        if (!computeHmacSha256Internal(key.data(), key.size(), sidLe, sizeof(sidLe), md))
        {
            return "";
        }

        char hex[17];
        for (int i = 0; i < 8; ++i)
        {
            snprintf(hex + (i * 2), 3, "%02x", md[i]);
        }
        hex[16] = '\0';
        return std::string(hex);
    }

    bool matchesSteamIdDiscoveryHash(uint64_t localSteamId64, std::string_view peerSidHash, int64_t unixTimeSec)
    {
        if (localSteamId64 == 0 || peerSidHash.size() != 16) return false;

        const int64_t nowSec = (unixTimeSec > 0) ? unixTimeSec : static_cast<int64_t>(time(nullptr));
        const int64_t utcDay = nowSec / 86400;

        for (int delta = -1; delta <= 1; ++delta)
        {
            const int64_t testSec = (utcDay + delta) * 86400;
            const std::string expected = computeSteamIdDiscoveryHash(localSteamId64, testSec);
            if (expected.size() == 16 && strcasecmp(expected.c_str(), std::string(peerSidHash).c_str()) == 0)
            {
                return true;
            }
        }
        return false;
    }

    std::string serializeDiscoveryBeacon(const DiscoveredPeer_t& selfInfo)
    {
        std::string s;
        s.reserve(256);
        s += "NAVIP2P1\n";
        s += "ver=1\n";
        s += "dev=" + selfInfo.deviceId + "\n";
        s += "host=" + selfInfo.hostname + "\n";
        s += "role=" + std::to_string(static_cast<uint32_t>(selfInfo.role)) + "\n";
        s += "port=" + std::to_string(selfInfo.tlsPort) + "\n";
        s += "gen=" + std::to_string(selfInfo.globalGeneration) + "\n";
        s += "epoch=" + std::to_string(selfInfo.keyEpoch) + "\n";
        s += "sid_hash=" + selfInfo.sidHashHex + "\n";
        return s;
    }

    bool parseDiscoveryBeacon(std::string_view packet, const std::string& senderIp, DiscoveredPeer_t& outPeer)
    {
        const std::string_view magic = "NAVIP2P1\n";
        if (packet.size() < magic.size() || packet.substr(0, magic.size()) != magic)
        {
            return false;
        }

        std::string_view rest = packet.substr(magic.size());
        DiscoveredPeer_t peer;
        peer.ipAddress = senderIp;
        peer.lastSeenUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        size_t pos = 0;
        while (pos < rest.size())
        {
            size_t next = rest.find('\n', pos);
            std::string_view line = (next == std::string_view::npos)
                                        ? rest.substr(pos)
                                        : rest.substr(pos, next - pos);
            pos = (next == std::string_view::npos) ? rest.size() : next + 1;

            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            if (line.empty()) continue;

            size_t eq = line.find('=');
            if (eq == std::string_view::npos) continue;

            std::string_view key = line.substr(0, eq);
            std::string_view val = line.substr(eq + 1);

            if (key == "dev")
            {
                peer.deviceId = std::string(val);
            }
            else if (key == "host")
            {
                peer.hostname = std::string(val);
            }
            else if (key == "role")
            {
                uint32_t r = 0;
                if (auto res = std::from_chars(val.data(), val.data() + val.size(), r); res.ec == std::errc())
                {
                    peer.role = static_cast<ESyncRole>(r);
                }
            }
            else if (key == "port")
            {
                uint32_t p = 0;
                if (auto res = std::from_chars(val.data(), val.data() + val.size(), p); res.ec == std::errc())
                {
                    peer.tlsPort = static_cast<uint16_t>(p);
                }
            }
            else if (key == "gen")
            {
                uint64_t g = 0;
                if (auto res = std::from_chars(val.data(), val.data() + val.size(), g); res.ec == std::errc())
                {
                    peer.globalGeneration = g;
                }
            }
            else if (key == "epoch")
            {
                uint32_t ep = 0;
                if (auto res = std::from_chars(val.data(), val.data() + val.size(), ep); res.ec == std::errc())
                {
                    peer.keyEpoch = ep;
                }
            }
            else if (key == "sid_hash")
            {
                peer.sidHashHex = std::string(val);
            }
        }

        if (peer.deviceId.empty() || peer.tlsPort == 0 || peer.sidHashHex.empty())
        {
            return false;
        }

        outPeer = std::move(peer);
        return true;
    }

    P2PDiscoveryService::P2PDiscoveryService(uint64_t steamId64,
                                             DiscoveredPeer_t localInfo,
                                             uint16_t udpPort,
                                             std::string multicastGroup,
                                             int announceIntervalMs)
        : m_steamId64(steamId64),
          m_localInfo(std::move(localInfo)),
          m_requestedPort(udpPort),
          m_multicastGroup(std::move(multicastGroup)),
          m_announceIntervalMs(announceIntervalMs > 20 ? announceIntervalMs : 20)
    {
    }

    P2PDiscoveryService::~P2PDiscoveryService()
    {
        stop();
    }

    bool P2PDiscoveryService::start()
    {
        if (m_running.load()) return true;

        m_socketFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (m_socketFd < 0) return false;

        int reuse = 1;
        setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
        int broadcast = 1;
        setsockopt(m_socketFd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

        unsigned char loop = 1;
        setsockopt(m_socketFd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

        sockaddr_in bindAddr{};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        bindAddr.sin_port = htons(m_requestedPort);

        if (bind(m_socketFd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0)
        {
            close(m_socketFd);
            m_socketFd = -1;
            return false;
        }

        sockaddr_in actualAddr{};
        socklen_t addrLen = sizeof(actualAddr);
        if (getsockname(m_socketFd, reinterpret_cast<sockaddr*>(&actualAddr), &addrLen) == 0)
        {
            m_boundPort = ntohs(actualAddr.sin_port);
        }
        else
        {
            m_boundPort = m_requestedPort;
        }

        // Try to join multicast group if configured
        if (!m_multicastGroup.empty())
        {
            ip_mreq mreq{};
            if (inet_pton(AF_INET, m_multicastGroup.c_str(), &mreq.imr_multiaddr) == 1)
            {
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                // Non-fatal if join fails (e.g. strict container or network policy)
                setsockopt(m_socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_localInfoMutex);
            if (m_localInfo.sidHashHex.empty())
            {
                m_localInfo.sidHashHex = computeSteamIdDiscoveryHash(m_steamId64);
            }
        }

        m_running.store(true);
        m_workerThread = std::thread(&P2PDiscoveryService::workerLoop, this);
        return true;
    }

    void P2PDiscoveryService::stop()
    {
        if (!m_running.exchange(false)) return;

        if (m_socketFd >= 0)
        {
            shutdown(m_socketFd, SHUT_RDWR);
            close(m_socketFd);
            m_socketFd = -1;
        }

        if (m_workerThread.joinable())
        {
            m_workerThread.join();
        }
    }

    void P2PDiscoveryService::updateLocalInfo(const DiscoveredPeer_t& newLocalInfo)
    {
        std::lock_guard<std::mutex> lock(m_localInfoMutex);
        m_localInfo = newLocalInfo;
        if (m_localInfo.sidHashHex.empty())
        {
            m_localInfo.sidHashHex = computeSteamIdDiscoveryHash(m_steamId64);
        }
    }

    void P2PDiscoveryService::sendBeaconTo(const std::string& destIp, uint16_t destUdpPort)
    {
        if (m_socketFd < 0 || destUdpPort == 0) return;

        DiscoveredPeer_t infoCopy;
        {
            std::lock_guard<std::mutex> lock(m_localInfoMutex);
            m_localInfo.sidHashHex = computeSteamIdDiscoveryHash(m_steamId64);
            infoCopy = m_localInfo;
        }

        const std::string pkt = serializeDiscoveryBeacon(infoCopy);

        sockaddr_in destAddr{};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(destUdpPort);
        if (inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr) <= 0)
        {
            return;
        }

        sendto(m_socketFd, pkt.data(), pkt.size(), 0,
               reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
    }

    void P2PDiscoveryService::sendMulticastBeacon()
    {
        if (m_socketFd < 0) return;

        DiscoveredPeer_t infoCopy;
        {
            std::lock_guard<std::mutex> lock(m_localInfoMutex);
            m_localInfo.sidHashHex = computeSteamIdDiscoveryHash(m_steamId64);
            infoCopy = m_localInfo;
        }

        const std::string pkt = serializeDiscoveryBeacon(infoCopy);

        // Multicast
        if (!m_multicastGroup.empty())
        {
            sockaddr_in mcastAddr{};
            mcastAddr.sin_family = AF_INET;
            mcastAddr.sin_port = htons(m_requestedPort > 0 ? m_requestedPort : m_boundPort);
            if (inet_pton(AF_INET, m_multicastGroup.c_str(), &mcastAddr.sin_addr) > 0)
            {
                sendto(m_socketFd, pkt.data(), pkt.size(), 0,
                       reinterpret_cast<const sockaddr*>(&mcastAddr), sizeof(mcastAddr));
            }
        }

        // Also broadcast on 255.255.255.255 as LAN fallback
        if (m_boundPort > 0)
        {
            sockaddr_in bcastAddr{};
            bcastAddr.sin_family = AF_INET;
            bcastAddr.sin_port = htons(m_requestedPort > 0 ? m_requestedPort : m_boundPort);
            bcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto(m_socketFd, pkt.data(), pkt.size(), 0,
                   reinterpret_cast<const sockaddr*>(&bcastAddr), sizeof(bcastAddr));
        }
    }

    std::vector<DiscoveredPeer_t> P2PDiscoveryService::getDiscoveredPeers(int64_t maxAgeMs) const
    {
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::lock_guard<std::mutex> lock(m_peersMutex);
        std::vector<DiscoveredPeer_t> active;
        for (const auto& [id, peer] : m_peers)
        {
            if ((nowMs - peer.lastSeenUnixMs) <= maxAgeMs)
            {
                active.push_back(peer);
            }
        }
        return active;
    }

    void P2PDiscoveryService::workerLoop()
    {
        auto lastAnnounce = std::chrono::steady_clock::now() - std::chrono::milliseconds(m_announceIntervalMs);

        while (m_running.load())
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAnnounce).count() >= m_announceIntervalMs)
            {
                sendMulticastBeacon();
                lastAnnounce = now;
            }

            pollfd pfd{};
            pfd.fd = m_socketFd;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, 50);
            if (ret > 0 && (pfd.revents & POLLIN))
            {
                while (true)
                {
                    char buf[2048];
                    sockaddr_in srcAddr{};
                    socklen_t addrLen = sizeof(srcAddr);

                    ssize_t n = recvfrom(m_socketFd, buf, sizeof(buf) - 1, 0,
                                         reinterpret_cast<sockaddr*>(&srcAddr), &addrLen);
                    if (n < 0)
                    {
                        break; // EAGAIN / EWOULDBLOCK
                    }

                    buf[n] = '\0';
                    char senderIp[INET_ADDRSTRLEN] = {0};
                    inet_ntop(AF_INET, &srcAddr.sin_addr, senderIp, sizeof(senderIp));

                    DiscoveredPeer_t peer;
                    if (parseDiscoveryBeacon(std::string_view(buf, static_cast<size_t>(n)), senderIp, peer))
                    {
                        std::string myDevId;
                        {
                            std::lock_guard<std::mutex> lk(m_localInfoMutex);
                            myDevId = m_localInfo.deviceId;
                        }

                        // Filter out self and steamId hash mismatch
                        if (peer.deviceId != myDevId &&
                            matchesSteamIdDiscoveryHash(m_steamId64, peer.sidHashHex))
                        {
                            std::lock_guard<std::mutex> lock(m_peersMutex);
                            m_peers[peer.deviceId] = peer;
                        }
                    }
                }
            }
        }
    }

} // namespace Sync
