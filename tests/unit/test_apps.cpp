/**
 * @file  test_apps.cpp
 * @brief Unit tests for Apps lifecycle, Family Share lock behavior, and CM packet handling
 */
#include <gtest/gtest.h>
#include "apps.hpp"
#include "fakeappid.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "CNetPacket.hpp"
#include "CSteamEngine.hpp"
#include "CUser.hpp"
#include "protobufs/steammessages_clientserver.pb.h"
#include "protobufs/steammessages_clientserver_2.pb.h"
#include "test_stubs.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace
{
    struct PacketRAII
    {
        CNetPacket pkt{};
        ~PacketRAII() { pkt.free(); }
    };

    template<typename MsgT>
    void buildProtoPacket(PacketRAII& out, EMsg emsg, const MsgT& msg, const CMsgProtoBufHeader* header = nullptr)
    {
        out.pkt.free();
        out.pkt.body = static_cast<CNetPacketBody*>(Steam::Plat_Alloc(sizeof(CNetPacketBody)));
        ASSERT_NE(out.pkt.body, nullptr);
        out.pkt.body->type = static_cast<ENetPacket>(static_cast<uint32_t>(emsg) | PROTOBUF_TYPE_MASK);
        out.pkt.body->headerSize = 0;
        out.pkt.size = sizeof(CNetPacketBody);
        out.pkt.originalBody = out.pkt.body;

        CMsgProtoBufHeader defaultHeader;
        out.pkt.serialize(msg, header ? header : &defaultHeader);
    }
}

class AppsFamilyShareTest : public ::testing::Test
{
protected:
    std::string m_tempDir;
    std::string m_origHome;
    std::string m_origXdg;
    bool m_hadXdg = false;

    Steam::Plat_Alloc_t m_origAlloc = nullptr;
    Steam::Plat_Free_t m_origFree = nullptr;
    Steam::Plat_Realloc_t m_origRealloc = nullptr;
    CSteamId m_origSteamId{};
    CSteamEngine* m_origSteamEngine = nullptr;
    bool m_origDisableFamilyLock = false;
    AppId_t m_origFamilyShareFakeAppId = 0;
    CConfig::FakeGame_t m_origIdleStatus{};
    std::unordered_map<AppId_t, AppId_t> m_origFakeAppIds;

