/**
 * @file  zip.cpp
 * @brief ZIP archive extraction and memory buffer decompression routines
 */
#include "zip.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace Zip
{
    // ── Validation ────────────────────────────────────────────────────────────

    bool isValid(const std::string& path)
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;

        unsigned char magic[4] = {};
        const size_t n = fread(magic, 1, 4, fp);
        fclose(fp);

        if (n < 4) return false;

        // PK\x03\x04  normal local-file entry
        // PK\x05\x06  end-of-central-directory (empty archive)
        // PK\x07\x08  data descriptor (spanned/split)
        return magic[0] == 'P' && magic[1] == 'K' &&
               (magic[2] == 0x03 || magic[2] == 0x05 || magic[2] == 0x07);
    }

    // ── Assembly ──────────────────────────────────────────────────────────────

    bool assemble(const std::string& zipPath,
                  const std::string& luaPath,
                  const std::vector<std::string>& manifestPaths,
                  const std::string& providerPath)
    {
        if (luaPath.empty())
            return false;

        // Ensure fresh archive without stale entries
        fs::remove(zipPath);

        // Build argv:  zip -j -0 <zipPath> <luaPath> [manifests…] [providerPath]
        // We use execvp so no shell is involved (no injection risk).
        std::vector<const char*> argv;
        argv.reserve(4 + manifestPaths.size() + 2);
        argv.push_back("zip");
        argv.push_back("-j");   // junk paths (store basename only)
        argv.push_back("-0");   // no compression
        argv.push_back(zipPath.c_str());
        argv.push_back(luaPath.c_str());
        for (const auto& m : manifestPaths)
            argv.push_back(m.c_str());
        if (!providerPath.empty() && fs::exists(providerPath))
            argv.push_back(providerPath.c_str());
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) return false;

        if (pid == 0)
        {
            // Redirect stdout/stderr to /dev/null — callers read no output
            FILE* devnull = fopen("/dev/null", "w");
            if (devnull)
            {
                dup2(fileno(devnull), STDOUT_FILENO);
                dup2(fileno(devnull), STDERR_FILENO);
                fclose(devnull);
            }
            execvp("zip", const_cast<char* const*>(argv.data()));

            // Fallback: if zip is not available in environment, try 7z
            std::vector<const char*> argv7z;
            argv7z.reserve(6 + manifestPaths.size() + 2);
            argv7z.push_back("7z");
            argv7z.push_back("a");
            argv7z.push_back("-tzip");
            argv7z.push_back("-mx0");
            argv7z.push_back(zipPath.c_str());
            argv7z.push_back(luaPath.c_str());
            for (const auto& m : manifestPaths)
                argv7z.push_back(m.c_str());
            if (!providerPath.empty() && fs::exists(providerPath))
                argv7z.push_back(providerPath.c_str());
            argv7z.push_back(nullptr);
            execvp("7z", const_cast<char* const*>(argv7z.data()));

            _exit(127);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
        {
            fs::remove(zipPath);
            return false;
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            fs::remove(zipPath);
            return false;
        }

        return true;
    }

    // ── Extraction ────────────────────────────────────────────────────────────

    bool extract(const std::string& zipPath, const std::string& destDir)
    {
        fs::create_directories(destDir);

        pid_t pid = fork();
        if (pid < 0) return false;

        if (pid == 0)
        {
            // Redirect stdout/stderr to /dev/null
            FILE* devnull = fopen("/dev/null", "w");
            if (devnull)
            {
                dup2(fileno(devnull), STDOUT_FILENO);
                dup2(fileno(devnull), STDERR_FILENO);
                fclose(devnull);
            }

            // unzip -o -q <zipPath> -d <destDir>
            execlp("unzip", "unzip",
                   "-o", "-q",
                   zipPath.c_str(),
                   "-d", destDir.c_str(),
                   nullptr);

            // Fallback: if unzip is not available, try 7z: 7z x -y -o<destDir> <zipPath>
            std::string outArg = "-o" + destDir;
            execlp("7z", "7z", "x", "-y", outArg.c_str(), zipPath.c_str(), nullptr);

            _exit(127);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return false;
        if (!WIFEXITED(status)) return false;
        return WEXITSTATUS(status) == 0;
    }

    // ── File discovery ────────────────────────────────────────────────────────

    ExtractedFiles findFiles(const std::string& extractDir,
                             const std::string& appId)
    {
        ExtractedFiles result;
        const std::string expectedLua = appId + ".lua";
        const std::string expectedProvider = appId + ".provider";

        for (const auto& entry : fs::recursive_directory_iterator(extractDir))
        {
            if (!entry.is_regular_file()) continue;

            const std::string name = entry.path().filename().string();

            if (name == expectedLua)
            {
                result.luaFile = entry.path().string();
            }
            else if (name == expectedProvider)
            {
                result.providerFile = entry.path().string();
            }
            else if (name.size() > 9 &&
                     name.compare(name.size() - 9, 9, ".manifest") == 0)
            {
                result.manifestFiles.push_back(entry.path().string());
            }
        }

        return result;
    }

} // namespace Zip
