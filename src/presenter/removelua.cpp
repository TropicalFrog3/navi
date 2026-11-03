#include "removelua.hpp"
#include "cdpinject.hpp"
#include "log.hpp"
#include "config.hpp"
#include "version.hpp"
#include "storeinject.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace RemoveLua
{
    std::string loadResourceFile(const std::string& filename)
    {
        std::vector<std::string> paths;
        static const std::vector<std::string> categories = {
            "", "core/", "store/", "library/", "cloud/", "shell/", "vendor/"
        };

        std::vector<std::filesystem::path> searchDirs;

        // 1. Try to resolve relative to libnavi.so location using dladdr
        Dl_info info;
        if (dladdr((void*)loadResourceFile, &info) && info.dli_fname)
        {
            try
            {
                std::filesystem::path libPath(info.dli_fname);
                std::filesystem::path libDir = libPath.parent_path();
                searchDirs.push_back(libDir);
                searchDirs.push_back(libDir / "res" / "inject-scripts");
                searchDirs.push_back(libDir / ".." / "res" / "inject-scripts");
            }
            catch (...) {}
        }

        // 2. Add current working directory candidates
        searchDirs.push_back(".");
        searchDirs.push_back("./res/inject-scripts");
        searchDirs.push_back("../res/inject-scripts");

        // 3. Add home folder ~/.local/share/navi candidates
        const char* home = getenv("HOME");
        if (home)
        {
            searchDirs.push_back(std::filesystem::path(home) / ".local" / "share" / "navi");
            searchDirs.push_back(std::filesystem::path(home) / ".local" / "share" / "navi" / "res" / "inject-scripts");
        }

        // Direct path
        paths.push_back(filename);

        for (const auto& baseDir : searchDirs)
        {
            if (filename.find('/') != std::string::npos)
            {
                paths.push_back((baseDir / filename).string());
            }
            else
            {
                for (const auto& cat : categories)
                {
                    paths.push_back((baseDir / (cat + filename)).string());
                }
            }
        }

        for (const auto& path : paths) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                LOG_INFO("CDPInject::loadResourceFile: Successfully loaded %s from %s\n", filename.c_str(), path.c_str());
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }

        LOG_INFO("CDPInject::loadResourceFile: Could not find %s in any candidate path!\n", filename.c_str());
        return "";
    }

    bool injectRemoveLuaScript()
    {
        auto pages = CDPInject::fetchPages();
        if (pages.empty()) return false;

        std::vector<std::string> pagesNeedCloudManager;
        std::vector<std::string> pagesNeedAppDetails;
        std::vector<std::string> pagesNeedImportLua;
        std::vector<std::string> pagesNeedAutoCollection;
        std::vector<std::string> pagesNeedVersionTopBar;

        for (auto& page : pages)
        {
            if (page.webSocketDebuggerUrl.empty()) continue;
            if ((!page.type.empty() && page.type != "page") || page.url.empty() || page.url == "about:blank") continue;

            if (page.title == "SharedJSContext") {
                if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsAutoCollectionInjected"))
                {
                    pagesNeedAutoCollection.push_back(page.webSocketDebuggerUrl);
                }
                continue;
            }

            if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__naviCloudManagerLoaded"))
            {
                pagesNeedCloudManager.push_back(page.webSocketDebuggerUrl);
            }
            if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsAppDetailsInjected"))
            {
                pagesNeedAppDetails.push_back(page.webSocketDebuggerUrl);
            }
            if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsImportLuaInjected"))
            {
                pagesNeedImportLua.push_back(page.webSocketDebuggerUrl);
            }
            if (!CDPInject::isScriptInjected(page.webSocketDebuggerUrl, "!!window.__slsVersionTopBarInjected"))
            {
                pagesNeedVersionTopBar.push_back(page.webSocketDebuggerUrl);
            }
        }

        if (pagesNeedCloudManager.empty() && pagesNeedAppDetails.empty() && pagesNeedImportLua.empty() &&
            pagesNeedAutoCollection.empty() && pagesNeedVersionTopBar.empty())
        {
            return true;
        }

        // Lazy-loaded cache
        static std::string cachedCloudManagerScript;
        static std::string cachedAppDetailsScript;
        static std::string cachedImportLuaScript;
        static std::string cachedAutoCollectionScript;
        static std::string cachedVersionTopBarScript;

        if (!pagesNeedCloudManager.empty() && cachedCloudManagerScript.empty())
        {
            cachedCloudManagerScript = loadResourceFile("cloud_manager.js");
            if (cachedCloudManagerScript.empty()) {
                LOG_DEBUG("Failed to load cloud_manager.js");
                return false;
            }
        }

        if (!pagesNeedAppDetails.empty() && cachedAppDetailsScript.empty())
        {
            cachedAppDetailsScript = loadResourceFile("app-details-script.js");
            if (cachedAppDetailsScript.empty()) {
                LOG_DEBUG("Failed to load app-details-script.js");
                return false;
            }
        }

        if (!pagesNeedImportLua.empty() && cachedImportLuaScript.empty())
        {
            cachedImportLuaScript = loadResourceFile("import-lua-script.js");
            if (cachedImportLuaScript.empty()) {
                LOG_DEBUG("Failed to load import-lua-script.js");
                return false;
            }
        }
        
        if (!pagesNeedAutoCollection.empty() && cachedAutoCollectionScript.empty())
        {
            cachedAutoCollectionScript = loadResourceFile("auto-collection.js");
            if (!cachedAutoCollectionScript.empty()) {
                cachedAutoCollectionScript = "window.__slsAutoCollectionInjected = true;\n" + cachedAutoCollectionScript;
            }
        }

        if (!pagesNeedVersionTopBar.empty() && cachedVersionTopBarScript.empty())
        {
            cachedVersionTopBarScript = loadResourceFile("version-top-bar.js");
            if (cachedVersionTopBarScript.empty()) {
                LOG_DEBUG("Failed to load version-top-bar.js");
                return false;
            }

            size_t versionPos = cachedVersionTopBarScript.find("%VERSION%");
            if (versionPos != std::string::npos)
                cachedVersionTopBarScript.replace(versionPos, 9, VERSION);
        }

        for (const auto& wsUrl : pagesNeedCloudManager)
        {
            std::string authJs = "if (!window.__naviAuthToken) { window.__naviAuthToken = \"" + StoreInject::getNaviAuthToken() + "\"; }\n";
            CDPInject::injectJS(wsUrl, authJs + cachedCloudManagerScript);
        }

        for (const auto& wsUrl : pagesNeedAppDetails)
            CDPInject::injectJS(wsUrl, cachedAppDetailsScript);

        for (const auto& wsUrl : pagesNeedImportLua)
            CDPInject::injectJS(wsUrl, cachedImportLuaScript);

        for (const auto& wsUrl : pagesNeedAutoCollection)
            CDPInject::injectJS(wsUrl, cachedAutoCollectionScript);

        for (const auto& wsUrl : pagesNeedVersionTopBar)
            CDPInject::injectJS(wsUrl, cachedVersionTopBarScript);

        return true;
    }
}
