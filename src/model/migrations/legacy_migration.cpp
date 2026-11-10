/**
 * @file  legacy_migration.cpp
 * @brief Backward-compatible parser for legacy pre-v1.0 config syntax
 */
#include "legacy_migration.hpp"
#include "atomic_file.hpp"
#include "log.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{

void cleanStaging(const std::string& stagingPath)
{
    if (stagingPath.empty()) return;
    std::error_code ec;
    if (!fs::exists(stagingPath, ec)) return;

    // Restore owner permissions so removal does not fail if read-only files were placed in staging
    fs::permissions(stagingPath, fs::perms::owner_all, fs::perm_options::add, ec);
    for (const auto& entry : fs::recursive_directory_iterator(stagingPath, fs::directory_options::skip_permission_denied, ec))
    {
        if (ec) break;
        fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ec);
    }
    ec.clear();
    fs::remove_all(stagingPath, ec);
}

bool isWritableOrCreatable(const std::string& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    if (fs::exists(path, ec))
    {
        return access(path.c_str(), W_OK) == 0;
    }

    fs::path p(path);
    while (!p.empty() && !fs::exists(p, ec))
    {
        p = p.parent_path();
    }
    if (!p.empty() && fs::exists(p, ec))
    {
        return access(p.c_str(), W_OK) == 0;
    }
    return true;
}

