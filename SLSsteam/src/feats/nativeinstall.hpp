#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <vector>
#include <mutex>
#include <memory>

class IClientAppManager;

namespace NativeInstall
{
    using InstallAppFn = uint32_t(*)(IClientAppManager*, uint32_t, uint32_t, uint8_t);

    struct Selection
    {
        std::unordered_set<uint32_t> depots;                // user-selected depots
        std::unordered_map<uint32_t, uint64_t> gid;         // from Lua setManifestid
        std::unordered_map<uint32_t, uint64_t> size;        // from Lua manifest sizes
    };

    struct QueuedInstall
    {
        IClientAppManager* pClientAppManager = nullptr;
        uint32_t appId = 0;
        uint32_t library = 0;
        uint8_t a4 = 0;
    };

    void init();
    bool arm(uint32_t appId, const std::string& csvDepots); // Called from HTTP thread
    void disarm(uint32_t appId);                            // Disarm and remove persistence
    void runFrame(InstallAppFn original);                   // Main thread only (ProcessIPCFrame)
    std::shared_ptr<const Selection> getSelection(uint32_t appId);  // Used by BuildDepotDependency
    bool isNative(uint32_t appId);                          // Check if app is native install
    bool isQueuedOrArmed(uint32_t appId);                   // True if queued to run or already armed
    void load();                                            // Load from native_installs.yaml
    void save();                                            // Save to native_installs.yaml
}
