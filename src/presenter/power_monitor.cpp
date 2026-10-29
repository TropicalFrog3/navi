#include "power_monitor.hpp"
#include "log.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace Sync
{
    PowerMonitor::PowerMonitor(
        WakeCallback_t onTrigger,
        int pollIntervalMs,
        int64_t sleepJumpThresholdSec,
        std::string procNetRoutePath)
        : m_onTrigger(std::move(onTrigger))
        , m_pollIntervalMs(pollIntervalMs)
        , m_sleepJumpThresholdSec(sleepJumpThresholdSec)
        , m_procNetRoutePath(std::move(procNetRoutePath))
    {
    }

    PowerMonitor::~PowerMonitor()
    {
        stop();
    }

    void PowerMonitor::start()
    {
        if (m_running.exchange(true))
        {
            return;
        }

        m_stopping = false;
        m_thread = std::thread([this]() {
            triggerLaunchCheckOnce();
            monitorLoop();
        });
    }

    void PowerMonitor::stop()
    {
        if (!m_running && !m_stopping)
        {
            return;
        }

        m_stopping = true;
        m_running = false;
        m_cv.notify_all();

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void PowerMonitor::triggerLaunchCheckOnce()
    {
        bool expected = false;
        if (m_launchTriggered.compare_exchange_strong(expected, true))
        {
            waitForNetworkReady(5000, 100);
            if (m_onTrigger)
            {
                m_onTrigger(ESyncTriggerReason::k_Launch);
            }
        }
    }

    bool PowerMonitor::hasClockJumped(
        int64_t prevMonoSec,
        int64_t currMonoSec,
        int64_t prevBootOrRealSec,
        int64_t currBootOrRealSec,
        int64_t thresholdSec)
    {
        int64_t deltaMono = currMonoSec - prevMonoSec;
        int64_t deltaReal = currBootOrRealSec - prevBootOrRealSec;
        return (deltaMono >= 0 && (deltaReal - deltaMono) >= thresholdSec);
    }

    bool PowerMonitor::isNetworkRouteReady(const std::string& procNetRoutePath)
    {
        std::ifstream file(procNetRoutePath);
        if (!file.is_open())
        {
            return false;
        }

        std::string header;
        if (!std::getline(file, header))
        {
            return false;
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string iface, destination, gateway, flagsStr;
            if (!(iss >> iface >> destination >> gateway >> flagsStr))
            {
                continue;
            }

            if (iface == "lo")
            {
                continue;
            }

            unsigned long flags = 0;
            try
            {
                flags = std::stoul(flagsStr, nullptr, 16);
            }
            catch (...)
            {
                continue;
            }

            constexpr unsigned long RTF_UP = 0x0001;
            if ((flags & RTF_UP) != 0 || destination == "00000000")
            {
                return true;
            }
        }

        return false;
    }

    bool PowerMonitor::waitForNetworkReady(int maxWaitMs, int pollStepMs) const
    {
        if (pollStepMs <= 0) pollStepMs = 50;
        int waited = 0;
        while (waited < maxWaitMs && !m_stopping)
        {
            if (isNetworkRouteReady(m_procNetRoutePath))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(pollStepMs));
            waited += pollStepMs;
        }
        return isNetworkRouteReady(m_procNetRoutePath);
    }

    void PowerMonitor::simulateResumeEventForTest()
    {
        struct timespec tsMono{};
        clock_gettime(CLOCK_MONOTONIC, &tsMono);
        int64_t currMono = tsMono.tv_sec;

        {
            std::lock_guard<std::mutex> lock(m_wakeMutex);
            if (m_lastWakeTimeSec != 0 && (currMono - m_lastWakeTimeSec) < 2)
            {
                return;
            }
            m_lastWakeTimeSec = currMono;
        }

        waitForNetworkReady(500, 50);
        if (m_onTrigger)
        {
            m_onTrigger(ESyncTriggerReason::k_WakeFromSleep);
        }
    }

    void PowerMonitor::monitorLoop()
    {
        struct timespec tsMono{}, tsBoot{};
        clock_gettime(CLOCK_MONOTONIC, &tsMono);
        clock_gettime(CLOCK_BOOTTIME, &tsBoot);
        int64_t prevMono = tsMono.tv_sec;
        int64_t prevBoot = tsBoot.tv_sec;

        while (m_running)
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            if (m_cv.wait_for(lock, std::chrono::milliseconds(m_pollIntervalMs), [this] { return !m_running; }))
            {
                break;
            }

            clock_gettime(CLOCK_MONOTONIC, &tsMono);
            clock_gettime(CLOCK_BOOTTIME, &tsBoot);
            int64_t currMono = tsMono.tv_sec;
            int64_t currBoot = tsBoot.tv_sec;

            if (hasClockJumped(prevMono, currMono, prevBoot, currBoot, m_sleepJumpThresholdSec))
            {
                bool debounce = false;
                {
                    std::lock_guard<std::mutex> wlock(m_wakeMutex);
                    if (m_lastWakeTimeSec != 0 && (currMono - m_lastWakeTimeSec) < 2)
                    {
                        debounce = true;
                    }
                    else
                    {
                        m_lastWakeTimeSec = currMono;
                    }
                }

                if (!debounce)
                {
                    waitForNetworkReady(15000, 250);
                    if (m_running && m_onTrigger)
                    {
                        m_onTrigger(ESyncTriggerReason::k_WakeFromSleep);
                    }
                }
            }

            prevMono = currMono;
            prevBoot = currBoot;
        }
    }
} // namespace Sync
