// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "message_storage.h"
#include "user_cache.h"
#include <sqlite3.h>

#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>
#include <limits>

using namespace fuelflux;

namespace {

std::string MakeTempDbPath() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);
    
    std::ostringstream oss;
    oss << "fuelflux_storage_test-" 
        << std::hex << std::setfill('0') << std::setw(4) << dis(gen) << "-"
        << std::setw(4) << dis(gen) << "-"
        << std::setw(4) << dis(gen) << ".db";
    
    auto path = std::filesystem::temp_directory_path() / oss.str();
    return path.string();
}

} // namespace

TEST(MessageStorageTest, PersistsDataAcrossSessions) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        EXPECT_TRUE(storage.IsOpen());
        EXPECT_TRUE(storage.AddBacklog("uid-1", MessageMethod::Refuel, "{\"TankNumber\":1}"));
        EXPECT_TRUE(storage.AddDeadMessage("uid-2", MessageMethod::Intake, "{\"TankNumber\":2}"));
        EXPECT_EQ(storage.BacklogCount(), 1);
        EXPECT_EQ(storage.DeadMessageCount(), 1);
    }

    {
        MessageStorage storage(dbPath);
        EXPECT_TRUE(storage.IsOpen());
        EXPECT_EQ(storage.BacklogCount(), 1);
        EXPECT_EQ(storage.DeadMessageCount(), 1);
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, FetchesAndRemovesBacklogInOrder) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        ASSERT_TRUE(storage.AddBacklog("uid-1", MessageMethod::Refuel, "{\"TankNumber\":1}"));
        ASSERT_TRUE(storage.AddBacklog("uid-2", MessageMethod::Intake, "{\"TankNumber\":2}"));

        auto first = storage.GetNextBacklog();
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(first->uid, "uid-1");
        EXPECT_EQ(first->method, MessageMethod::Refuel);

        EXPECT_TRUE(storage.RemoveBacklog(first->id));
        EXPECT_EQ(storage.BacklogCount(), 1);

        auto second = storage.GetNextBacklog();
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->uid, "uid-2");
        EXPECT_EQ(second->method, MessageMethod::Intake);
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, CalibrationCoefficientDefaultsAndPersists) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        const auto initial = storage.GetCalibrationCoefficient();
        ASSERT_TRUE(initial.has_value());
        EXPECT_DOUBLE_EQ(*initial, 1.0);
        EXPECT_TRUE(storage.SetCalibrationCoefficient(0.5));
    }

    {
        MessageStorage storage(dbPath);
        const auto persisted = storage.GetCalibrationCoefficient();
        ASSERT_TRUE(persisted.has_value());
        EXPECT_DOUBLE_EQ(*persisted, 0.5);
        EXPECT_TRUE(storage.SetCalibrationCoefficient(1.5));
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, CalibrationCoefficientRejectsInvalidValues) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        EXPECT_FALSE(storage.SetCalibrationCoefficient(0.499));
        EXPECT_FALSE(storage.SetCalibrationCoefficient(1.501));
        EXPECT_FALSE(storage.SetCalibrationCoefficient(
            std::numeric_limits<double>::quiet_NaN()));
        const auto value = storage.GetCalibrationCoefficient();
        ASSERT_TRUE(value.has_value());
        EXPECT_DOUBLE_EQ(*value, 1.0);
    }

    std::filesystem::remove(dbPath);
}

TEST(ReceiptStorageTest, PromotionAndAllowanceApplicationAreIdempotent) {
    MessageStorage storage(":memory:");
    UserCache cache(":memory:");
    ASSERT_TRUE(cache.UpdateEntry("uid", 100, 1));
    auto receipt = storage.BeginReceipt("uid", MessageMethod::Refuel, "{}", 7.5, true);
    ASSERT_TRUE(receipt);
    EXPECT_TRUE(storage.RetainReceipt(*receipt, false));
    EXPECT_TRUE(storage.RetainReceipt(*receipt, false));
    EXPECT_EQ(storage.BacklogCount(), 1);
    EXPECT_TRUE(cache.DeductAllowanceOnce(*receipt, "uid", 7.5));
    EXPECT_TRUE(cache.DeductAllowanceOnce(*receipt, "uid", 7.5));
    EXPECT_DOUBLE_EQ(cache.GetEntry("uid")->allowance, 92.5);
    EXPECT_TRUE(storage.AccountReceipt(*receipt));
    ASSERT_TRUE(storage.PendingReceipts());
    EXPECT_TRUE(storage.PendingReceipts()->empty());
}

TEST(ReceiptStorageTest, FailedDebitDoesNotConsumeReceiptId) {
    UserCache cache(":memory:");
    EXPECT_FALSE(cache.DeductAllowanceOnce("receipt", "missing", 5));
    ASSERT_TRUE(cache.UpdateEntry("missing", 100, 1));
    EXPECT_TRUE(cache.DeductAllowanceOnce("receipt", "missing", 5));
    EXPECT_DOUBLE_EQ(cache.GetEntry("missing")->allowance, 95);
}

TEST(ReceiptStorageTest, DebitUpdatesPopulationTablesAtomically) {
    UserCache cache(":memory:");
    ASSERT_TRUE(cache.UpdateEntry("uid", 100, 1));
    ASSERT_TRUE(cache.BeginPopulation());
    ASSERT_TRUE(cache.DeductAllowanceOnce("receipt", "uid", 5));
    ASSERT_TRUE(cache.AddPopulationEntry("uid", 100, 1)); // delayed stale server batch
    ASSERT_TRUE(cache.CommitPopulation());
    EXPECT_DOUBLE_EQ(cache.GetEntry("uid")->allowance, 95);
    EXPECT_TRUE(cache.DeductAllowanceOnce("receipt", "uid", 5));
    EXPECT_DOUBLE_EQ(cache.GetEntry("uid")->allowance, 95);
}

TEST(ReceiptStorageTest, PopulationDoesNotReplayDebitsAlreadyIncludedByServer) {
    UserCache cache(":memory:");
    ASSERT_TRUE(cache.UpdateEntry("uid", 100, 1));
    ASSERT_TRUE(cache.BeginPopulation());
    ASSERT_TRUE(cache.AddPopulationEntry("uid", 100, 1));
    ASSERT_TRUE(cache.DeductAllowanceOnce("first", "uid", 5));
    ASSERT_TRUE(cache.DeductAllowanceOnce("second", "uid", 3));
    ASSERT_TRUE(cache.AddPopulationEntry("uid", 92, 1));
    ASSERT_TRUE(cache.AddPopulationEntry("other", 200, 2));
    ASSERT_TRUE(cache.CommitPopulation());
    EXPECT_DOUBLE_EQ(cache.GetEntry("uid")->allowance, 92);
    EXPECT_DOUBLE_EQ(cache.GetEntry("other")->allowance, 200);
    ASSERT_TRUE(cache.BeginPopulation());
    ASSERT_TRUE(cache.AddPopulationEntry("uid", 92, 1));
    ASSERT_TRUE(cache.CommitPopulation());
    EXPECT_DOUBLE_EQ(cache.GetEntry("uid")->allowance, 92);
    EXPECT_TRUE(cache.DeductAllowanceOnce("first", "uid", 5));
    EXPECT_DOUBLE_EQ(cache.GetEntry("uid")->allowance, 92);
}
