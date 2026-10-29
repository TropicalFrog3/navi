#include "p2p_session.hpp"
#include "atomic_file.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>

#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace Sync
{
    static std::string bytesToHex(const uint8_t* data, size_t len)
    {
        std::string hex;
        hex.reserve(len * 2);
        char buf[3];
        for (size_t i = 0; i < len; ++i)
        {
            snprintf(buf, sizeof(buf), "%02x", data[i]);
            hex.append(buf, 2);
        }
        return hex;
    }

    static std::vector<uint8_t> hexToBytes(std::string_view hex)
    {
        std::vector<uint8_t> bytes;
        if (hex.size() % 2 != 0) return bytes;
        bytes.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            char buf[3] = { hex[i], hex[i + 1], '\0' };
            char* end = nullptr;
            unsigned long b = strtoul(buf, &end, 16);
            if (end != buf + 2) return {};
            bytes.push_back(static_cast<uint8_t>(b));
        }
        return bytes;
    }

    static bool computeHmacSha256(const void* key, size_t keyLen,
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

    static std::string extractCertFingerprintFromPem(const std::string& certPem)
    {
        if (certPem.empty()) return "";
        BIO* bio = BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size()));
        if (!bio) return "";
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!cert) return "";

        unsigned char md[32];
        unsigned int mdLen = 0;
        std::string hex;
        if (X509_digest(cert, EVP_sha256(), md, &mdLen) == 1 && mdLen == 32)
        {
            hex = bytesToHex(md, 32);
        }
        X509_free(cert);
        return hex;
    }

    static std::string extractPeerCertFingerprint(SSL* ssl)
    {
        X509* cert = SSL_get_peer_certificate(ssl);
        if (!cert) return "";
        unsigned char md[32];
        unsigned int mdLen = 0;
        std::string hex;
        if (X509_digest(cert, EVP_sha256(), md, &mdLen) == 1 && mdLen == 32)
        {
            hex = bytesToHex(md, 32);
        }
        X509_free(cert);
        return hex;
    }

    enum class EP2PFrameType : uint8_t
    {
        k_HelloPair = 1,
        k_ClientPinProof = 2,
        k_ServerPinResponse = 3,
        k_HelloSync = 4,
        k_ClientKeyProof = 5,
        k_ServerKeyProof = 6,
        k_SyncTransfer = 7,
        k_Error = 0xFF
    };

    static bool sendFrame(SSL* ssl, EP2PFrameType type, const std::string_view& payload)
    {
        const uint32_t totalLen = static_cast<uint32_t>(1 + payload.size());
        const uint32_t netLen = htonl(totalLen);
        const uint8_t t = static_cast<uint8_t>(type);

        std::vector<uint8_t> buf(4 + 1 + payload.size());
        std::memcpy(buf.data(), &netLen, 4);
        buf[4] = t;
        if (!payload.empty())
        {
            std::memcpy(buf.data() + 5, payload.data(), payload.size());
        }

        size_t written = 0;
        while (written < buf.size())
        {
            int n = SSL_write(ssl, buf.data() + written, static_cast<int>(buf.size() - written));
            if (n <= 0) return false;
            written += static_cast<size_t>(n);
        }
        return true;
    }

    static bool recvFrame(SSL* ssl, EP2PFrameType& outType, std::string& outPayload)
    {
        uint32_t netLen = 0;
        size_t r = 0;
        while (r < 4)
        {
            int n = SSL_read(ssl, reinterpret_cast<uint8_t*>(&netLen) + r, static_cast<int>(4 - r));
            if (n <= 0) return false;
            r += static_cast<size_t>(n);
        }

        const uint32_t totalLen = ntohl(netLen);
        if (totalLen < 1 || totalLen > (64 * 1024 * 1024)) return false;

        uint8_t t = 0;
        int n = SSL_read(ssl, &t, 1);
        if (n <= 0) return false;
        outType = static_cast<EP2PFrameType>(t);

        const uint32_t payloadLen = totalLen - 1;
        outPayload.resize(payloadLen);
        r = 0;
        while (r < payloadLen)
        {
            n = SSL_read(ssl, reinterpret_cast<uint8_t*>(outPayload.data()) + r, static_cast<int>(payloadLen - r));
            if (n <= 0) return false;
            r += static_cast<size_t>(n);
        }
        return true;
    }

    struct CleanseGuard
    {
        void* ptr = nullptr;
        size_t len = 0;
        CleanseGuard(void* p, size_t l) : ptr(p), len(l) {}
        ~CleanseGuard()
        {
            if (ptr && len > 0)
            {
                OPENSSL_cleanse(ptr, len);
            }
        }
    };

    static void closeSslSocket(SSL* ssl, int fd)
    {
        if (ssl)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (fd >= 0)
        {
            shutdown(fd, SHUT_WR);
            char buf[512];
            while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
            close(fd);
        }
    }

    static std::string serializeSyncPayload(const SyncStateManifest_t& manifest,
                                            const std::unordered_map<std::string, std::vector<uint8_t>>& blobs)
    {
        std::string manifestYaml = serializeManifestToYaml(manifest);
        std::string out;

        const uint32_t yamlLen = static_cast<uint32_t>(manifestYaml.size());
        const uint32_t netYamlLen = htonl(yamlLen);
        out.append(reinterpret_cast<const char*>(&netYamlLen), 4);
        out.append(manifestYaml);

        const uint32_t blobCount = static_cast<uint32_t>(blobs.size());
        const uint32_t netCount = htonl(blobCount);
        out.append(reinterpret_cast<const char*>(&netCount), 4);

        for (const auto& [key, data] : blobs)
        {
            const uint32_t keyLen = static_cast<uint32_t>(key.size());
            const uint32_t netKeyLen = htonl(keyLen);
            out.append(reinterpret_cast<const char*>(&netKeyLen), 4);
            out.append(key);

            const uint32_t dataLen = static_cast<uint32_t>(data.size());
            const uint32_t netDataLen = htonl(dataLen);
            out.append(reinterpret_cast<const char*>(&netDataLen), 4);
            out.append(reinterpret_cast<const char*>(data.data()), data.size());
        }
        return out;
    }

    static bool deserializeSyncPayload(const std::string& payload,
                                       SyncStateManifest_t& outManifest,
                                       std::unordered_map<std::string, std::vector<uint8_t>>& outBlobs)
    {
        if (payload.size() < 8) return false;
        size_t offset = 0;

        uint32_t netYamlLen = 0;
        std::memcpy(&netYamlLen, payload.data() + offset, 4);
        offset += 4;
        const uint32_t yamlLen = ntohl(netYamlLen);
        if (offset + 4 > payload.size() || yamlLen > payload.size() - offset - 4) return false;

        std::string yamlStr(payload.data() + offset, yamlLen);
        offset += yamlLen;
        if (!deserializeManifestFromYaml(yamlStr, outManifest)) return false;

        uint32_t netBlobCount = 0;
        std::memcpy(&netBlobCount, payload.data() + offset, 4);
        offset += 4;
        const uint32_t blobCount = ntohl(netBlobCount);

        outBlobs.clear();
        for (uint32_t i = 0; i < blobCount; ++i)
        {
            if (offset + 4 > payload.size()) return false;
            uint32_t netKeyLen = 0;
            std::memcpy(&netKeyLen, payload.data() + offset, 4);
            offset += 4;
            const uint32_t keyLen = ntohl(netKeyLen);
            if (offset + 4 > payload.size() || keyLen > payload.size() - offset - 4) return false;

            std::string key(payload.data() + offset, keyLen);
            offset += keyLen;

            uint32_t netDataLen = 0;
            std::memcpy(&netDataLen, payload.data() + offset, 4);
            offset += 4;
            const uint32_t dataLen = ntohl(netDataLen);
            if (dataLen > payload.size() - offset) return false;

            std::vector<uint8_t> data(payload.data() + offset, payload.data() + offset + dataLen);
            offset += dataLen;

            outBlobs[key] = std::move(data);
        }
        return true;
    }

    static SSL_CTX* createTlsContext(bool isServer, const std::string& certPem, const std::string& keyPem)
    {
        const SSL_METHOD* method = isServer ? TLS_server_method() : TLS_client_method();
        SSL_CTX* ctx = SSL_CTX_new(method);
        if (!ctx) return nullptr;

        SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

        // Load certificate
        BIO* cbio = BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size()));
        X509* cert = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr);
        BIO_free(cbio);
        if (!cert || SSL_CTX_use_certificate(ctx, cert) <= 0)
        {
            if (cert) X509_free(cert);
            SSL_CTX_free(ctx);
            return nullptr;
        }
        X509_free(cert);

        // Load private key
        BIO* kbio = BIO_new_mem_buf(keyPem.data(), static_cast<int>(keyPem.size()));
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(kbio, nullptr, nullptr, nullptr);
        BIO_free(kbio);
        if (!pkey || SSL_CTX_use_PrivateKey(ctx, pkey) <= 0)
        {
            if (pkey) EVP_PKEY_free(pkey);
            SSL_CTX_free(ctx);
            return nullptr;
        }
        EVP_PKEY_free(pkey);

        // Accept self-signed certificates at TLS transport layer; verification is done at app layer
        auto verifyCb = [](int /*preverify_ok*/, X509_STORE_CTX* /*x509_ctx*/) -> int {
            return 1;
        };

        if (isServer)
        {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verifyCb);
        }
        else
        {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verifyCb);
        }

        return ctx;
    }

    std::string p2pErrorToString(EP2PError err)
    {
        switch (err)
        {
            case EP2PError::k_Ok: return "Ok";
            case EP2PError::k_SocketError: return "SocketError";
            case EP2PError::k_Timeout: return "Timeout";
            case EP2PError::k_TlsHandshakeFailed: return "TlsHandshakeFailed";
            case EP2PError::k_SteamIdMismatch: return "SteamIdMismatch";
            case EP2PError::k_PinInvalid: return "PinInvalid";
            case EP2PError::k_LockedOut: return "LockedOut";
            case EP2PError::k_UntrustedPeer: return "UntrustedPeer";
            case EP2PError::k_ProtocolError: return "ProtocolError";
            case EP2PError::k_CryptoError: return "CryptoError";
            default: return "Unknown";
        }
    }

    std::string serializeProvisionBundle(const P2PPairingProvisionBundle_t& bundle)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "steamid64" << YAML::Value << std::to_string(bundle.steamId64);
        out << YAML::Key << "key_epoch" << YAML::Value << bundle.keyEpoch;
        out << YAML::Key << "master_key" << YAML::Value << bytesToHex(bundle.masterKey.data(), bundle.masterKey.size());
        out << YAML::Key << "cloud_provider" << YAML::Value << cloudProviderToString(bundle.activeCloudProvider);
        out << YAML::Key << "webdav_url" << YAML::Value << bundle.webDavUrl;
        out << YAML::Key << "webdav_user" << YAML::Value << bundle.webDavUser;
        out << YAML::Key << "cloud_token_hex" << YAML::Value << bytesToHex(bundle.encryptedCloudAuthToken.data(), bundle.encryptedCloudAuthToken.size());
        out << YAML::Key << "overrider_device_id" << YAML::Value << bundle.overriderDeviceId;
        out << YAML::Key << "overrider_pubkey_fp" << YAML::Value << bundle.overriderPubKeyFingerprintHex;
        out << YAML::EndMap;
        return std::string(out.c_str());
    }

    bool deserializeProvisionBundle(const std::string& yamlStr, P2PPairingProvisionBundle_t& outBundle)
    {
        try
        {
            YAML::Node node = YAML::Load(yamlStr);
            if (!node.IsMap()) return false;

            if (node["steamid64"]) outBundle.steamId64 = std::stoull(node["steamid64"].as<std::string>());
            if (node["key_epoch"]) outBundle.keyEpoch = node["key_epoch"].as<uint32_t>();
            if (node["master_key"])
            {
                std::string hex = node["master_key"].as<std::string>();
                std::vector<uint8_t> keyBytes = hexToBytes(hex);
                if (keyBytes.size() == 32)
                {
                    std::memcpy(outBundle.masterKey.data(), keyBytes.data(), 32);
                }
                if (!hex.empty()) OPENSSL_cleanse(hex.data(), hex.size());
                if (!keyBytes.empty()) OPENSSL_cleanse(keyBytes.data(), keyBytes.size());
            }
            if (node["cloud_provider"]) outBundle.activeCloudProvider = parseCloudProvider(node["cloud_provider"].as<std::string>());
            if (node["webdav_url"]) outBundle.webDavUrl = node["webdav_url"].as<std::string>();
            if (node["webdav_user"]) outBundle.webDavUser = node["webdav_user"].as<std::string>();
            if (node["cloud_token_hex"]) outBundle.encryptedCloudAuthToken = hexToBytes(node["cloud_token_hex"].as<std::string>());
            if (node["overrider_device_id"]) outBundle.overriderDeviceId = node["overrider_device_id"].as<std::string>();
            if (node["overrider_pubkey_fp"]) outBundle.overriderPubKeyFingerprintHex = node["overrider_pubkey_fp"].as<std::string>();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool generateSelfSignedIdentityCert(std::string& outCertPem,
                                        std::string& outKeyPem,
                                        std::string& outCertSha256Hex)
    {
        EVP_PKEY* pkey = nullptr;
        EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        if (!kctx) return false;
        if (EVP_PKEY_keygen_init(kctx) <= 0 ||
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1) <= 0 ||
            EVP_PKEY_keygen(kctx, &pkey) <= 0)
        {
            EVP_PKEY_CTX_free(kctx);
            return false;
        }
        EVP_PKEY_CTX_free(kctx);

        X509* x509 = X509_new();
        if (!x509)
        {
            EVP_PKEY_free(pkey);
            return false;
        }

        ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
        X509_gmtime_adj(X509_get_notBefore(x509), 0);
        X509_gmtime_adj(X509_get_notAfter(x509), 315360000L); // 10 years

        X509_NAME* name = X509_get_subject_name(x509);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("navi-p2p-node"), -1, -1, 0);
        X509_set_issuer_name(x509, name);
        X509_set_pubkey(x509, pkey);

        if (X509_sign(x509, pkey, EVP_sha256()) <= 0)
        {
            X509_free(x509);
            EVP_PKEY_free(pkey);
            return false;
        }

        // Export cert PEM
        BIO* cbio = BIO_new(BIO_s_mem());
        PEM_write_bio_X509(cbio, x509);
        BUF_MEM* cmem = nullptr;
        BIO_get_mem_ptr(cbio, &cmem);
        outCertPem.assign(cmem->data, cmem->length);
        BIO_free(cbio);

        // Export key PEM
        BIO* kbio = BIO_new(BIO_s_mem());
        PEM_write_bio_PrivateKey(kbio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        BUF_MEM* kmem = nullptr;
        BIO_get_mem_ptr(kbio, &kmem);
        outKeyPem.assign(kmem->data, kmem->length);
        BIO_free(kbio);

        // SHA-256 fingerprint
        unsigned char md[32];
        unsigned int mdLen = 0;
        if (X509_digest(x509, EVP_sha256(), md, &mdLen) == 1 && mdLen == 32)
        {
            outCertSha256Hex = bytesToHex(md, 32);
        }

        X509_free(x509);
        EVP_PKEY_free(pkey);
        return true;
    }

    TrustedPeerStore::TrustedPeerStore(std::string trustedPeersYamlPath)
        : m_filePath(std::move(trustedPeersYamlPath))
    {
        load();
    }

    bool TrustedPeerStore::load()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_peers.clear();

        if (m_filePath.empty()) return false;
        std::error_code ec;
        if (!fs::exists(m_filePath, ec))
        {
            return true;
        }

        try
        {
            YAML::Node root = YAML::LoadFile(m_filePath);
            if (root && root["trusted_peers"] && root["trusted_peers"].IsSequence())
            {
                for (const auto& item : root["trusted_peers"])
                {
                    TrustedPeerEntry_t e;
                    if (item["device_id"]) e.deviceId = item["device_id"].as<std::string>();
                    if (item["hostname"]) e.hostname = item["hostname"].as<std::string>();
                    if (item["steamid64"]) e.steamId64 = std::stoull(item["steamid64"].as<std::string>());
                    if (item["cert_sha256"]) e.certSha256FingerprintHex = item["cert_sha256"].as<std::string>();
                    if (item["paired_at"]) e.pairedAtUnixSec = item["paired_at"].as<int64_t>();
                    m_peers.push_back(std::move(e));
                }
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TrustedPeerStore::save() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_filePath.empty()) return false;

        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "trusted_peers" << YAML::Value << YAML::BeginSeq;
        for (const auto& e : m_peers)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "device_id" << YAML::Value << e.deviceId;
            out << YAML::Key << "hostname" << YAML::Value << e.hostname;
            out << YAML::Key << "steamid64" << YAML::Value << std::to_string(e.steamId64);
            out << YAML::Key << "cert_sha256" << YAML::Value << e.certSha256FingerprintHex;
            out << YAML::Key << "paired_at" << YAML::Value << e.pairedAtUnixSec;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        return AtomicFile::write(m_filePath, std::string_view(out.c_str()), false);
    }

    bool TrustedPeerStore::isPeerTrusted(uint64_t steamId64, const std::string& deviceId, const std::string& certSha256Hex) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& e : m_peers)
        {
            if (e.steamId64 == steamId64)
            {
                if (deviceId.empty() || e.deviceId == deviceId)
                {
                    if (strcasecmp(e.certSha256FingerprintHex.c_str(), certSha256Hex.c_str()) == 0)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool TrustedPeerStore::addTrustedPeer(const TrustedPeerEntry_t& entry)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            bool found = false;
            for (auto& e : m_peers)
            {
                if (e.steamId64 == entry.steamId64 && e.deviceId == entry.deviceId)
                {
                    e = entry;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                m_peers.push_back(entry);
            }
        }
        return save();
    }

    bool TrustedPeerStore::removeTrustedPeer(uint64_t steamId64, const std::string& deviceId)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = std::remove_if(m_peers.begin(), m_peers.end(), [&](const TrustedPeerEntry_t& e) {
                return e.steamId64 == steamId64 && e.deviceId == deviceId;
            });
            if (it != m_peers.end())
            {
                m_peers.erase(it, m_peers.end());
            }
        }
        return save();
    }

    std::vector<TrustedPeerEntry_t> TrustedPeerStore::getTrustedPeers(uint64_t steamId64) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<TrustedPeerEntry_t> res;
        for (const auto& e : m_peers)
        {
            if (e.steamId64 == steamId64)
            {
                res.push_back(e);
            }
        }
        return res;
    }

    std::string generateSixDigitPin()
    {
        uint32_t val = 0;
        const uint32_t limit = 1000000;
        const uint32_t maxUnbiased = (0xFFFFFFFFu / limit) * limit;
        do
        {
            if (RAND_bytes(reinterpret_cast<unsigned char*>(&val), sizeof(val)) != 1)
            {
                val = static_cast<uint32_t>(rand());
            }
        } while (val >= maxUnbiased);

        val %= limit;
        char buf[7];
        snprintf(buf, sizeof(buf), "%06u", val);
        return std::string(buf);
    }

    P2PSessionServer::P2PSessionServer(P2PServerConfig_t cfg)
        : m_cfg(std::move(cfg))
    {
        signal(SIGPIPE, SIG_IGN);

        if (m_cfg.certPem.empty() || m_cfg.keyPem.empty())
        {
            generateSelfSignedIdentityCert(m_cfg.certPem, m_cfg.keyPem, m_certSha256Hex);
        }
        else
        {
            m_certSha256Hex = extractCertFingerprintFromPem(m_cfg.certPem);
        }

        if (m_cfg.activePin.empty())
        {
            m_cfg.activePin = generateSixDigitPin();
        }

        m_sslCtx = createTlsContext(true, m_cfg.certPem, m_cfg.keyPem);
    }

    P2PSessionServer::~P2PSessionServer()
    {
        stop();
        if (m_sslCtx)
        {
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
        }
    }

    void P2PSessionServer::setActivePin(const std::string& pin)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cfg.activePin = pin;
    }

    std::string P2PSessionServer::activePin() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cfg.activePin;
    }

    void P2PSessionServer::regeneratePin()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cfg.activePin = generateSixDigitPin();
    }

    void P2PSessionServer::setProvisionBundle(const P2PPairingProvisionBundle_t& bundle)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cfg.provisionBundle = bundle;
    }

    void P2PSessionServer::setSyncData(const SyncStateManifest_t& manifest,
                                       const std::unordered_map<std::string, std::vector<uint8_t>>& blobs,
                                       ICloudStorageProvider* cloudRelay)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cfg.syncManifest = manifest;
        m_cfg.syncBlobs = blobs;
        m_cfg.optionalCloudRelay = cloudRelay;
    }

    bool P2PSessionServer::isIpLockedOut(const std::string& ip) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_failMap.find(ip);
        if (it == m_failMap.end()) return false;
        const int64_t now = time(nullptr);
        return now < it->second.lockoutUntilUnixSec;
    }

    int P2PSessionServer::failedAttemptsForIp(const std::string& ip) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_failMap.find(ip);
        if (it == m_failMap.end()) return 0;
        return it->second.attempts;
    }

    void P2PSessionServer::recordFailedAttempt(const std::string& ip)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int64_t now = time(nullptr);
        auto& info = m_failMap[ip];
        if (info.attempts == 0 || (now - info.windowStartUnixSec > 900))
        {
            info.attempts = 1;
            info.windowStartUnixSec = now;
            info.lockoutUntilUnixSec = 0;
        }
        else
        {
            info.attempts++;
        }

        if (info.attempts >= 3)
        {
            info.lockoutUntilUnixSec = now + 900; // 15 minutes lockout
            m_cfg.activePin = generateSixDigitPin(); // Invalidate & regenerate active PIN
        }
    }

    bool P2PSessionServer::start(uint16_t port, const std::string& bindIp)
    {
        if (m_running.load() || !m_sslCtx) return false;

        m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listenFd < 0) return false;

        int reuse = 1;
        setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, bindIp.c_str(), &addr.sin_addr) <= 0)
        {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }

        if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            close(m_listenFd);
            m_listenFd = -1;
            return false;
        }

        if (listen(m_listenFd, 16) < 0)
        {
            close(m_listenFd);
            m_listenFd = -1;
            return false;
        }

        sockaddr_in actualAddr{};
        socklen_t addrLen = sizeof(actualAddr);
        if (getsockname(m_listenFd, reinterpret_cast<sockaddr*>(&actualAddr), &addrLen) == 0)
        {
            m_boundPort = ntohs(actualAddr.sin_port);
        }
        else
        {
            m_boundPort = port;
        }

        m_running.store(true);
        m_acceptThread = std::thread(&P2PSessionServer::acceptLoop, this);
        return true;
    }

    void P2PSessionServer::stop()
    {
        if (!m_running.exchange(false)) return;

        if (m_listenFd >= 0)
        {
            shutdown(m_listenFd, SHUT_RDWR);
            close(m_listenFd);
            m_listenFd = -1;
        }

        if (m_acceptThread.joinable())
        {
            m_acceptThread.join();
        }

        {
            std::lock_guard<std::mutex> lk(m_threadsMutex);
            for (auto& th : m_clientThreads)
            {
                if (th.joinable())
                {
                    th.join();
                }
            }
            m_clientThreads.clear();
        }
    }

    void P2PSessionServer::acceptLoop()
    {
        while (m_running.load())
        {
            pollfd pfd{};
            pfd.fd = m_listenFd;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, 100);
            if (ret > 0 && (pfd.revents & POLLIN))
            {
                sockaddr_in clientAddr{};
                socklen_t addrLen = sizeof(clientAddr);
                int clientFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
                if (clientFd >= 0)
                {
                    char clientIp[INET_ADDRSTRLEN] = {0};
                    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));

                    std::lock_guard<std::mutex> lk(m_threadsMutex);
                    m_clientThreads.emplace_back([this, clientFd, ip = std::string(clientIp)]() {
                        handleConnection(clientFd, ip);
                    });
                }
            }
        }
    }

    void P2PSessionServer::handleConnection(int clientFd, const std::string& clientIp)
    {
        timeval tv{5, 0};
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        SSL* ssl = SSL_new(m_sslCtx);
        if (!ssl)
        {
            close(clientFd);
            return;
        }
        SSL_set_fd(ssl, clientFd);

        if (SSL_accept(ssl) <= 0)
        {
            closeSslSocket(ssl, clientFd);
            return;
        }

        const std::string peerCertSha256Hex = extractPeerCertFingerprint(ssl);
        if (peerCertSha256Hex.empty())
        {
            sendFrame(ssl, EP2PFrameType::k_Error, "UNTRUSTED_PEER");
            closeSslSocket(ssl, clientFd);
            return;
        }

        uint8_t exporterBuf[32];
        CleanseGuard g_exp(exporterBuf, sizeof(exporterBuf));
        if (SSL_export_keying_material(ssl, exporterBuf, sizeof(exporterBuf),
                                       "EXPORTER-navi-p2p-v1", 20,
                                       nullptr, 0, 0) <= 0)
        {
            closeSslSocket(ssl, clientFd);
            return;
        }

        EP2PFrameType frameType{};
        std::string payload;
        if (!recvFrame(ssl, frameType, payload))
        {
            closeSslSocket(ssl, clientFd);
            return;
        }

        if (frameType == EP2PFrameType::k_HelloPair)
        {
            if (isIpLockedOut(clientIp))
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "LOCKED_OUT");
                closeSslSocket(ssl, clientFd);
                return;
            }

            uint64_t clientSteamId64 = 0;
            std::string clientDeviceId;
            std::string clientHostname;
            try
            {
                YAML::Node node = YAML::Load(payload);
                if (node["steamid64"]) clientSteamId64 = std::stoull(node["steamid64"].as<std::string>());
                if (node["device_id"]) clientDeviceId = node["device_id"].as<std::string>();
                if (node["hostname"]) clientHostname = node["hostname"].as<std::string>();
            }
            catch (...)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "PROTOCOL_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            if (clientSteamId64 != m_cfg.steamId64)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "STEAMID_MISMATCH");
                closeSslSocket(ssl, clientFd);
                return;
            }

            MasterKey_t ikm;
            CleanseGuard g_ikm(ikm.data(), ikm.size());
            std::memcpy(ikm.data(), exporterBuf, 32);
            uint8_t salt[8];
            for (int i = 0; i < 8; ++i)
            {
                salt[i] = static_cast<uint8_t>((m_cfg.steamId64 >> (8 * i)) & 0xFF);
            }

            const std::string pin = activePin();
            const std::string info = "navi-p2p-pin-" + pin + "-" + peerCertSha256Hex + "-" + m_certSha256Hex;
            MasterKey_t K_pair;
            CleanseGuard g_kpair(K_pair.data(), K_pair.size());
            if (!Sync::deriveSubkey(ikm, salt, 8, info, K_pair))
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "CRYPTO_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            EP2PFrameType proofType{};
            std::string proofPayload;
            if (!recvFrame(ssl, proofType, proofPayload) || proofType != EP2PFrameType::k_ClientPinProof || proofPayload.size() != 32)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "PROTOCOL_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            uint8_t expectedClientProof[32];
            computeHmacSha256(K_pair.data(), K_pair.size(), "client-pin-proof", 16, expectedClientProof);

            if (CRYPTO_memcmp(proofPayload.data(), expectedClientProof, 32) != 0)
            {
                recordFailedAttempt(clientIp);
                sendFrame(ssl, EP2PFrameType::k_Error, "PIN_INVALID");
                closeSslSocket(ssl, clientFd);
                return;
            }

            // Success: clear IP failed count
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_failMap.erase(clientIp);
            }

            // Add client to trusted peer store
            if (m_cfg.trustedStore)
            {
                TrustedPeerEntry_t entry;
                entry.deviceId = clientDeviceId;
                entry.hostname = clientHostname;
                entry.steamId64 = m_cfg.steamId64;
                entry.certSha256FingerprintHex = peerCertSha256Hex;
                entry.pairedAtUnixSec = time(nullptr);
                m_cfg.trustedStore->addTrustedPeer(entry);
            }

            P2PPairingProvisionBundle_t bundle;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                bundle = m_cfg.provisionBundle;
            }
            bundle.steamId64 = m_cfg.steamId64;
            bundle.overriderDeviceId = m_cfg.deviceId;
            bundle.overriderPubKeyFingerprintHex = m_certSha256Hex;

            std::string bundleYaml = serializeProvisionBundle(bundle);
            CleanseGuard g_byaml(bundleYaml.data(), bundleYaml.size());
            CleanseGuard g_bkey(bundle.masterKey.data(), bundle.masterKey.size());

            EncryptedEnvelopeMetadata_t meta;
            meta.steamId64 = m_cfg.steamId64;
            meta.keyEpoch = 1;
            meta.category = ESyncCategory::k_ManifestsAndLua;
            meta.logicalPath = "p2p://provision_bundle";
            meta.generation = 0;

            std::vector<uint8_t> plainVec(bundleYaml.begin(), bundleYaml.end());
            CleanseGuard g_pvec(plainVec.data(), plainVec.size());
            std::vector<uint8_t> encVec;
            if (!Sync::encryptBlob(K_pair, meta, plainVec, encVec))
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "CRYPTO_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            uint8_t serverProof[32];
            computeHmacSha256(K_pair.data(), K_pair.size(), "server-pin-proof", 16, serverProof);

            std::string respPayload;
            respPayload.reserve(32 + encVec.size());
            respPayload.append(reinterpret_cast<const char*>(serverProof), 32);
            respPayload.append(reinterpret_cast<const char*>(encVec.data()), encVec.size());

            sendFrame(ssl, EP2PFrameType::k_ServerPinResponse, respPayload);
        }
        else if (frameType == EP2PFrameType::k_HelloSync)
        {
            uint64_t clientSteamId64 = 0;
            std::string clientDeviceId;
            try
            {
                YAML::Node node = YAML::Load(payload);
                if (node["steamid64"]) clientSteamId64 = std::stoull(node["steamid64"].as<std::string>());
                if (node["device_id"]) clientDeviceId = node["device_id"].as<std::string>();
            }
            catch (...)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "PROTOCOL_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            if (clientSteamId64 != m_cfg.steamId64)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "STEAMID_MISMATCH");
                closeSslSocket(ssl, clientFd);
                return;
            }

            if (!m_cfg.trustedStore || !m_cfg.trustedStore->isPeerTrusted(m_cfg.steamId64, clientDeviceId, peerCertSha256Hex))
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "UNTRUSTED_PEER");
                closeSslSocket(ssl, clientFd);
                return;
            }

            // Proof of masterKey possession bound to exporterBuf
            std::vector<uint8_t> proofData(32 + 8);
            CleanseGuard g_pfd(proofData.data(), proofData.size());
            std::memcpy(proofData.data(), exporterBuf, 32);
            for (int i = 0; i < 8; ++i)
            {
                proofData[32 + i] = static_cast<uint8_t>((m_cfg.steamId64 >> (8 * i)) & 0xFF);
            }

            uint8_t expectedProof[32];
            CleanseGuard g_expProof(expectedProof, sizeof(expectedProof));
            computeHmacSha256(m_cfg.provisionBundle.masterKey.data(), m_cfg.provisionBundle.masterKey.size(),
                              proofData.data(), proofData.size(), expectedProof);

            EP2PFrameType keyProofType{};
            std::string clientKeyProof;
            if (!recvFrame(ssl, keyProofType, clientKeyProof) || keyProofType != EP2PFrameType::k_ClientKeyProof || clientKeyProof.size() != 32)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "PROTOCOL_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            if (CRYPTO_memcmp(clientKeyProof.data(), expectedProof, 32) != 0)
            {
                sendFrame(ssl, EP2PFrameType::k_Error, "CRYPTO_ERROR");
                closeSslSocket(ssl, clientFd);
                return;
            }

            sendFrame(ssl, EP2PFrameType::k_ServerKeyProof, std::string_view(reinterpret_cast<const char*>(expectedProof), 32));

            // Sync transfer
            SyncStateManifest_t manifestCopy;
            std::unordered_map<std::string, std::vector<uint8_t>> blobsCopy;
            ICloudStorageProvider* relay = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                manifestCopy = m_cfg.syncManifest;
                blobsCopy = m_cfg.syncBlobs;
                relay = m_cfg.optionalCloudRelay;
            }

            const std::string syncPayload = serializeSyncPayload(manifestCopy, blobsCopy);
            sendFrame(ssl, EP2PFrameType::k_SyncTransfer, syncPayload);

            // Optional Cloud Relay commit
            if (relay != nullptr)
            {
                std::string newETag;
                relay->commitRemoteManifest(m_cfg.steamId64, manifestCopy, "", newETag);
                for (const auto& [objKey, blob] : blobsCopy)
                {
                    relay->pushEncryptedObject(m_cfg.steamId64, objKey, blob);
                }
            }
        }
        else
        {
            sendFrame(ssl, EP2PFrameType::k_Error, "PROTOCOL_ERROR");
        }

        closeSslSocket(ssl, clientFd);
    }

    P2PSessionClient::P2PSessionClient(P2PClientConfig_t cfg)
        : m_cfg(std::move(cfg))
    {
        signal(SIGPIPE, SIG_IGN);

        if (m_cfg.certPem.empty() || m_cfg.keyPem.empty())
        {
            generateSelfSignedIdentityCert(m_cfg.certPem, m_cfg.keyPem, m_certSha256Hex);
        }
        else
        {
            m_certSha256Hex = extractCertFingerprintFromPem(m_cfg.certPem);
        }

        m_sslCtx = createTlsContext(false, m_cfg.certPem, m_cfg.keyPem);
    }

    P2PSessionClient::~P2PSessionClient()
    {
        if (m_sslCtx)
        {
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
        }
    }

    int P2PSessionClient::connectSocket(const std::string& serverIp, uint16_t serverPort)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;

        timeval tv{5, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        if (inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr) <= 0)
        {
            close(sock);
            return -1;
        }

        if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
        {
            close(sock);
            return -1;
        }

        return sock;
    }

    EP2PError P2PSessionClient::pairWithPin(const std::string& serverIp,
                                            uint16_t serverPort,
                                            const std::string& pin6Digits,
                                            P2PPairingProvisionBundle_t& outProvision)
    {
        if (!m_sslCtx) return EP2PError::k_CryptoError;

        int sock = connectSocket(serverIp, serverPort);
        if (sock < 0) return EP2PError::k_SocketError;

        SSL* ssl = SSL_new(m_sslCtx);
        if (!ssl)
        {
            close(sock);
            return EP2PError::k_CryptoError;
        }
        SSL_set_fd(ssl, sock);

        if (SSL_connect(ssl) <= 0)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_TlsHandshakeFailed;
        }

        const std::string peerCertSha256Hex = extractPeerCertFingerprint(ssl);
        if (peerCertSha256Hex.empty())
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_TlsHandshakeFailed;
        }

        uint8_t exporterBuf[32];
        CleanseGuard g_exp(exporterBuf, sizeof(exporterBuf));
        if (SSL_export_keying_material(ssl, exporterBuf, sizeof(exporterBuf),
                                       "EXPORTER-navi-p2p-v1", 20,
                                       nullptr, 0, 0) <= 0)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_TlsHandshakeFailed;
        }

        YAML::Emitter helloOut;
        helloOut << YAML::BeginMap;
        helloOut << YAML::Key << "steamid64" << YAML::Value << std::to_string(m_cfg.steamId64);
        helloOut << YAML::Key << "device_id" << YAML::Value << m_cfg.deviceId;
        helloOut << YAML::Key << "hostname" << YAML::Value << m_cfg.hostname;
        helloOut << YAML::EndMap;

        if (!sendFrame(ssl, EP2PFrameType::k_HelloPair, helloOut.c_str()))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_SocketError;
        }

        MasterKey_t ikm;
        CleanseGuard g_ikm(ikm.data(), ikm.size());
        std::memcpy(ikm.data(), exporterBuf, 32);
        uint8_t salt[8];
        for (int i = 0; i < 8; ++i)
        {
            salt[i] = static_cast<uint8_t>((m_cfg.steamId64 >> (8 * i)) & 0xFF);
        }

        const std::string info = "navi-p2p-pin-" + pin6Digits + "-" + m_certSha256Hex + "-" + peerCertSha256Hex;
        MasterKey_t K_pair;
        CleanseGuard g_kpair(K_pair.data(), K_pair.size());
        if (!Sync::deriveSubkey(ikm, salt, 8, info, K_pair))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_CryptoError;
        }

        uint8_t clientProof[32];
        computeHmacSha256(K_pair.data(), K_pair.size(), "client-pin-proof", 16, clientProof);
        if (!sendFrame(ssl, EP2PFrameType::k_ClientPinProof, std::string_view(reinterpret_cast<const char*>(clientProof), 32)))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_SocketError;
        }

        EP2PFrameType respType{};
        std::string respPayload;
        if (!recvFrame(ssl, respType, respPayload))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_Timeout;
        }

        if (respType == EP2PFrameType::k_Error)
        {
            closeSslSocket(ssl, sock);
            if (respPayload == "LOCKED_OUT") return EP2PError::k_LockedOut;
            if (respPayload == "STEAMID_MISMATCH") return EP2PError::k_SteamIdMismatch;
            if (respPayload == "PIN_INVALID") return EP2PError::k_PinInvalid;
            if (respPayload == "UNTRUSTED_PEER") return EP2PError::k_UntrustedPeer;
            return EP2PError::k_ProtocolError;
        }

        if (respType != EP2PFrameType::k_ServerPinResponse || respPayload.size() < 32)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_ProtocolError;
        }

        uint8_t expectedServerProof[32];
        computeHmacSha256(K_pair.data(), K_pair.size(), "server-pin-proof", 16, expectedServerProof);

        if (CRYPTO_memcmp(respPayload.data(), expectedServerProof, 32) != 0)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_CryptoError;
        }

        EncryptedEnvelopeMetadata_t meta;
        meta.steamId64 = m_cfg.steamId64;
        meta.keyEpoch = 1;
        meta.category = ESyncCategory::k_ManifestsAndLua;
        meta.logicalPath = "p2p://provision_bundle";
        meta.generation = 0;

        const std::vector<uint8_t> encVec(respPayload.begin() + 32, respPayload.end());
        std::vector<uint8_t> plainVec;
        if (!Sync::decryptBlob(K_pair, meta, encVec, plainVec))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_CryptoError;
        }
        CleanseGuard g_pvec(plainVec.data(), plainVec.size());

        std::string yamlStr(reinterpret_cast<const char*>(plainVec.data()), plainVec.size());
        CleanseGuard g_ystr(yamlStr.data(), yamlStr.size());
        if (!deserializeProvisionBundle(yamlStr, outProvision))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_ProtocolError;
        }

        // Pin server certificate in client's trusted store
        if (m_cfg.trustedStore)
        {
            TrustedPeerEntry_t entry;
            entry.deviceId = outProvision.overriderDeviceId;
            entry.hostname = "";
            entry.steamId64 = m_cfg.steamId64;
            entry.certSha256FingerprintHex = peerCertSha256Hex;
            entry.pairedAtUnixSec = time(nullptr);
            m_cfg.trustedStore->addTrustedPeer(entry);
        }

        closeSslSocket(ssl, sock);
        return EP2PError::k_Ok;
    }

    EP2PError P2PSessionClient::syncWithPeer(const std::string& serverIp,
                                             uint16_t serverPort,
                                             const MasterKey_t& masterKey,
                                             const SyncStateManifest_t& /*localManifest*/,
                                             const std::unordered_map<std::string, std::vector<uint8_t>>& /*outgoingBlobs*/,
                                             SyncStateManifest_t& outRemoteManifest,
                                             std::unordered_map<std::string, std::vector<uint8_t>>& outIncomingBlobs)
    {
        if (!m_sslCtx) return EP2PError::k_CryptoError;

        int sock = connectSocket(serverIp, serverPort);
        if (sock < 0) return EP2PError::k_SocketError;

        SSL* ssl = SSL_new(m_sslCtx);
        if (!ssl)
        {
            close(sock);
            return EP2PError::k_CryptoError;
        }
        SSL_set_fd(ssl, sock);

        if (SSL_connect(ssl) <= 0)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_TlsHandshakeFailed;
        }

        const std::string peerCertSha256Hex = extractPeerCertFingerprint(ssl);
        if (peerCertSha256Hex.empty())
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_TlsHandshakeFailed;
        }

        // Verify server certificate is in client's trusted peer store
        if (!m_cfg.trustedStore || !m_cfg.trustedStore->isPeerTrusted(m_cfg.steamId64, "", peerCertSha256Hex))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_UntrustedPeer;
        }

        uint8_t exporterBuf[32];
        CleanseGuard g_exp(exporterBuf, sizeof(exporterBuf));
        if (SSL_export_keying_material(ssl, exporterBuf, sizeof(exporterBuf),
                                       "EXPORTER-navi-p2p-v1", 20,
                                       nullptr, 0, 0) <= 0)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_TlsHandshakeFailed;
        }

        YAML::Emitter helloOut;
        helloOut << YAML::BeginMap;
        helloOut << YAML::Key << "steamid64" << YAML::Value << std::to_string(m_cfg.steamId64);
        helloOut << YAML::Key << "device_id" << YAML::Value << m_cfg.deviceId;
        helloOut << YAML::Key << "hostname" << YAML::Value << m_cfg.hostname;
        helloOut << YAML::EndMap;

        if (!sendFrame(ssl, EP2PFrameType::k_HelloSync, helloOut.c_str()))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_SocketError;
        }

        std::vector<uint8_t> proofData(32 + 8);
        CleanseGuard g_pfd(proofData.data(), proofData.size());
        std::memcpy(proofData.data(), exporterBuf, 32);
        for (int i = 0; i < 8; ++i)
        {
            proofData[32 + i] = static_cast<uint8_t>((m_cfg.steamId64 >> (8 * i)) & 0xFF);
        }

        uint8_t myProof[32];
        CleanseGuard g_prf(myProof, sizeof(myProof));
        computeHmacSha256(masterKey.data(), masterKey.size(), proofData.data(), proofData.size(), myProof);

        if (!sendFrame(ssl, EP2PFrameType::k_ClientKeyProof, std::string_view(reinterpret_cast<const char*>(myProof), 32)))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_SocketError;
        }

        EP2PFrameType keyRespType{};
        std::string serverKeyProof;
        if (!recvFrame(ssl, keyRespType, serverKeyProof))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_Timeout;
        }

        if (keyRespType == EP2PFrameType::k_Error)
        {
            closeSslSocket(ssl, sock);
            if (serverKeyProof == "UNTRUSTED_PEER") return EP2PError::k_UntrustedPeer;
            if (serverKeyProof == "STEAMID_MISMATCH") return EP2PError::k_SteamIdMismatch;
            if (serverKeyProof == "CRYPTO_ERROR") return EP2PError::k_CryptoError;
            return EP2PError::k_ProtocolError;
        }

        if (keyRespType != EP2PFrameType::k_ServerKeyProof || serverKeyProof.size() != 32)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_ProtocolError;
        }

        if (CRYPTO_memcmp(serverKeyProof.data(), myProof, 32) != 0)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_CryptoError;
        }

        EP2PFrameType transferType{};
        std::string transferPayload;
        if (!recvFrame(ssl, transferType, transferPayload) || transferType != EP2PFrameType::k_SyncTransfer)
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_ProtocolError;
        }

        if (!deserializeSyncPayload(transferPayload, outRemoteManifest, outIncomingBlobs))
        {
            closeSslSocket(ssl, sock);
            return EP2PError::k_ProtocolError;
        }

        closeSslSocket(ssl, sock);
        return EP2PError::k_Ok;
    }

} // namespace Sync
