#pragma once

#include <string>
#include <vector>

namespace StoreInject
{
    /**
     * Start the background thread that monitors the Steam Store
     * via the CDP (Chrome DevTools Protocol) and performs injection.
     */
    void init();

    /**
     * Stop the background thread.
     */
    void shutdown();

    /**
     * Returns the 32-byte hex session authentication token.
     */
    const std::string& getNaviAuthToken();

    /**
     * Sets a test authentication token for unit testing.
     */
    void setNaviAuthTokenForTest(const std::string& token);
}