bool migrateDirectoryAtomic(const std::string& srcDir, const std::string& dstDir, bool isConfigDir)
{
    if (srcDir.empty() || dstDir.empty()) return true;

    std::error_code ec;
    if (!fs::exists(srcDir, ec) || !fs::is_directory(srcDir, ec))
    {
        return true;
    }

    // Clean up stale staging directories from any previous aborted migration
    cleanStaging(dstDir + ".staging");
    cleanStaging(dstDir + "/.staging");

    if (!isWritableOrCreatable(dstDir))
    {
        LOG_WARN("LegacyMigration: Destination '%s' is unwritable; skipping migration.\n", dstDir.c_str());
        return false;
    }

    const std::string stagingDir = dstDir + ".staging";

    try
    {
        if (!fs::exists(dstDir, ec))
        {
            fs::create_directories(stagingDir, ec);
            if (ec)
            {
                LOG_WARN("LegacyMigration: Failed to create staging directory '%s': %s\n", stagingDir.c_str(), ec.message().c_str());
                return false;
            }

            for (const auto& entry : fs::recursive_directory_iterator(srcDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                const auto rel = fs::relative(entry.path(), srcDir, ec);
                if (ec) continue;

                const fs::path staged = fs::path(stagingDir) / rel;

                if (entry.is_directory(ec))
                {
                    fs::create_directories(staged, ec);
                }
                else if (entry.is_regular_file(ec) || entry.is_symlink(ec))
                {
                    fs::create_directories(staged.parent_path(), ec);
                    fs::copy_file(entry.path(), staged, fs::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        LOG_WARN("LegacyMigration: Failed to stage file '%s': %s\n", entry.path().c_str(), ec.message().c_str());
                        cleanStaging(stagingDir);
                        return false;
                    }

                    // Verify copy size
                    const auto szSrc = fs::file_size(entry.path(), ec);
                    const auto szStaged = fs::file_size(staged, ec);
                    if (szSrc != szStaged)
                    {
                        LOG_WARN("LegacyMigration: Staged file '%s' size mismatch (%llu != %llu)\n",
                                 staged.c_str(),
                                 static_cast<unsigned long long>(szStaged),
                                 static_cast<unsigned long long>(szSrc));
                        cleanStaging(stagingDir);
                        return false;
                    }

                    // Preserve original permissions
                    const auto perms = fs::status(entry.path(), ec).permissions();
                    fs::permissions(staged, perms, ec);
                }
            }

            // Move completed staging directory into dstDir
            fs::rename(stagingDir, dstDir, ec);
            if (ec)
            {
                ec.clear();
                if (!AtomicFile::moveOrMergeDirectory(stagingDir, dstDir))
                {
                    LOG_WARN("LegacyMigration: Failed to move staging directory '%s' to '%s'\n", stagingDir.c_str(), dstDir.c_str());
                    cleanStaging(stagingDir);
                    return false;
                }
            }
            cleanStaging(stagingDir);
        }
        else
        {
            // Destination directory already exists
            fs::create_directories(stagingDir, ec);
            if (ec)
            {
                LOG_WARN("LegacyMigration: Failed to create staging directory '%s': %s\n", stagingDir.c_str(), ec.message().c_str());
                return false;
            }

            // Check if both old and new config.yaml exist
            if (isConfigDir)
            {
                const fs::path oldConfig = fs::path(srcDir) / "config.yaml";
                const fs::path newConfig = fs::path(dstDir) / "config.yaml";
                if (fs::exists(oldConfig, ec) && fs::exists(newConfig, ec))
                {
                    LOG_WARN("LegacyMigration: Both old (%s) and new (%s) configs exist; preserving new config (new wins).\n",
                             oldConfig.c_str(), newConfig.c_str());
                }
            }

            for (const auto& entry : fs::recursive_directory_iterator(srcDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                const auto rel = fs::relative(entry.path(), srcDir, ec);
                if (ec) continue;

                const fs::path targetItem = fs::path(dstDir) / rel;

                if (entry.is_directory(ec))
                {
                    continue;
                }
                else if (entry.is_regular_file(ec) || entry.is_symlink(ec))
                {
                    // If target already exists, existing file wins! Do not overwrite.
                    if (fs::exists(targetItem, ec))
                    {
                        continue;
                    }

                    const fs::path staged = fs::path(stagingDir) / rel;
                    fs::create_directories(staged.parent_path(), ec);
                    fs::copy_file(entry.path(), staged, fs::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        LOG_WARN("LegacyMigration: Failed to stage file '%s': %s\n", entry.path().c_str(), ec.message().c_str());
                        cleanStaging(stagingDir);
                        return false;
                    }

                    // Verify copy size
                    const auto szSrc = fs::file_size(entry.path(), ec);
                    const auto szStaged = fs::file_size(staged, ec);
                    if (szSrc != szStaged)
                    {
                        LOG_WARN("LegacyMigration: Staged file '%s' size mismatch (%llu != %llu)\n",
                                 staged.c_str(),
                                 static_cast<unsigned long long>(szStaged),
                                 static_cast<unsigned long long>(szSrc));
                        cleanStaging(stagingDir);
                        return false;
                    }

                    const auto perms = fs::status(entry.path(), ec).permissions();
                    fs::permissions(staged, perms, ec);
                }
            }

            // Move staged files into target directory
            for (const auto& entry : fs::recursive_directory_iterator(stagingDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (entry.is_regular_file(ec) || entry.is_symlink(ec))
                {
                    const auto rel = fs::relative(entry.path(), stagingDir, ec);
                    const fs::path finalDest = fs::path(dstDir) / rel;
                    fs::create_directories(finalDest.parent_path(), ec);

                    const auto perms = fs::status(entry.path(), ec).permissions();
                    fs::rename(entry.path(), finalDest, ec);
                    if (ec)
                    {
                        ec.clear();
                        fs::copy_file(entry.path(), finalDest, fs::copy_options::overwrite_existing, ec);
                        fs::remove(entry.path(), ec);
                    }
                    fs::permissions(finalDest, perms, ec);
                }
            }
            cleanStaging(stagingDir);
        }
    }
    catch (const std::exception& ex)
    {
        LOG_WARN("LegacyMigration: Exception during directory migration from '%s' to '%s': %s\n",
                 srcDir.c_str(), dstDir.c_str(), ex.what());
        cleanStaging(stagingDir);
        return false;
    }

    return true;
}

} // anonymous namespace

LegacyMigration::Paths LegacyMigration::buildPaths(const std::string& homeDir)
{
    Paths p;
    p.legacyConfigDir = homeDir + "/.config/SLSsteam";
    p.targetConfigDir = homeDir + "/.config/navi";
    p.legacyCacheDir = homeDir + "/.local/share/SLSsteam/cache";
    p.targetCacheDir = homeDir + "/.local/share/navi/cache";
    p.targetShareDir = homeDir + "/.local/share/navi";
    p.legacyManifestsDir = homeDir + "/.local/share/ACCELA/morrenus_manifests";
    p.targetManifestsDir = homeDir + "/.local/share/navi/cache/manifests";
    p.targetManifestsAltDir = homeDir + "/.local/share/navi/manifests";
    return p;
}

bool LegacyMigration::migrate(const std::string& customHome)
{
    return migrate(buildPaths(customHome));
}

bool LegacyMigration::migrate(const Paths& paths)
{
    std::error_code ec;

    const bool hasLegacyConfig = !paths.legacyConfigDir.empty() && fs::exists(paths.legacyConfigDir, ec);
    const bool hasLegacyCache = !paths.legacyCacheDir.empty() && fs::exists(paths.legacyCacheDir, ec);
    const bool hasLegacyManifests = !paths.legacyManifestsDir.empty() && fs::exists(paths.legacyManifestsDir, ec);

    // If no legacy paths exist, there is nothing to migrate
    if (!hasLegacyConfig && !hasLegacyCache && !hasLegacyManifests)
    {
        return true;
    }

    const std::string configMarker = paths.targetConfigDir + "/" + MARKER_FILE;
    const std::string shareMarker = paths.targetShareDir + "/" + MARKER_FILE;

    const bool configMarkerExists = !paths.targetConfigDir.empty() && fs::exists(configMarker, ec);
    const bool shareMarkerExists = !paths.targetShareDir.empty() && fs::exists(shareMarker, ec);

    // Idempotent fast-path: both markers present
    if (configMarkerExists && shareMarkerExists)
    {
        return true;
    }

    bool allSuccess = true;

    // 1. Config Migration
    if (hasLegacyConfig && !configMarkerExists)
    {
        if (migrateDirectoryAtomic(paths.legacyConfigDir, paths.targetConfigDir, true))
        {
            if (fs::exists(paths.targetConfigDir, ec))
            {
                if (!AtomicFile::write(configMarker, "migrated_from_slssteam\n"))
                {
                    LOG_WARN("LegacyMigration: Failed to write marker file '%s'\n", configMarker.c_str());
                }
            }
        }
        else
        {
            allSuccess = false;
        }
    }

    // 2. Cache and Manifests Migration
    if ((hasLegacyCache || hasLegacyManifests || hasLegacyConfig) && !shareMarkerExists)
    {
        if (hasLegacyCache)
        {
            if (!migrateDirectoryAtomic(paths.legacyCacheDir, paths.targetCacheDir, false))
            {
                allSuccess = false;
            }
        }

        if (hasLegacyManifests)
        {
            if (!migrateDirectoryAtomic(paths.legacyManifestsDir, paths.targetManifestsDir, false))
            {
                allSuccess = false;
            }
            if (!paths.targetManifestsAltDir.empty())
            {
                migrateDirectoryAtomic(paths.legacyManifestsDir, paths.targetManifestsAltDir, false);
            }
        }

        if (allSuccess && !paths.targetShareDir.empty())
        {
            fs::create_directories(paths.targetShareDir, ec);
            if (!AtomicFile::write(shareMarker, "migrated_from_slssteam\n"))
            {
                LOG_WARN("LegacyMigration: Failed to write marker file '%s'\n", shareMarker.c_str());
            }
        }
    }

    return allSuccess;
}

bool LegacyMigration::migrateIfNeeded()
{
    const char* home = std::getenv("HOME");
    if (!home) return false;

    // Standard XDG paths
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    const std::string configBase = xdgConfig ? std::string(xdgConfig) : (std::string(home) + "/.config");
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    const std::string dataBase = xdgData ? std::string(xdgData) : (std::string(home) + "/.local/share");

    Paths stdPaths;
    stdPaths.legacyConfigDir = configBase + "/SLSsteam";
    stdPaths.targetConfigDir = configBase + "/navi";
    stdPaths.legacyCacheDir = dataBase + "/SLSsteam/cache";
    stdPaths.targetCacheDir = dataBase + "/navi/cache";
    stdPaths.targetShareDir = dataBase + "/navi";
    stdPaths.legacyManifestsDir = dataBase + "/ACCELA/morrenus_manifests";
    stdPaths.targetManifestsDir = dataBase + "/navi/cache/manifests";
    stdPaths.targetManifestsAltDir = dataBase + "/navi/manifests";

    const bool stdOk = migrate(stdPaths);

    // Flatpak Steam paths if present
    const std::string flatpakBase = std::string(home) + "/.var/app/com.valvesoftware.Steam";
    std::error_code ec;
    if (fs::exists(flatpakBase, ec))
    {
        Paths flatpakPaths;
        flatpakPaths.legacyConfigDir = flatpakBase + "/.config/SLSsteam";
        flatpakPaths.targetConfigDir = flatpakBase + "/.config/navi";
        flatpakPaths.legacyCacheDir = flatpakBase + "/.local/share/SLSsteam/cache";
        flatpakPaths.targetCacheDir = flatpakBase + "/.local/share/navi/cache";
        flatpakPaths.targetShareDir = flatpakBase + "/.local/share/navi";
        flatpakPaths.legacyManifestsDir = flatpakBase + "/.local/share/ACCELA/morrenus_manifests";
        flatpakPaths.targetManifestsDir = flatpakBase + "/.local/share/navi/cache/manifests";
        flatpakPaths.targetManifestsAltDir = flatpakBase + "/.local/share/navi/manifests";

        migrate(flatpakPaths);
    }

    return stdOk;
}
