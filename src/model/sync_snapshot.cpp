#include "sync_snapshot.hpp"
#include "atomic_file.hpp"
#include "utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace Sync
{
    static std::string generateSnapshotId(int64_t timestampSec)
    {
        static std::atomic<uint32_t> s_counter{1};
        const uint32_t c = s_counter.fetch_add(1);
        return std::to_string(timestampSec) + "_" + std::to_string(c);
    }

    bool SyncSnapshotManager::createPreSyncBackup(
        const std::string& backupRootDir,
        uint64_t steamId64,
        const std::vector<std::string>& targetAbsPaths,
        SnapshotRecord_t& outRecord)
    {
        const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        outRecord.snapshotId = generateSnapshotId(nowSec);
        outRecord.steamId64 = steamId64;
        outRecord.createdAtUnixSec = nowSec;
        outRecord.backedUpFiles.clear();
        outRecord.nonExistentTargets.clear();

        const fs::path snapshotDir = fs::path(backupRootDir) / std::to_string(steamId64) / outRecord.snapshotId;
        const fs::path filesDir = snapshotDir / "files";

        std::error_code ec;
        fs::create_directories(filesDir, ec);
        if (ec) return false;

        size_t fileIdx = 0;
        for (const auto& targetPathStr : targetAbsPaths)
        {
            const fs::path targetPath(targetPathStr);
            if (fs::exists(targetPath, ec))
            {
                // 1. Sibling .bak copy
                const std::string siblingBak = targetPathStr + ".bak";
                fs::copy_file(targetPath, siblingBak, fs::copy_options::overwrite_existing, ec);
                if (ec) return false;

                // 2. Archive copy in backup directory
                const std::string backupFilename = std::to_string(fileIdx++) + "_" + targetPath.filename().string();
                const fs::path backupCopyPath = filesDir / backupFilename;
                fs::copy_file(targetPath, backupCopyPath, fs::copy_options::overwrite_existing, ec);
                if (ec) return false;

                // 3. Verify SHA-256 integrity
                try
                {
                    const std::string srcSha = Utils::getFileSHA256(targetPathStr.c_str());
                    const std::string bakSha = Utils::getFileSHA256(backupCopyPath.string().c_str());
                    const std::string siblingSha = Utils::getFileSHA256(siblingBak.c_str());
                    if (srcSha.empty() || srcSha != bakSha || srcSha != siblingSha)
                    {
                        return false;
                    }
                }
                catch (...)
                {
                    return false;
                }

                outRecord.backedUpFiles.emplace_back(targetPathStr, backupCopyPath.string());
            }
            else
            {
                // Target did not exist prior to sync
                outRecord.nonExistentTargets.push_back(targetPathStr);
            }
        }

        // 4. Atomically write snapshot.yaml metadata
        YAML::Emitter emitter;
        emitter << YAML::BeginMap;
        emitter << YAML::Key << "snapshot_id" << YAML::Value << outRecord.snapshotId;
        emitter << YAML::Key << "steam_id_64" << YAML::Value << std::to_string(outRecord.steamId64);
        emitter << YAML::Key << "created_at_unix_sec" << YAML::Value << outRecord.createdAtUnixSec;

        emitter << YAML::Key << "backed_up_files" << YAML::Value << YAML::BeginSeq;
        for (const auto& [orig, bak] : outRecord.backedUpFiles)
        {
            emitter << YAML::BeginMap;
            emitter << YAML::Key << "original" << YAML::Value << orig;
            emitter << YAML::Key << "backup" << YAML::Value << bak;
            emitter << YAML::EndMap;
        }
        emitter << YAML::EndSeq;

        emitter << YAML::Key << "non_existent_targets" << YAML::Value << YAML::BeginSeq;
        for (const auto& ne : outRecord.nonExistentTargets)
        {
            emitter << YAML::Value << ne;
        }
        emitter << YAML::EndSeq;

        emitter << YAML::EndMap;

        const fs::path metadataPath = snapshotDir / "snapshot.yaml";
        return AtomicFile::write(metadataPath.string(), emitter.c_str());
    }

    bool SyncSnapshotManager::restoreSnapshot(
        const std::string& backupRootDir,
        uint64_t steamId64,
        const std::string& snapshotId)
    {
        const fs::path snapshotDir = fs::path(backupRootDir) / std::to_string(steamId64) / snapshotId;
        const fs::path metadataPath = snapshotDir / "snapshot.yaml";

        std::error_code ec;
        if (!fs::exists(metadataPath, ec))
            return false;

        YAML::Node root;
        try
        {
            root = YAML::LoadFile(metadataPath.string());
        }
        catch (...)
        {
            return false;
        }

        if (!root.IsMap()) return false;

        // Restore backed up files
        if (root["backed_up_files"] && root["backed_up_files"].IsSequence())
        {
            for (const auto& entry : root["backed_up_files"])
            {
                if (!entry.IsMap() || !entry["original"] || !entry["backup"])
                    continue;

                const std::string original = entry["original"].as<std::string>();
                const std::string backup = entry["backup"].as<std::string>();

                if (!fs::exists(backup, ec))
                    return false;

                std::ifstream srcFile(backup, std::ios::binary);
                if (!srcFile.is_open()) return false;
                std::string data((std::istreambuf_iterator<char>(srcFile)), std::istreambuf_iterator<char>());
                srcFile.close();

                if (!AtomicFile::write(original, data, true))
                    return false;
            }
        }

        // Clean up targets that did not exist before sync
        if (root["non_existent_targets"] && root["non_existent_targets"].IsSequence())
        {
            for (const auto& entry : root["non_existent_targets"])
            {
                const std::string pathToRemove = entry.as<std::string>();
                fs::remove(pathToRemove, ec);
            }
        }

        return true;
    }

    void SyncSnapshotManager::pruneOldSnapshots(
        const std::string& backupRootDir,
        uint64_t steamId64,
        size_t maxRetained)
    {
        const fs::path userDir = fs::path(backupRootDir) / std::to_string(steamId64);
        std::error_code ec;
        if (!fs::exists(userDir, ec) || !fs::is_directory(userDir, ec))
            return;

        struct SnapshotInfo
        {
            int64_t timestamp = 0;
            fs::path dir;
        };

        std::vector<SnapshotInfo> snapshots;

        fs::directory_iterator it(userDir, ec);
        fs::directory_iterator end;
        while (!ec && it != end)
        {
            if (it->is_directory(ec))
            {
                const fs::path meta = it->path() / "snapshot.yaml";
                int64_t ts = 0;
                if (fs::exists(meta, ec))
                {
                    try
                    {
                        YAML::Node node = YAML::LoadFile(meta.string());
                        if (node["created_at_unix_sec"])
                            ts = node["created_at_unix_sec"].as<int64_t>();
                    }
                    catch (...) {}
                }

                if (ts == 0)
                {
                    // Fallback to directory last write time
                    auto lwt = fs::last_write_time(it->path(), ec);
                    ts = std::chrono::duration_cast<std::chrono::seconds>(
                        lwt.time_since_epoch()).count();
                }

                snapshots.push_back({ ts, it->path() });
            }
            it.increment(ec);
        }

        if (snapshots.size() <= maxRetained)
            return;

        // Sort descending: newest first; tie-break on directory filename/snapshotId descending
        std::sort(snapshots.begin(), snapshots.end(), [](const SnapshotInfo& a, const SnapshotInfo& b)
        {
            if (a.timestamp != b.timestamp)
                return a.timestamp > b.timestamp;
            return a.dir.filename().string() > b.dir.filename().string();
        });

        for (size_t i = maxRetained; i < snapshots.size(); ++i)
        {
            fs::remove_all(snapshots[i].dir, ec);
        }
    }
} // namespace Sync