    void SetUp() override
    {
        m_tempDir = "/tmp/unit_test_apps_" + std::to_string(getpid());
        fs::remove_all(m_tempDir);
        fs::create_directories(m_tempDir + "/.config");

        const char* h = getenv("HOME");
        m_origHome = h ? h : "";
        const char* x = getenv("XDG_CONFIG_HOME");
        if (x)
        {
            m_hadXdg = true;
            m_origXdg = x;
        }
        setenv("HOME", m_tempDir.c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");

        m_origAlloc = Steam::Plat_Alloc;
        m_origFree = Steam::Plat_Free;
        m_origRealloc = Steam::Plat_Realloc;
        Steam::Plat_Alloc = [](int sz) -> void* { return std::malloc(static_cast<size_t>(sz)); };
        Steam::Plat_Free = std::free;
        Steam::Plat_Realloc = [](void* p, int sz) -> void* { return std::realloc(p, static_cast<size_t>(sz)); };

        m_origSteamId = g_currentSteamId;
        m_origSteamEngine = g_pSteamEngine;
        m_origDisableFamilyLock = g_config.disableFamilyLock.get();
        m_origFamilyShareFakeAppId = g_config.familyShareFakeAppId.get();
        m_origIdleStatus = g_config.idleStatus.get();
        m_origFakeAppIds = g_config.fakeAppIds.get();

        static CSteamEngine s_engine;
        g_pSteamEngine = &s_engine;
        g_currentSteamId = CSteamId(999888);
        g_config.fakeAppIds = std::unordered_map<AppId_t, AppId_t>{};
        g_config.familyShareFakeAppId = 0;
        clearMockAppOwnership();
    }

    void TearDown() override
    {
        clearMockAppOwnership();
        g_config.fakeAppIds = m_origFakeAppIds;
        g_config.familyShareFakeAppId = m_origFamilyShareFakeAppId;
        g_config.disableFamilyLock = m_origDisableFamilyLock;
        g_config.idleStatus = m_origIdleStatus;
        g_currentSteamId = m_origSteamId;
        g_pSteamEngine = m_origSteamEngine;

        Steam::Plat_Alloc = m_origAlloc;
        Steam::Plat_Free = m_origFree;
        Steam::Plat_Realloc = m_origRealloc;

        if (!m_origHome.empty())
        {
            setenv("HOME", m_origHome.c_str(), 1);
        }
        if (m_hadXdg)
        {
            setenv("XDG_CONFIG_HOME", m_origXdg.c_str(), 1);
        }
        else
        {
            unsetenv("XDG_CONFIG_HOME");
        }
        fs::remove_all(m_tempDir);
    }
};

TEST_F(AppsFamilyShareTest, SendGamesPlayedPreservesOwnerIdWhenDisableFamilyLockDisabled)
{
    g_config.disableFamilyLock = false;
    g_config.idleStatus = CConfig::FakeGame_t{};

    CMsgClientGamesPlayed req;
    auto* ownedGame = req.add_games_played();
    ownedGame->set_game_id(730);
    ownedGame->set_owner_id(999888);

    auto* familyGame = req.add_games_played();
    familyGame->set_game_id(570);
    familyGame->set_owner_id(123456);

    PacketRAII holder;
    buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

    Apps::sendGamesPlayed(&holder.pkt);

    const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
    ASSERT_EQ(out.games_played_size(), 2);
    // Regression: setting owner_id(1) triggers Valve CM disconnects ("logged in elsewhere")
    EXPECT_EQ(out.games_played(0).owner_id(), 999888u);
    EXPECT_EQ(out.games_played(1).owner_id(), 123456u);
    EXPECT_NE(out.games_played(0).owner_id(), 1u);
    EXPECT_NE(out.games_played(1).owner_id(), 1u);
}

TEST_F(AppsFamilyShareTest, SendGamesPlayedKeepsUnmaskedFamilySharedGames)
{
    // The CM family-lock timer is server-side. Removing the entry made Steam
    // report "playing nothing", which breaks online matchmaking; presence must stay.
    g_config.disableFamilyLock = true;
    g_config.idleStatus = CConfig::FakeGame_t{};

    AppOwnershipInfo_t ownedInfo{};
    ownedInfo.owner = 999888;
    ownedInfo.ownsLicense = true;
    ownedInfo.familyShared = false;
    setMockAppOwnership(730, ownedInfo);

    AppOwnershipInfo_t familyInfo{};
    familyInfo.owner = 1555636215;
    familyInfo.ownsLicense = true;
    familyInfo.familyShared = true;
    setMockAppOwnership(1566200, familyInfo);

    // Case 1: Mix of owned, familyShared mock, unmocked different-owner, and non-Steam shortcut
    {
        CMsgClientGamesPlayed req;
        auto* owned = req.add_games_played();
        owned->set_game_id(730);
        owned->set_owner_id(999888);

        auto* sharedByFlag = req.add_games_played();
        sharedByFlag->set_game_id(1566200);
        sharedByFlag->set_owner_id(1555636215);

        auto* sharedByOwner = req.add_games_played();
        sharedByOwner->set_game_id(570);
        sharedByOwner->set_owner_id(123456);

        auto* shortcut = req.add_games_played();
        shortcut->set_game_id(GAME_TYPE_SHORTCUT | 99ULL);
        shortcut->set_owner_id(0);

        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

        Apps::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 4);
        EXPECT_EQ(out.games_played(0).game_id(), 730u);
        EXPECT_EQ(out.games_played(1).game_id(), 1566200u);
        EXPECT_EQ(out.games_played(2).game_id(), 570u);
        EXPECT_EQ(out.games_played(3).game_id(), GAME_TYPE_SHORTCUT | 99ULL);
    }

