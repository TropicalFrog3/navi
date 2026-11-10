/**
 * @file  prefix_bucket_sync.hpp
 * @brief Declarations for prefix save bucket scanning and remote push/pull
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//PrefixBucketSync bridges emulator-managed remote storage ("Goldberg"/GSE style
//save buckets living inside a Proton prefix) with CloudRedirect's provider
//backend so those saves sync across devices through the same Google
//Drive/OneDrive account the engine already uses.
//
//The engine only captures files that flow through its own hooked UFS pipeline;
//games that run cloud I/O inside an emulator (ISteamRemoteStorage served by the
//game's own steam_api replacement) never reach it. For those games the save
//files live under a directory named after the app id inside the prefix:
//
//    compatdata/<appid>/pfx/drive_c/users/<user>/AppData/{Roaming,Local,LocalLow}/<...>/<appid>/{remote/,...}
//
//Discovery for that family is structural (directory named after the app id
//holding the save content), so it works for any user, app or emulator flavor
//without per-app configuration.
//
//Push direction (validated against the live engine):
//  1. mirror bucket files into <cloudRoot>/storage/<account>/<app>/<cleanpath>
//     and seed root_token/file_tokens bookkeeping,
//  2. bump <cloudRoot>/storage/<account>/<app>/cn.cloudredirect,
//  3. run the CLI verb `publish-full-manifest <provider> <account> <app>
//     <cloudRoot>`. The engine enumerates the storage tree, uploads CAS blobs
//     to Cloud shas and republishes state.cloudredirect + cn.cloudredirect
//     (refusing CN regressions, which is why we always bump first).
//
//Pull direction (validated against the live engine):
//  1. run the CLI verb `sync-remote-app <provider> <account> <app>
//     <cloudRoot>`; the engine downloads blobs only when the remote CN is
//     newer than the local bookkeeping, materializing
//     <cloudRoot>/storage/<account>/<app>/<cleanpath>/<sha-leaf> plus its own
//     manifest/cn bookkeeping,
//  2. copy every sha-leaf file back into the prefix bucket at <cleanpath>,
//     backing up conflicting bucket copies first.
//
//Triggers: launch/exit edges detected from IClientAppManager install state in
//the IPC frame (cheap, throttled), an initial sync at startup and a manual
//trigger file; the real filesystem/network work always happens on the worker
//thread, never inside the frame.

namespace PrefixBucketSync
{
    enum class Mode
    {
        PullOnly,
        PushOnly,
        Full,
    };

    inline const char* ModeToString(Mode m)
    {
        switch (m)
        {
            case Mode::PullOnly: return "pull";
            case Mode::PushOnly: return "push";
            case Mode::Full: return "full";
            default: return "unknown";
        }
    }

    //One emulator save bucket discovered inside a Proton prefix.
    struct PrefixBucket_t
    {
        uint32_t appId = 0;
        std::filesystem::path pfxDir;       //.../compatdata/<appid>/pfx
        std::filesystem::path userDir;      //.../pfx/drive_c/users/<user>
        std::filesystem::path remoteDir;    //directory that holds the save files directly
        std::string appDataSubName;         //e.g. AppData/Roaming
        std::string tailDirs;               //dirs between the AppData subtree root and remoteDir, e.g. "GSE Saves/<appid>/remote"

        //Equivalent engine UFS root token for the AppData subtree.
        const char* engineRootToken() const;
        //Clean path used inside CloudRedirect storage for a file of this bucket,
        //i.e. "<tailDirs>/<rel>" where rel is relative to remoteDir.
        std::string cleanPathForRel(const std::string& rel) const;
        //Path of the bucket file for a storage clean path, empty when the clean
        //path does not belong to this bucket's tail.
        std::filesystem::path remoteDirForCleanPath(const std::string& cleanPath) const;
    };

    struct SyncResult_t
    {
        bool ok = false;
        std::string message;
        unsigned int filesPulled = 0;
        unsigned int filesPushed = 0;
    };

    //Scans library steamapps directories for emulator buckets of the given app
    //ids. Pure filesystem logic without global state, safe from any thread.
    std::vector<PrefixBucket_t> DiscoverPrefixBuckets(
        const std::vector<std::filesystem::path>& steamappsDirs,
        const std::unordered_set<uint32_t>& appIds);

    //Worker lifecycle
    void init();
    void shutdown();

    //Cheap per-frame hook; only consults cached state and app install state.
    void notifyFrame();

    //Manual trigger; returns false when nothing was enqueued.
    bool requestSyncForApp(uint32_t appId, Mode mode, const char* reason);
    bool requestSyncForAllBuckets(Mode mode, const char* reason);

    //Snapshot for logging/diagnostics
    std::vector<uint32_t> GetWatchedAppIds();

    //- Test hooks ----------------------------------------------------------
    void SetRunCliOverrideForTest(
        const std::function<std::string(const std::vector<std::string>& args, std::string& outJson)>& fn);
    void SetCloudRootForTest(const std::filesystem::path& dir); //virtual ~/.config/CloudRedirect
    void SetStateDirForTest(const std::filesystem::path& dir);  //only used for CLI temp files
    void SetBackupDirForTest(const std::filesystem::path& dir);
    void SetAccountIdForTest(uint32_t accountId); //0 = derive from session
    void SetBucketDiscoverForTestEnabled(bool enabled); //skip library scanning entirely
    void SetBucketsForTest(const std::vector<PrefixBucket_t>& buckets); //inject cache, map by appId
    void SetSteamRootForTest(const std::filesystem::path& dir); //override Steam root with config/stplug-in and depotcache
    //Runs one job synchronously (no worker thread). Returns the result.
    SyncResult_t RunJobForTest(uint32_t appId, Mode mode, const char* reason);
    void ResetForTest(); //clears all static/module state
}
