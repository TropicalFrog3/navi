#include "sync_engine.hpp"

#include "atomic_file.hpp"
#include "config.hpp"
#include "paths.hpp"
#include "sync_snapshot.hpp"
#include "utils.hpp"
#include "vdf.hpp"

#include "yaml-cpp/yaml.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace Sync
{
    bool doesAcfRequireStagedShutdownCommit(
        const std::string& existingAcfContent,
        const std::string& incomingAcfContent)
    {
        if (existingAcfContent.empty() || incomingAcfContent.empty())
            return false;

        VDF::Node exRoot;
        VDF::Node inRoot;
        if (!VDF::parse(existingAcfContent, exRoot) || !VDF::parse(incomingAcfContent, inRoot))
        {
            return false;
        }

        const VDF::Node* exAppState = (exRoot.key == "AppState") ? &exRoot : exRoot.getChild("AppState");
        const VDF::Node* inAppState = (inRoot.key == "AppState") ? &inRoot : inRoot.getChild("AppState");
        if (!exAppState) exAppState = &exRoot;
        if (!inAppState) inAppState = &inRoot;

        // 1. Compare buildid
        const std::string exBuildId = exAppState->getString("buildid");
        const std::string inBuildId = inAppState->getString("buildid");
        if (!exBuildId.empty() && !inBuildId.empty() && exBuildId != inBuildId)
        {
            return true;
        }

        // 2. Compare InstalledDepots manifest IDs
        const VDF::Node* exDepots = exAppState->getChild("InstalledDepots");
        const VDF::Node* inDepots = inAppState->getChild("InstalledDepots");
        if (exDepots && inDepots)
        {
            std::unordered_map<std::string, std::string> exManifests;
            for (const auto& child : exDepots->children)
            {
                std::string manifest = child.getString("manifest");
                if (!manifest.empty()) exManifests[child.key] = manifest;
            }

            std::unordered_map<std::string, std::string> inManifests;
            for (const auto& child : inDepots->children)
            {
                std::string manifest = child.getString("manifest");
                if (!manifest.empty()) inManifests[child.key] = manifest;
            }

            if (exManifests != inManifests)
            {
                return true;
            }
        }
        else if ((exDepots != nullptr) != (inDepots != nullptr))
        {
            return true;
        }

        return false;
    }

    bool commitStagedAcfFilesOnShutdown(
        const std::string& configDir,
        const std::string& steamRootOverride)
    {
        const fs::path pendingDir = fs::path(configDir) / "pending_acf";
        std::error_code ec;
        if (!fs::exists(pendingDir, ec) || !fs::is_directory(pendingDir, ec))
        {
            return true;
        }

        bool allSuccess = true;
        for (const auto& entry : fs::directory_iterator(pendingDir, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            const std::string filename = entry.path().filename().string();
            uint32_t appId = 0;
            if (filename.rfind("appmanifest_", 0) == 0 && filename.ends_with(".acf"))
            {
                const std::string idStr = filename.substr(12, filename.size() - 16);
                try { appId = std::stoul(idStr); } catch (...) {}
            }
            if (appId == 0) continue;

            std::string libFolder = Paths::findAppLibraryFolder(appId, steamRootOverride);
            if (libFolder.empty())
            {
                libFolder = !steamRootOverride.empty() ? steamRootOverride : Paths::findSteamRoot();
            }
            if (libFolder.empty())
            {
                allSuccess = false;
                continue;
            }

            const fs::path destAcf = fs::path(libFolder) / "steamapps" / filename;
            fs::create_directories(destAcf.parent_path(), ec);

            std::ifstream src(entry.path(), std::ios::binary);
            if (!src.is_open())
            {
                allSuccess = false;
                continue;
            }
            std::string content((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
            src.close();

            if (AtomicFile::write(destAcf.string(), content, false))
            {
                fs::remove(entry.path(), ec);
            }
            else
            {
                allSuccess = false;
            }
        }

        return allSuccess;
    }

    SyncEngine::SyncEngine(
        std::string configDir,
        std::string steamRootOverride,
        std::string procRootOverride)
        : configDir_(std::move(configDir))
        , steamRootOverride_(std::move(steamRootOverride))
        , procRootOverride_(std::move(procRootOverride))
    {
    }

    bool SyncEngine::isAppDeferred(uint32_t appId) const
    {
        return deferredQueue_.contains(appId);
    }

    std::string SyncEngine::resolveDestPath(const SyncFileEntry_t& entry, uint64_t steamId64) const
    {
        std::string p = Paths::resolveLogicalSyncPath(entry.logicalPath, steamId64, entry.appId, steamRootOverride_);
        if (!p.empty()) return p;

        const std::string steamRoot = !steamRootOverride_.empty() ? steamRootOverride_ : Paths::findSteamRoot();
        const uint32_t steam32 = static_cast<uint32_t>(steamId64 & 0xFFFFFFFFULL);

        if (entry.logicalPath.starts_with("achievements://"))
        {
            if (steamRoot.empty()) return "";
            std::string rest = entry.logicalPath.substr(15);
            size_t slash = rest.find('/');
            std::string sub = (slash != std::string::npos) ? rest.substr(slash + 1) : rest;
            return (fs::path(steamRoot) / "userdata" / std::to_string(steam32) / std::to_string(entry.appId) / sub).lexically_normal().string();
        }
        if (entry.logicalPath.starts_with("config://"))
        {
            std::string rest = entry.logicalPath.substr(9);
            return (fs::path(configDir_) / rest).lexically_normal().string();
        }
        return "";
    }

    void SyncEngine::loadBaseState(uint64_t steamId64)
    {
        const fs::path basePath = fs::path(configDir_) / ("sync_base_state_" + std::to_string(steamId64) + ".yaml");
        std::error_code ec;
        if (!fs::exists(basePath, ec)) return;

        try
        {
            YAML::Node doc = YAML::LoadFile(basePath.string());
            if (doc["lastSyncedSha256"] && doc["lastSyncedSha256"].IsMap())
            {
                for (const auto& item : doc["lastSyncedSha256"])
                {
                    lastSyncedSha256_[item.first.as<std::string>()] = item.second.as<std::string>();
                }
            }
        }
        catch (...) {}
    }

    void SyncEngine::saveBaseState(uint64_t steamId64)
    {
        const fs::path basePath = fs::path(configDir_) / ("sync_base_state_" + std::to_string(steamId64) + ".yaml");
        std::error_code ec;
        fs::create_directories(basePath.parent_path(), ec);

        YAML::Node doc;
        doc["schemaVersion"] = 1;
        doc["steamId64"] = steamId64;
        YAML::Node hashesNode(YAML::NodeType::Map);
        for (const auto& [k, v] : lastSyncedSha256_)
        {
            hashesNode[k] = v;
        }
        doc["lastSyncedSha256"] = hashesNode;

        std::stringstream ss;
        ss << doc;
        AtomicFile::write(basePath.string(), ss.str(), false);
    }

    ESyncApplyStatus SyncEngine::applyRemoteSyncPackage(
        uint64_t localSteamId64,
        const SyncStateManifest_t& remoteManifest,
        const std::unordered_map<std::string, std::vector<uint8_t>>& plaintextPayloadsByLogicalPath,
        const SyncSelectionFilter_t& filter,
        const std::unordered_map<std::string, EConflictChoice>& userConflictChoices,
        SyncApplyReport_t& outReport)
    {
        outReport = SyncApplyReport_t{};

        // 1. Hard Account Scope Guard
        if (localSteamId64 == 0 || remoteManifest.steamId64 != localSteamId64)
        {
            outReport.status = ESyncApplyStatus::k_SteamIdMismatch;
            return ESyncApplyStatus::k_SteamIdMismatch;
        }

        loadBaseState(localSteamId64);

        // 2. Filter entries
        std::vector<SyncFileEntry_t> selectedEntries;
        for (const auto& entry : remoteManifest.entries)
        {
            bool selected = false;
            switch (entry.category)
            {
                case ESyncCategory::k_ManifestsAndLua:
                    selected = filter.isAppSelected(entry.category, entry.appId);
                    break;
                case ESyncCategory::k_CloudSaves:
                    selected = filter.isAppSelected(entry.category, entry.appId);
                    break;
                case ESyncCategory::k_Achievements:
                    selected = filter.isAppSelected(entry.category, entry.appId);
                    break;
                case ESyncCategory::k_Configs:
                    selected = filter.isConfigSelected(entry.logicalPath);
                    break;
                case ESyncCategory::k_ApiKeys:
                    selected = filter.isProviderSelected(entry.logicalPath);
                    break;
            }
            if (selected)
            {
                selectedEntries.push_back(entry);
            }
        }

        // 3. Game Process Lock Check
        std::unordered_set<uint32_t> runningApps;
        std::unordered_set<uint32_t> checkedApps;

        for (const auto& entry : selectedEntries)
        {
            if (entry.appId != 0 && !checkedApps.contains(entry.appId))
            {
                checkedApps.insert(entry.appId);
                if (GameProcessLock::isAppRunning(entry.appId, procRootOverride_, steamRootOverride_))
                {
                    runningApps.insert(entry.appId);
                    outReport.deferredAppIds.push_back(entry.appId);
                }
            }
        }

        struct PreparedWrite_t
        {
            SyncFileEntry_t entry;
            std::string destPath;
            std::vector<uint8_t> payload;
            bool isStagedAcf = false;
            std::string stagedPath;
        };

        std::vector<PreparedWrite_t> writesToExecute;
        std::vector<std::string> targetsToSnapshot;

        for (const auto& entry : selectedEntries)
        {
            auto itPayload = plaintextPayloadsByLogicalPath.find(entry.logicalPath);
            std::vector<uint8_t> remotePayload;
            if (itPayload != plaintextPayloadsByLogicalPath.end())
            {
                remotePayload = itPayload->second;
            }

            // Defer if game is running
            if (entry.appId != 0 && runningApps.contains(entry.appId))
            {
                deferredQueue_[entry.appId].push_back({entry, std::move(remotePayload)});
                continue;
            }

            const std::string destPath = resolveDestPath(entry, localSteamId64);

            // 4. Save Conflict Detection (k_CloudSaves)
            if (entry.category == ESyncCategory::k_CloudSaves)
            {
                std::error_code ec;
                if (!destPath.empty() && fs::exists(destPath, ec))
                {
                    const std::string localSha = Utils::getFileSHA256(destPath.c_str());
                    if (localSha == entry.sha256Hex)
                    {
                        lastSyncedSha256_[entry.logicalPath] = localSha;
                        continue;
                    }

                    std::string baseSha;
                    auto itBase = lastSyncedSha256_.find(entry.logicalPath);
                    if (itBase != lastSyncedSha256_.end())
                    {
                        baseSha = itBase->second;
                    }

                    if (localSha != baseSha)
                    {
                        auto itChoice = userConflictChoices.find(entry.logicalPath);
                        EConflictChoice choice = (itChoice != userConflictChoices.end())
                            ? itChoice->second
                            : EConflictChoice::k_PendingUserChoice;

                        if (choice == EConflictChoice::k_PendingUserChoice)
                        {
                            SaveConflict_t sc;
                            sc.appId = entry.appId;
                            sc.logicalPath = entry.logicalPath;
                            sc.localSha256 = localSha;
                            struct stat st;
                            if (stat(destPath.c_str(), &st) == 0)
                            {
                                sc.localMtimeUnixSec = st.st_mtime;
                                sc.localSizeBytes = static_cast<uint64_t>(st.st_size);
                            }
                            sc.remoteSha256 = entry.sha256Hex;
                            sc.remoteMtimeUnixSec = entry.mtimeUnixSec;
                            sc.remoteSizeBytes = entry.sizeBytes;
                            outReport.conflicts.push_back(sc);
                            continue;
                        }
                        else if (choice == EConflictChoice::k_KeepLocal)
                        {
                            lastSyncedSha256_[entry.logicalPath] = entry.sha256Hex;
                            continue;
                        }
                        else if (choice == EConflictChoice::k_BackupLocalAndUseRemote)
                        {
                            const fs::path conflictBakDir = fs::path(configDir_) / "sync_backups" / std::to_string(localSteamId64) / "conflicts";
                            fs::create_directories(conflictBakDir, ec);
                            const fs::path fn = fs::path(destPath).filename();
                            const fs::path conflictBakFile = conflictBakDir / (std::to_string(entry.appId) + "_" + fn.string() + ".conflict_bak");
                            if (!AtomicFile::copy(destPath, conflictBakFile.string()))
                            {
                                outReport.status = ESyncApplyStatus::k_BackupFailed;
                                return ESyncApplyStatus::k_BackupFailed;
                            }
                        }
                    }
                }
            }

            // Check if ACF needs staging
            bool isStagedAcf = false;
            std::string stagedPath;
            if (entry.category == ESyncCategory::k_ManifestsAndLua &&
                entry.logicalPath.starts_with("steamapps://appmanifest_"))
            {
                std::error_code ec;
                if (!destPath.empty() && fs::exists(destPath, ec))
                {
                    std::ifstream curFile(destPath, std::ios::binary);
                    std::string curContent((std::istreambuf_iterator<char>(curFile)), std::istreambuf_iterator<char>());
                    curFile.close();

                    std::string inContent(remotePayload.begin(), remotePayload.end());
                    if (doesAcfRequireStagedShutdownCommit(curContent, inContent))
                    {
                        isStagedAcf = true;
                        stagedPath = (fs::path(configDir_) / "pending_acf" / ("appmanifest_" + std::to_string(entry.appId) + ".acf")).string();
                        outReport.stagedAcfRestartRequiredAppIds.push_back(entry.appId);
                    }
                }
            }

            PreparedWrite_t pw;
            pw.entry = entry;
            pw.destPath = destPath;
            pw.payload = std::move(remotePayload);
            pw.isStagedAcf = isStagedAcf;
            pw.stagedPath = stagedPath;

            if (isStagedAcf)
            {
                targetsToSnapshot.push_back(stagedPath);
            }
            else if (!destPath.empty())
            {
                targetsToSnapshot.push_back(destPath);
            }

            writesToExecute.push_back(std::move(pw));
        }

        // 5. Pre-Sync Snapshot Backup
        if (!targetsToSnapshot.empty())
        {
            SnapshotRecord_t snapRecord;
            bool backedUp = SyncSnapshotManager::createPreSyncBackup(
                (fs::path(configDir_) / "sync_backups").string(),
                localSteamId64,
                targetsToSnapshot,
                snapRecord);

            if (!backedUp)
            {
                outReport.status = ESyncApplyStatus::k_BackupFailed;
                return ESyncApplyStatus::k_BackupFailed;
            }
            outReport.snapshotId = snapRecord.snapshotId;
        }

        // 6. Execute Writes
        const auto preWriteLastSyncedSha256 = lastSyncedSha256_;

        if (simulateWriteFailure_)
        {
            if (!outReport.snapshotId.empty())
            {
                SyncSnapshotManager::restoreSnapshot(
                    (fs::path(configDir_) / "sync_backups").string(),
                    localSteamId64,
                    outReport.snapshotId);
            }
            lastSyncedSha256_ = preWriteLastSyncedSha256;
            outReport.status = ESyncApplyStatus::k_IoError;
            return ESyncApplyStatus::k_IoError;
        }

        bool writeFailed = false;
        for (const auto& pw : writesToExecute)
        {
            const auto& entry = pw.entry;
            const auto& payload = pw.payload;

            if (pw.isStagedAcf)
            {
                std::string_view sv(reinterpret_cast<const char*>(payload.data()), payload.size());
                if (!AtomicFile::write(pw.stagedPath, sv, false))
                {
                    writeFailed = true;
                    break;
                }
                outReport.appliedLogicalPaths.push_back(entry.logicalPath);
                lastSyncedSha256_[entry.logicalPath] = entry.sha256Hex;
                continue;
            }

            if (entry.category == ESyncCategory::k_Achievements)
            {
                std::error_code ec;
                std::vector<uint8_t> bytesToWrite = payload;
                if (!pw.destPath.empty() && fs::exists(pw.destPath, ec))
                {
                    std::ifstream curFile(pw.destPath, std::ios::binary);
                    std::vector<uint8_t> localBytes((std::istreambuf_iterator<char>(curFile)), std::istreambuf_iterator<char>());
                    curFile.close();

                    std::vector<uint8_t> merged;
                    if (mergeAchievementBuffersUnion(localBytes, payload, merged))
                    {
                        bytesToWrite = std::move(merged);
                    }
                }

                std::string_view sv(reinterpret_cast<const char*>(bytesToWrite.data()), bytesToWrite.size());
                if (!AtomicFile::write(pw.destPath, sv, true))
                {
                    writeFailed = true;
                    break;
                }
                outReport.appliedLogicalPaths.push_back(entry.logicalPath);
                lastSyncedSha256_[entry.logicalPath] = entry.sha256Hex;
            }
            else if (entry.category == ESyncCategory::k_ManifestsAndLua ||
                     entry.category == ESyncCategory::k_CloudSaves)
            {
                std::string_view sv(reinterpret_cast<const char*>(payload.data()), payload.size());
                if (!AtomicFile::write(pw.destPath, sv, true))
                {
                    writeFailed = true;
                    break;
                }
                outReport.appliedLogicalPaths.push_back(entry.logicalPath);
                lastSyncedSha256_[entry.logicalPath] = entry.sha256Hex;
            }
            else if (entry.category == ESyncCategory::k_Configs)
            {
                fs::path cfgPath = fs::path(configDir_) / "config.yaml";
                YAML::Node existingCfg;
                std::error_code ec;
                if (fs::exists(cfgPath, ec))
                {
                    try { existingCfg = YAML::LoadFile(cfgPath.string()); } catch (...) { existingCfg = YAML::Node(YAML::NodeType::Map); }
                }
                else
                {
                    existingCfg = YAML::Node(YAML::NodeType::Map);
                }
                std::string remoteYaml(payload.begin(), payload.end());
                try {
                    YAML::Node remoteNode = YAML::Load(remoteYaml);
                    if (remoteNode.IsMap())
                    {
                        for (const auto& item : remoteNode)
                        {
                            existingCfg[item.first.as<std::string>()] = item.second;
                        }
                    }
                } catch (...) {}
                std::stringstream ss;
                ss << existingCfg;
                if (!AtomicFile::write(cfgPath.string(), ss.str(), false))
                {
                    writeFailed = true;
                    break;
                }
                g_config.loadSettings(false);
                outReport.appliedLogicalPaths.push_back(entry.logicalPath);
                lastSyncedSha256_[entry.logicalPath] = entry.sha256Hex;
            }
            else if (entry.category == ESyncCategory::k_ApiKeys)
            {
                std::string keyVal(payload.begin(), payload.end());
                while (!keyVal.empty() && (keyVal.back() == '\n' || keyVal.back() == '\r')) keyVal.pop_back();

                if (entry.logicalPath.find("ryuu") != std::string::npos)
                {
                    g_config.ryuuKey = keyVal;
                }
                else if (entry.logicalPath.find("depotbox") != std::string::npos || entry.logicalPath.find("depotBox") != std::string::npos)
                {
                    g_config.depotBoxKey = keyVal;
                }
                else if (entry.logicalPath.find("hubcap") != std::string::npos)
                {
                    g_config.hubcapKey = keyVal;
                }
                outReport.appliedLogicalPaths.push_back(entry.logicalPath);
                lastSyncedSha256_[entry.logicalPath] = entry.sha256Hex;
            }
        }

        if (writeFailed)
        {
            if (!outReport.snapshotId.empty())
            {
                SyncSnapshotManager::restoreSnapshot(
                    (fs::path(configDir_) / "sync_backups").string(),
                    localSteamId64,
                    outReport.snapshotId);
            }
            lastSyncedSha256_ = preWriteLastSyncedSha256;
            outReport.status = ESyncApplyStatus::k_IoError;
            return ESyncApplyStatus::k_IoError;
        }

        saveBaseState(localSteamId64);

        const bool hasDeferred = !outReport.deferredAppIds.empty();
        const bool hasConflicts = !outReport.conflicts.empty();
        const bool hasApplied = !outReport.appliedLogicalPaths.empty();

        if (hasConflicts && !hasApplied)
        {
            outReport.status = ESyncApplyStatus::k_ConflictPendingUserChoice;
        }
        else if (hasDeferred && !hasApplied && !hasConflicts)
        {
            outReport.status = ESyncApplyStatus::k_DeferredGameRunning;
        }
        else if ((hasDeferred || hasConflicts) && hasApplied)
        {
            outReport.status = ESyncApplyStatus::k_PartialDeferredOrConflict;
        }
        else
        {
            outReport.status = ESyncApplyStatus::k_Ok;
        }

        return outReport.status;
    }

    SyncApplyReport_t SyncEngine::drainDeferredGamesIfExited(
        uint64_t localSteamId64,
        const std::unordered_map<std::string, EConflictChoice>& userConflictChoices)
    {
        SyncApplyReport_t totalReport;
        totalReport.status = ESyncApplyStatus::k_Ok;

        if (deferredQueue_.empty())
        {
            return totalReport;
        }

        std::vector<uint32_t> appsToDrain;
        for (const auto& [appId, _] : deferredQueue_)
        {
            if (!GameProcessLock::isAppRunning(appId, procRootOverride_, steamRootOverride_))
            {
                appsToDrain.push_back(appId);
            }
        }

        if (appsToDrain.empty())
        {
            totalReport.status = ESyncApplyStatus::k_DeferredGameRunning;
            for (const auto& [appId, _] : deferredQueue_)
            {
                totalReport.deferredAppIds.push_back(appId);
            }
            return totalReport;
        }

        SyncStateManifest_t drainManifest;
        drainManifest.steamId64 = localSteamId64;
        std::unordered_map<std::string, std::vector<uint8_t>> drainPayloads;

        SyncSelectionFilter_t filter;
        filter.allManifestsAndLua = true;
        filter.allCloudSaves = true;
        filter.allAchievements = true;
        filter.allConfigs = true;
        filter.allApiKeys = true;

        for (uint32_t appId : appsToDrain)
        {
            auto it = deferredQueue_.find(appId);
            if (it != deferredQueue_.end())
            {
                for (const auto& [entry, payload] : it->second)
                {
                    drainManifest.entries.push_back(entry);
                    drainPayloads[entry.logicalPath] = payload;
                }
                deferredQueue_.erase(it);
            }
        }

        applyRemoteSyncPackage(
            localSteamId64,
            drainManifest,
            drainPayloads,
            filter,
            userConflictChoices,
            totalReport);

        return totalReport;
    }
} // namespace Sync