    // Case 2: Only unmasked family-shared game + IdleStatus configured -> the real game stays
    {
        g_config.idleStatus = CConfig::FakeGame_t{ 480, "Idle" };
        CMsgClientGamesPlayed req;
        auto* sharedOnly = req.add_games_played();
        sharedOnly->set_game_id(1566200);
        sharedOnly->set_owner_id(1555636215);

        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

        Apps::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).game_id(), 1566200u);
    }

    // Case 3: Unset g_currentSteamId must not drop entries
    {
        g_config.idleStatus = CConfig::FakeGame_t{};
        g_currentSteamId = CSteamId{};
        CMsgClientGamesPlayed req;
        auto* unflagged = req.add_games_played();
        unflagged->set_game_id(570);
        unflagged->set_owner_id(123456);

        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

        Apps::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).game_id(), 570u);
    }
}

TEST_F(AppsFamilyShareTest, SendGamesPlayedPreservesAndRemapsFamilySharedWithFakeAppId)
{
    g_config.disableFamilyLock = true;
    g_config.idleStatus = CConfig::FakeGame_t{};
    g_config.fakeAppIds = std::unordered_map<AppId_t, AppId_t>{ { 1566200, 480 } };

    AppOwnershipInfo_t familyInfo{};
    familyInfo.owner = 1555636215;
    familyInfo.ownsLicense = true;
    familyInfo.familyShared = true;
    setMockAppOwnership(1566200, familyInfo);

    // Case 1: Fake target 480 unowned -> falls back to self SteamID (never keeps lender owner_id)
    {
        CMsgClientGamesPlayed req;
        auto* game = req.add_games_played();
        game->set_game_id(1566200);
        game->set_owner_id(1555636215);

        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

        Apps::sendGamesPlayed(&holder.pkt);
        FakeAppIds::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).game_id(), 480u);
        EXPECT_EQ(out.games_played(0).owner_id(), 999888u);
        EXPECT_NE(out.games_played(0).owner_id(), 1555636215u);
    }

    // Case 2: Fake target 480 has explicit ownership record -> uses target's owner_id
    {
        AppOwnershipInfo_t targetInfo{};
        targetInfo.owner = 555666;
        targetInfo.ownsLicense = true;
        setMockAppOwnership(480, targetInfo);

        CMsgClientGamesPlayed req;
        auto* game = req.add_games_played();
        game->set_game_id(1566200);
        game->set_owner_id(1555636215);

        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

        Apps::sendGamesPlayed(&holder.pkt);
        FakeAppIds::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).game_id(), 480u);
        EXPECT_EQ(out.games_played(0).owner_id(), 555666u);
    }

    // Case 3: FamilyShareFakeAppId fallback remaps a borrowed app with no explicit entry
    {
        g_config.fakeAppIds = std::unordered_map<AppId_t, AppId_t>{};
        g_config.familyShareFakeAppId = 480;

        CMsgClientGamesPlayed req;
        auto* game = req.add_games_played();
        game->set_game_id(1566200);
        game->set_owner_id(1555636215);

        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

        Apps::sendGamesPlayed(&holder.pkt);
        FakeAppIds::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).game_id(), 480u);
        EXPECT_NE(out.games_played(0).owner_id(), 1555636215u);
    }

    // Case 4: Wildcard FakeAppIds with null g_pSteamEngine does not dereference null
    {
        g_pSteamEngine = nullptr;
        g_config.fakeAppIds = std::unordered_map<AppId_t, AppId_t>{ { 0, 480 } };
        EXPECT_EQ(FakeAppIds::getFakeAppId(1566200), 480u);
    }
}

