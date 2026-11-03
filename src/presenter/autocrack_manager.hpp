#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace AutoCrack {

enum class Origin {
    Auto,
    Manual
};

enum class State {
    Idle,
    Queued,
    Running,
    Succeeded,
    NeedsReapply,
    Failed,
    Skipped,
    Restoring
};

inline const char* stateToString(State s) {
    switch (s) {
        case State::Idle: return "idle";
        case State::Queued: return "queued";
        case State::Running: return "running";
        case State::Succeeded: return "succeeded";
        case State::NeedsReapply: return "needs_reapply";
        case State::Failed: return "failed";
        case State::Skipped: return "skipped";
        case State::Restoring: return "restoring";
        default: return "unknown";
    }
}

struct Status {
    uint32_t appId = 0;
    State state = State::Idle;
    std::string stage;     // "gameinfo" | "unpack" | "apply" ...
    std::string message;
    int64_t updatedAt = 0;
};

void init();
void shutdown();
bool enqueue(uint32_t appId, Origin o, bool force = false);
bool enqueueRestore(uint32_t appId);
Status inspectOrGetStatus(uint32_t appId);
void setStatus(uint32_t appId, State state, const std::string& stage, const std::string& msg);

std::filesystem::path getAutoCrackDir();
std::filesystem::path getManifestPath(uint32_t appId);
bool isDriftDetected(uint32_t appId);
bool performRestore(uint32_t appId, std::string& outError);

} // namespace AutoCrack

