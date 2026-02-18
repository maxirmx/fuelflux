// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "cache_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <random>
#include <sstream>
#include <thread>

using namespace fuelflux;
using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;

namespace {

class MockBackend : public IBackend {
public:
    MOCK_METHOD(bool, Authorize, (const std::string& uid), (override));
    MOCK_METHOD(bool, Deauthorize, (), (override));
    MOCK_METHOD(bool, Refuel, (TankNumber tankNumber, Volume volume), (override));
    MOCK_METHOD(bool, Intake, (TankNumber tankNumber, Volume volume, IntakeDirection direction), (override));
    MOCK_METHOD(bool, RefuelPayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IntakePayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IsAuthorized, (), (const, override));
    MOCK_METHOD(std::string, GetToken, (), (const, override));
    MOCK_METHOD(int, GetRoleId, (), (const, override));
    MOCK_METHOD(double, GetAllowance, (), (const, override));
    MOCK_METHOD(double, GetPrice, (), (const, override));
    MOCK_METHOD((const std::vector<BackendTankInfo>&), GetFuelTanks, (), (const, override));
    MOCK_METHOD((const std::string&), GetLastError, (), (const, override));
    MOCK_METHOD(bool, IsNetworkError, (), (const, override));
    MOCK_METHOD(std::vector<UserCard>, FetchUserCards, (int first, int number), (override));
};

std::string MakeTempDbPath() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);

    std::ostringstream oss;
    oss << "fuelflux_cache_manager_test-"
        << std::hex << dis(gen) << '-' << dis(gen) << ".db";

    return (std::filesystem::temp_directory_path() / oss.str()).string();
}

bool WaitForPopulation(CacheManager& manager,
                       std::chrono::system_clock::time_point previousTime,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (manager.GetLastPopulationTime() > previousTime) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

} // namespace

class CacheManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = MakeTempDbPath();
    }

    void TearDown() override {
        if (std::filesystem::exists(dbPath_)) {
            std::filesystem::remove(dbPath_);
        }
    }

    std::string dbPath_;
};

TEST_F(CacheManagerTest, StartPerformsInitialPaginatedPopulation) {
    auto cache = std::make_shared<UserCache>(dbPath_);
    auto backend = std::make_shared<MockBackend>();

    std::vector<UserCard> firstBatch(100);
    for (int i = 0; i < 100; ++i) {
        firstBatch[i] = {"uid-" + std::to_string(i), i % 2 + 1, static_cast<double>(100 + i)};
    }
    std::vector<UserCard> secondBatch = {
        {"uid-100", 1, 555.0},
        {"uid-101", 2, 777.0},
    };

    {
        InSequence seq;
        EXPECT_CALL(*backend, FetchUserCards(0, 100)).WillOnce(Return(firstBatch));
        EXPECT_CALL(*backend, FetchUserCards(100, 100)).WillOnce(Return(secondBatch));
    }

    CacheManager manager(cache, backend);
    auto before = manager.GetLastPopulationTime();

    EXPECT_TRUE(manager.Start());
    EXPECT_FALSE(manager.Start());

    ASSERT_TRUE(WaitForPopulation(manager, before));
    EXPECT_TRUE(manager.GetLastPopulationSuccess());
    EXPECT_EQ(cache->GetCount(), 102);

    auto last = cache->GetEntry("uid-101");
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->roleId, 2);
    EXPECT_DOUBLE_EQ(last->allowance, 777.0);

    manager.Stop();
}