TEST_F(AppsFamilyShareTest, RichPresenceRemappedForFamilySharedApp)
{
    // Rich Presence must keep flowing: choking it is what made the client look idle.
    std::ifstream hooksFile("src/presenter/hooks.cpp");
    ASSERT_TRUE(hooksFile.is_open());
    std::string hooksSrc((std::istreambuf_iterator<char>(hooksFile)), std::istreambuf_iterator<char>());
    EXPECT_EQ(hooksSrc.find("shouldChokeSendPacket"), std::string::npos);

    AppOwnershipInfo_t ownedInfo{};
    ownedInfo.owner = 999888;
    ownedInfo.ownsLicense = true;
    setMockAppOwnership(730, ownedInfo);

    AppOwnershipInfo_t familyInfo{};
    familyInfo.owner = 1555636215;
    familyInfo.ownsLicense = true;
    familyInfo.familyShared = true;
    setMockAppOwnership(1566200, familyInfo);

    CMsgClientRichPresenceUpload rpBody;
    CMsgProtoBufHeader familyHeader;
    familyHeader.set_routing_appid(1566200);

    // FamilyShareFakeAppId fallback remaps the routing appid
    g_config.disableFamilyLock = true;
    g_config.familyShareFakeAppId = 480;
    PacketRAII rpFamilyPkt;
    buildProtoPacket(rpFamilyPkt, k_EMsgClientRichPresenceUpload, rpBody, &familyHeader);
    FakeAppIds::sendRichPresenceUpload(&rpFamilyPkt.pkt);
    EXPECT_EQ(rpFamilyPkt.pkt.deserializeHeader().routing_appid(), 480u);

    // Explicit FakeAppIds entry wins over the fallback
    g_config.fakeAppIds = std::unordered_map<AppId_t, AppId_t>{ { 1566200, 730 } };
    PacketRAII rpExplicitPkt;
    buildProtoPacket(rpExplicitPkt, k_EMsgClientRichPresenceUpload, rpBody, &familyHeader);
    FakeAppIds::sendRichPresenceUpload(&rpExplicitPkt.pkt);
    EXPECT_EQ(rpExplicitPkt.pkt.deserializeHeader().routing_appid(), 730u);
    g_config.fakeAppIds = std::unordered_map<AppId_t, AppId_t>{};

    // No fallback configured -> untouched
    g_config.familyShareFakeAppId = 0;
    PacketRAII rpNoFallbackPkt;
    buildProtoPacket(rpNoFallbackPkt, k_EMsgClientRichPresenceUpload, rpBody, &familyHeader);
    FakeAppIds::sendRichPresenceUpload(&rpNoFallbackPkt.pkt);
    EXPECT_EQ(rpNoFallbackPkt.pkt.deserializeHeader().routing_appid(), 1566200u);

    // Owned app rich presence is never remapped by the family fallback
    g_config.familyShareFakeAppId = 480;
    CMsgProtoBufHeader ownedHeader;
    ownedHeader.set_routing_appid(730);
    PacketRAII rpOwnedPkt;
    buildProtoPacket(rpOwnedPkt, k_EMsgClientRichPresenceUpload, rpBody, &ownedHeader);
    FakeAppIds::sendRichPresenceUpload(&rpOwnedPkt.pkt);
    EXPECT_EQ(rpOwnedPkt.pkt.deserializeHeader().routing_appid(), 730u);
}

TEST_F(AppsFamilyShareTest, SendGamesPlayedIdleStatusUsesRealOwnershipOrCurrentSteamId)
{
    g_config.disableFamilyLock = true;

    // Case 1: IdleStatus app has real ownership info (e.g. owned or family-shared)
    AppOwnershipInfo_t ownedInfo{};
    ownedInfo.owner = 555666;
    setMockAppOwnership(480, ownedInfo);
    g_config.idleStatus = CConfig::FakeGame_t{ 480, "Owned Idle" };

    {
        CMsgClientGamesPlayed emptyReq;
        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, emptyReq);

        Apps::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).game_id(), 480u);
        EXPECT_EQ(out.games_played(0).owner_id(), 555666u);
    }

    // Case 2: IdleStatus app unowned / checkAppOwnership returns false -> falls back to current user accountId
    clearMockAppOwnership();
    g_config.idleStatus = CConfig::FakeGame_t{ 480, "Unowned Idle" };

    {
        CMsgClientGamesPlayed emptyReq;
        PacketRAII holder;
        buildProtoPacket(holder, k_EMsgClientGamesPlayed, emptyReq);

        Apps::sendGamesPlayed(&holder.pkt);

        const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
        ASSERT_EQ(out.games_played_size(), 1);
        EXPECT_EQ(out.games_played(0).owner_id(), 999888u);
        EXPECT_NE(out.games_played(0).owner_id(), 1u);
    }
}

