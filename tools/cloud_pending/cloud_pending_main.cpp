/**
 * @file  cloud_pending_main.cpp
 * @brief Out-of-process idle helper executable applying queued save ops
 */
#include "cloud_pending_ops.hpp"
#include "cloudredirect.hpp"
#include "log.hpp"

#include <iostream>
#include <cstring>
#include <memory>

int main(int argc, char* argv[])
{
    g_pLog = std::unique_ptr<CLog>(CLog::createDefaultLog());
    bool processAll = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--process-all") == 0)
        {
            processAll = true;
        }
    }

    if (!processAll)
    {
        std::cerr << "Usage: navi-cloud-apply-pending --process-all\n";
        return 1;
    }

    Navi::CloudRedirect::CPendingOpsExecutor executor;
    if (!executor.AssertSteamIdle())
    {
        std::cerr << "Steam client processes are still active. Cannot apply pending operations.\n";
        return 2;
    }

    int rc = executor.ExecuteAllPending();
    return rc;
}
