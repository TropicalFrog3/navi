/**
 * @file  test_zip_and_atomic.cpp
 * @brief Unit tests for atomic file writer and ZIP extraction routines
 */
#include <gtest/gtest.h>
#include "zip.hpp"
#include "atomic_file.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ==========================================
// Zip Module Tests
// ==========================================

TEST(ZipTest, AssemblyExtractionAndDiscovery)
{
    const std::string tempDir = "/tmp/unit_test_zip_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string luaPath = tempDir + "/480.lua";
    {
        std::ofstream f(luaPath);
        f << "addappid(480)\nsetManifestid(480, \"1234567890\")\n";
    }

    const std::string provPath = tempDir + "/480.provider";
    {
        std::ofstream f(provPath);
        f << "test_provider\n";
    }

    const std::string m1 = tempDir + "/480_1.manifest";
    {
        std::ofstream f(m1);
        f << "MANIFEST_DATA_1";
    }

    const std::string m2 = tempDir + "/480_2.manifest";
    {
        std::ofstream f(m2);
        f << "MANIFEST_DATA_2";
    }

    const std::string zipPath = tempDir + "/480.zip";

    // 1. Assemble ZIP
    bool assembled = Zip::assemble(zipPath, luaPath, { m1, m2 }, provPath);
    ASSERT_TRUE(assembled);
    ASSERT_TRUE(fs::exists(zipPath));

    // 2. Validate ZIP
    EXPECT_TRUE(Zip::isValid(zipPath));

    // Non-zip file validation
    const std::string fakeZip = tempDir + "/fake.zip";
    {
        std::ofstream f(fakeZip);
        f << "This is not a zip file!";
    }
    EXPECT_FALSE(Zip::isValid(fakeZip));

    // 3. Extract ZIP
    const std::string extractDir = tempDir + "/extracted";
    bool extracted = Zip::extract(zipPath, extractDir);
    ASSERT_TRUE(extracted);
    ASSERT_TRUE(fs::exists(extractDir));

    // 4. Find files
    auto files = Zip::findFiles(extractDir, "480");
    EXPECT_FALSE(files.luaFile.empty());
    EXPECT_TRUE(fs::exists(files.luaFile));
    EXPECT_FALSE(files.providerFile.empty());
    EXPECT_TRUE(fs::exists(files.providerFile));
    EXPECT_EQ(files.manifestFiles.size(), 2u);

    std::ifstream pf(files.providerFile);
    std::string provContent;
    std::getline(pf, provContent);
    EXPECT_EQ(provContent, "test_provider");

    fs::remove_all(tempDir);
}

// ==========================================
// AtomicFile Tests
// ==========================================

TEST(AtomicFileTest, BasicWriteAndStream)
{
    const std::string tempDir = "/tmp/unit_test_atomic_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string target = tempDir + "/nested/path/sample.txt";

    // Write creates parent directories
    bool ok = AtomicFile::write(target, "Initial Atomic Content");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(fs::exists(target));

    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "Initial Atomic Content");
    }

    // Overwrite atomically
    ok = AtomicFile::write(target, "Second Version Content");
    ASSERT_TRUE(ok);
    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "Second Version Content");
    }

    // Stream write
    const std::string streamTarget = tempDir + "/stream.txt";
    bool streamOk = AtomicFile::writeStream(streamTarget, [](std::ostream& os) {
        os << "StreamLineA\nStreamLineB\n";
        return true;
    });
    ASSERT_TRUE(streamOk);
    {
        std::ifstream f(streamTarget);
        std::string la, lb;
        std::getline(f, la);
        std::getline(f, lb);
        EXPECT_EQ(la, "StreamLineA");
        EXPECT_EQ(lb, "StreamLineB");
    }

    fs::remove_all(tempDir);
}

TEST(AtomicFileTest, StreamFailureRecovery)
{
    const std::string tempDir = "/tmp/unit_test_atomic_recovery_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string target = tempDir + "/important.txt";
    ASSERT_TRUE(AtomicFile::write(target, "Do Not Corrupt Me"));

    // Callback returns false to simulate failure during streaming
    bool failedStream = AtomicFile::writeStream(target, [](std::ostream& os) {
        os << "Corrupted data";
        return false;
    });
    EXPECT_FALSE(failedStream);

    // Target file must remain untouched with original content
    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "Do Not Corrupt Me");
    }

    // No leftover .tmp files should exist in tempDir
    for (const auto& e : fs::directory_iterator(tempDir))
    {
        EXPECT_NE(e.path().filename().string().find(".tmp."), 0u);
    }

    fs::remove_all(tempDir);
}