TEST_F(AppsFamilyShareTest, UnlockAppPreservesExistingFamilySharedOwnership)
{
    AppOwnershipInfo_t info{};
    info.subId = 4242;
    info.owner = 777777;
    info.familyShared = true;
    info.licensePermanent = false;
    info.freeLicense = false;

    ASSERT_TRUE(Apps::unlockApp(730, &info));

    EXPECT_TRUE(info.familyShared);
    EXPECT_EQ(info.owner, 777777u);
    EXPECT_FALSE(info.licensePermanent);
    EXPECT_TRUE(info.freeLicense);
    EXPECT_TRUE(info.ownsLicense);
}

TEST_F(AppsFamilyShareTest, SendGamesPlayedKeepsNewSteamFamiliesBorrowedApp)
{
    // Regression: the CM family-lock timer is server-side. Dropping the entry made
    // Steam report "playing nothing" and broke online matchmaking; presence must stay.
    g_config.disableFamilyLock = true;
    g_config.idleStatus = CConfig::FakeGame_t{};

    AppOwnershipInfo_t borrowedInfo{};
    borrowedInfo.owner = 999888; // self, not the lender
    borrowedInfo.realOwner = 0;
    borrowedInfo.ownsLicense = true;
    borrowedInfo.familyShared = false;
    setMockAppOwnership(1566200, borrowedInfo);
    setMockDirectOwnership(1566200, false);

    CMsgClientGamesPlayed req;
    auto* borrowed = req.add_games_played();
    borrowed->set_game_id(1566200);
    borrowed->set_owner_id(999888);

    PacketRAII holder;
    buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

    Apps::sendGamesPlayed(&holder.pkt);

    const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
    ASSERT_EQ(out.games_played_size(), 1);
    EXPECT_EQ(out.games_played(0).game_id(), 1566200u);
}

TEST_F(AppsFamilyShareTest, SendGamesPlayedKeepsDirectlyOwnedAppInsideFamilyGroup)
{
    // A directly owned game must never be hidden just because another family member
    // also owns a copy of it (Steam's own 0x100 flag logic requires !OwnsAppDirectly).
    g_config.disableFamilyLock = true;
    g_config.idleStatus = CConfig::FakeGame_t{};

    AppOwnershipInfo_t ownedInfo{};
    ownedInfo.owner = 999888;
    ownedInfo.ownsLicense = true;
    ownedInfo.familyShared = false;
    setMockAppOwnership(730, ownedInfo);
    setMockDirectOwnership(730, true);

    CMsgClientGamesPlayed req;
    auto* owned = req.add_games_played();
    owned->set_game_id(730);
    owned->set_owner_id(999888);

    PacketRAII holder;
    buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

    Apps::sendGamesPlayed(&holder.pkt);

    const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
    ASSERT_EQ(out.games_played_size(), 1);
    EXPECT_EQ(out.games_played(0).game_id(), 730u);

    // An app with no license at all (e.g. not owned and not installed) is never hidden:
    // CheckAppOwnership returns false, so it cannot be mistaken for a borrowed title.
    CMsgClientGamesPlayed req2;
    auto* retained = req2.add_games_played();
    retained->set_game_id(440);
    retained->set_owner_id(999888);
    clearMockAppOwnership();

    PacketRAII holder2;
    buildProtoPacket(holder2, k_EMsgClientGamesPlayed, req2);
    Apps::sendGamesPlayed(&holder2.pkt);
    EXPECT_EQ(holder2.pkt.deserializeBody<CMsgClientGamesPlayed>().games_played_size(), 1);
}