TEST_F(CacheManagerTest, TriggerPopulationReplacesActiveTableWithFreshData) {
    auto cache = std::make_shared<UserCache>(dbPath_);
    auto backend = std::make_shared<MockBackend>();

    std::vector<UserCard> initial = {{"uid-initial", 1, 100.0}};
    std::vector<UserCard> refreshed = {{"uid-refreshed", 2, 42.5}};

    {
        InSequence seq;
        EXPECT_CALL(*backend, FetchUserCards(0, 100)).WillOnce(Return(initial));
        EXPECT_CALL(*backend, FetchUserCards(0, 100)).WillOnce(Return(refreshed));
    }

    CacheManager manager(cache, backend);

    EXPECT_TRUE(manager.Start());
    ASSERT_TRUE(WaitForPopulation(manager, std::chrono::system_clock::time_point::min()));

    auto firstDone = manager.GetLastPopulationTime();
    manager.TriggerPopulation();
    ASSERT_TRUE(WaitForPopulation(manager, firstDone));

    EXPECT_TRUE(manager.GetLastPopulationSuccess());
    EXPECT_EQ(cache->GetCount(), 1);
    EXPECT_FALSE(cache->GetEntry("uid-initial").has_value());

    auto entry = cache->GetEntry("uid-refreshed");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->roleId, 2);
    EXPECT_DOUBLE_EQ(entry->allowance, 42.5);

    manager.Stop();
}

TEST_F(CacheManagerTest, PopulationFailureKeepsExistingDataAndSetsFailureStatus) {
    auto cache = std::make_shared<UserCache>(dbPath_);
    ASSERT_TRUE(cache->UpdateEntry("uid-existing", 321.0, 1));

    auto backend = std::make_shared<MockBackend>();
    EXPECT_CALL(*backend, FetchUserCards(_, _))
        .WillOnce(Invoke([](int, int) -> std::vector<UserCard> {
            throw std::runtime_error("backend boom");
        }));

    CacheManager manager(cache, backend);
    auto before = manager.GetLastPopulationTime();

    EXPECT_TRUE(manager.Start());
    ASSERT_TRUE(WaitForPopulation(manager, before));

    EXPECT_FALSE(manager.GetLastPopulationSuccess());
    EXPECT_EQ(cache->GetCount(), 1);

    auto preserved = cache->GetEntry("uid-existing");
    ASSERT_TRUE(preserved.has_value());
    EXPECT_DOUBLE_EQ(preserved->allowance, 321.0);

    manager.Stop();
}

TEST_F(CacheManagerTest, UpdateAndDeductOperationsHandleEdgeCases) {
    auto cache = std::make_shared<UserCache>(dbPath_);
    auto backend = std::make_shared<MockBackend>();

    CacheManager manager(cache, backend);

    EXPECT_FALSE(manager.DeductAllowance("missing", 10.0));
    EXPECT_TRUE(manager.UpdateCacheEntry("uid-1", 50.0, 1));
    EXPECT_TRUE(manager.DeductAllowance("uid-1", 12.5));

    auto updated = cache->GetEntry("uid-1");
    ASSERT_TRUE(updated.has_value());
    EXPECT_DOUBLE_EQ(updated->allowance, 37.5);

    EXPECT_TRUE(manager.DeductAllowance("uid-1", 999.0));
    auto clamped = cache->GetEntry("uid-1");
    ASSERT_TRUE(clamped.has_value());
    EXPECT_DOUBLE_EQ(clamped->allowance, 0.0);

    CacheManager nullCacheManager(nullptr, backend);
    EXPECT_FALSE(nullCacheManager.UpdateCacheEntry("uid-2", 10.0, 2));
    EXPECT_FALSE(nullCacheManager.DeductAllowance("uid-2", 5.0));
}

TEST_F(CacheManagerTest, NullDependenciesCauseFailedPopulation) {
    auto cache = std::make_shared<UserCache>(dbPath_);

    CacheManager noBackend(cache, nullptr);
    EXPECT_TRUE(noBackend.Start());
    ASSERT_TRUE(WaitForPopulation(noBackend, std::chrono::system_clock::time_point::min()));
    EXPECT_FALSE(noBackend.GetLastPopulationSuccess());
    noBackend.Stop();

    auto backend = std::make_shared<MockBackend>();
    CacheManager noCache(nullptr, backend);
    EXPECT_TRUE(noCache.Start());
    ASSERT_TRUE(WaitForPopulation(noCache, std::chrono::system_clock::time_point::min()));
    EXPECT_FALSE(noCache.GetLastPopulationSuccess());
    noCache.Stop();
}
