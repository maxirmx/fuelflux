// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "message_storage.h"

#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>

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

    std::filesystem::remove(dbPath);
}