TEST_F(AppsFamilyShareTest, SendGamesPlayedKeepsLegacyFamilySharedByOwnerMismatch)
{
    // Legacy Family Sharing (pre-FamilyGroups) reports familyShared == 0 but sets the
    // ownership owner to the lender. The owner-mismatch branch must still classify the
    // app as borrowed (so presence can be remapped) while keeping the entry itself.
    g_config.disableFamilyLock = true;
    g_config.idleStatus = CConfig::FakeGame_t{};

    AppOwnershipInfo_t lenderOwned{};
    lenderOwned.owner = 123456; // lender, not the local account (999888)
    lenderOwned.realOwner = 0;
    lenderOwned.ownsLicense = true;
    lenderOwned.familyShared = false; // legacy path deliberately not flagged
    setMockAppOwnership(570, lenderOwned);
    setMockDirectOwnership(570, true); // would keep the app if the branch were skipped

    // Packet owner is self on purpose: the packet field must not be the only signal.
    CMsgClientGamesPlayed req;
    auto* shared = req.add_games_played();
    shared->set_game_id(570);
    shared->set_owner_id(999888);

    PacketRAII holder;
    buildProtoPacket(holder, k_EMsgClientGamesPlayed, req);

    Apps::sendGamesPlayed(&holder.pkt);

    const auto out = holder.pkt.deserializeBody<CMsgClientGamesPlayed>();
    ASSERT_EQ(out.games_played_size(), 1);
    EXPECT_EQ(out.games_played(0).game_id(), 570u);
    EXPECT_TRUE(Apps::isFamilySharedApp(g_pSteamEngine->getUser(0), 570));
}

TEST_F(AppsFamilyShareTest, DefaultConfigEnablesFamilyShareLock)
{
    // 1. Default created config file must keep the bypass ON.
    ASSERT_TRUE(g_config.createFile());
    ASSERT_TRUE(g_config.loadSettings(true));
    EXPECT_TRUE(g_config.disableFamilyLock.get());
    EXPECT_EQ(g_config.familyShareFakeAppId.get(), 480u);

    // 2. Fallback when DisableFamilyShareLock key is omitted from config.yaml
    {
        std::ofstream f(g_config.getPath());
        f << "UseWhitelist: no\n";
    }
    ASSERT_TRUE(g_config.loadSettings(true));
    EXPECT_TRUE(g_config.disableFamilyLock.get());

    // 3. Repository template res/config.yaml
    std::ifstream templateFile("res/config.yaml");
    ASSERT_TRUE(templateFile.is_open());
    std::string yaml((std::istreambuf_iterator<char>(templateFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(yaml.find("DisableFamilyShareLock: yes"), std::string::npos);
    EXPECT_EQ(yaml.find("DisableFamilyShareLock: no"), std::string::npos);
    EXPECT_NE(yaml.find("FamilyShareFakeAppId: 480"), std::string::npos);
}

TEST_F(AppsFamilyShareTest, RecvPktDoesNotChokeSharedLibraryStopPlaying)
{
    std::ifstream hooksFile("src/presenter/hooks.cpp");
    ASSERT_TRUE(hooksFile.is_open());
    std::string hooksSrc((std::istreambuf_iterator<char>(hooksFile)), std::istreambuf_iterator<char>());

    // Regression: choking k_EMsgClientSharedLibraryStopPlaying prevents Steam's 3-minute notice,
    // causing server-side forced CM disconnects ("account logged in on another computer").
    EXPECT_EQ(hooksSrc.find("k_EMsgClientSharedLibraryStopPlaying"), std::string::npos);
    EXPECT_NE(hooksSrc.find("Apps::shouldChokeRecvPacket"), std::string::npos);

    // Verify k_EMsgClientSharedLibraryStopPlaying is never choked even when DisableFamilyShareLock is enabled
    g_config.disableFamilyLock = true;
    {
        CMsgClientGamesPlayed dummyBody;
        PacketRAII stopPlayingPkt;
        buildProtoPacket(stopPlayingPkt, k_EMsgClientSharedLibraryStopPlaying, dummyBody);
        EXPECT_FALSE(Apps::shouldChokeRecvPacket(&stopPlayingPkt.pkt));
    }

    // Client-side FamilyGroups lock notification is choked only when DisableFamilyShareLock is true
    {
        CMsgClientGamesPlayed dummyBody;
        CMsgProtoBufHeader familyHeader;
        familyHeader.set_target_job_name("FamilyGroupsClient.NotifyRunningApps#1");
        PacketRAII familyPkt;
        buildProtoPacket(familyPkt, k_EMsgServiceMethod, dummyBody, &familyHeader);

        g_config.disableFamilyLock = true;
        EXPECT_TRUE(Apps::shouldChokeRecvPacket(&familyPkt.pkt));

        g_config.disableFamilyLock = false;
        EXPECT_FALSE(Apps::shouldChokeRecvPacket(&familyPkt.pkt));
    }
}

