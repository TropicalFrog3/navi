#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Sync
{
    enum class ESyncTriggerReason : uint8_t
    {
        k_Launch = 1,
        k_WakeFromSleep = 2,
        k_ManualUserCheck = 3,
        k_P2PPeerDiscovered = 4
    };

    using WakeCallback_t = std::function<void(ESyncTriggerReason)>;

    class PowerMonitor
    {
    public:
        explicit PowerMonitor(
            WakeCallback_t onTrigger,
            int pollIntervalMs = 2000,
            int64_t sleepJumpThresholdSec = 5,
            std::string procNetRoutePath = "/proc/net/route");

        ~PowerMonitor();

        void start();
        void stop();

        void triggerLaunchCheckOnce();

        static bool hasClockJumped(
            int64_t prevMonoSec,
            int64_t currMonoSec,
            int64_t prevBootOrRealSec,
            int64_t currBootOrRealSec,
            int64_t thresholdSec = 5);

        static bool isNetworkRouteReady(const std::string& procNetRoutePath = "/proc/net/route");

        bool waitForNetworkReady(int maxWaitMs = 15000, int pollStepMs = 250) const;

        void simulateResumeEventForTest();

    private:
        void monitorLoop();

        WakeCallback_t m_onTrigger;
        int m_pollIntervalMs;
        int64_t m_sleepJumpThresholdSec;
        std::string m_procNetRoutePath;

        std::atomic<bool> m_running{false};
        std::atomic<bool> m_stopping{false};
        std::atomic<bool> m_launchTriggered{false};
        std::thread m_thread;
        std::condition_variable m_cv;
        std::mutex m_cvMutex;

        mutable std::mutex m_wakeMutex;
        int64_t m_lastWakeTimeSec = 0;
    };
} // namespace Sync
