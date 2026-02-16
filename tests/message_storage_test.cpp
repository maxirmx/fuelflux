// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "message_storage.h"
#include "types.h"

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

TEST(MessageStorageTest, UserCacheAddAndGet) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        
        // Initially empty
        EXPECT_EQ(storage.CacheCount(), 0);
        
        // Add entries to staging
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 100.0, 1));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user2", 200.0, 2));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user3", 0.0, 1));
        
        // Active cache should still be empty
        EXPECT_EQ(storage.CacheCount(), 0);
        EXPECT_FALSE(storage.GetCacheEntry("user1").has_value());
        
        // Swap staging to active
        EXPECT_TRUE(storage.SwapCache());
        EXPECT_EQ(storage.CacheCount(), 3);
        
        // Verify entries
        auto entry1 = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry1.has_value());
        EXPECT_EQ(entry1->uid, "user1");
        EXPECT_DOUBLE_EQ(entry1->allowance, 100.0);
        EXPECT_EQ(entry1->roleId, 1);
        
        auto entry2 = storage.GetCacheEntry("user2");
        ASSERT_TRUE(entry2.has_value());
        EXPECT_EQ(entry2->uid, "user2");
        EXPECT_DOUBLE_EQ(entry2->allowance, 200.0);
        EXPECT_EQ(entry2->roleId, 2);
        
        auto entry3 = storage.GetCacheEntry("user3");
        ASSERT_TRUE(entry3.has_value());
        EXPECT_EQ(entry3->uid, "user3");
        EXPECT_DOUBLE_EQ(entry3->allowance, 0.0);
        EXPECT_EQ(entry3->roleId, 1);
        
        // Non-existent entry
        EXPECT_FALSE(storage.GetCacheEntry("user999").has_value());
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, UserCacheUpdate) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        
        // Add initial entry
        EXPECT_TRUE(storage.UpdateCacheEntry("user1", 100.0, 1));
        EXPECT_EQ(storage.CacheCount(), 1);
        
        auto entry = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry.has_value());
        EXPECT_DOUBLE_EQ(entry->allowance, 100.0);
        EXPECT_EQ(entry->roleId, 1);
        
        // Update existing entry
        EXPECT_TRUE(storage.UpdateCacheEntry("user1", 50.0, 1));
        EXPECT_EQ(storage.CacheCount(), 1);
        
        entry = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry.has_value());
        EXPECT_DOUBLE_EQ(entry->allowance, 50.0);
        EXPECT_EQ(entry->roleId, 1);
        
        // Add new entry via update
        EXPECT_TRUE(storage.UpdateCacheEntry("user2", 300.0, 2));
        EXPECT_EQ(storage.CacheCount(), 2);
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, UserCacheFlipFlop) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        
        // Initial cache
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 100.0, 1));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user2", 200.0, 2));
        EXPECT_TRUE(storage.SwapCache());
        EXPECT_EQ(storage.CacheCount(), 2);
        
        // Populate new staging with different data
        EXPECT_TRUE(storage.AddCacheEntryStaging("user3", 300.0, 1));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user4", 400.0, 2));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 150.0, 1)); // Updated user1
        
        // Old cache should still be active
        EXPECT_EQ(storage.CacheCount(), 2);
        auto entry1 = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry1.has_value());
        EXPECT_DOUBLE_EQ(entry1->allowance, 100.0);
        EXPECT_FALSE(storage.GetCacheEntry("user3").has_value());
        
        // Swap to new cache
        EXPECT_TRUE(storage.SwapCache());
        EXPECT_EQ(storage.CacheCount(), 3);
        
        // Verify new cache
        entry1 = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry1.has_value());
        EXPECT_DOUBLE_EQ(entry1->allowance, 150.0);
        
        auto entry3 = storage.GetCacheEntry("user3");
        ASSERT_TRUE(entry3.has_value());
        EXPECT_DOUBLE_EQ(entry3->allowance, 300.0);
        
        auto entry4 = storage.GetCacheEntry("user4");
        ASSERT_TRUE(entry4.has_value());
        EXPECT_DOUBLE_EQ(entry4->allowance, 400.0);
        
        // Old user2 should be gone
        EXPECT_FALSE(storage.GetCacheEntry("user2").has_value());
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, UserCacheClearStaging) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        
        // Add to staging
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 100.0, 1));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user2", 200.0, 2));
        
        // Clear staging
        EXPECT_TRUE(storage.ClearCacheStaging());
        
        // Swap should result in empty cache
        EXPECT_TRUE(storage.SwapCache());
        EXPECT_EQ(storage.CacheCount(), 0);
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, UserCachePersistence) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 100.0, 1));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user2", 200.0, 2));
        EXPECT_TRUE(storage.SwapCache());
        EXPECT_EQ(storage.CacheCount(), 2);
    }

    // Reopen database
    {
        MessageStorage storage(dbPath);
        EXPECT_EQ(storage.CacheCount(), 2);
        
        auto entry1 = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry1.has_value());
        EXPECT_EQ(entry1->uid, "user1");
        EXPECT_DOUBLE_EQ(entry1->allowance, 100.0);
        EXPECT_EQ(entry1->roleId, 1);
        
        auto entry2 = storage.GetCacheEntry("user2");
        ASSERT_TRUE(entry2.has_value());
        EXPECT_EQ(entry2->uid, "user2");
        EXPECT_DOUBLE_EQ(entry2->allowance, 200.0);
        EXPECT_EQ(entry2->roleId, 2);
    }

    std::filesystem::remove(dbPath);
}

TEST(MessageStorageTest, UserCacheReplaceInStaging) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);

    {
        MessageStorage storage(dbPath);
        
        // Add same user multiple times to staging
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 100.0, 1));
        EXPECT_TRUE(storage.AddCacheEntryStaging("user1", 200.0, 2)); // Should replace
        
        EXPECT_TRUE(storage.SwapCache());
        EXPECT_EQ(storage.CacheCount(), 1);
        
        auto entry = storage.GetCacheEntry("user1");
        ASSERT_TRUE(entry.has_value());
        EXPECT_DOUBLE_EQ(entry->allowance, 200.0);
        EXPECT_EQ(entry->roleId, 2);
    }

    std::filesystem::remove(dbPath);
}
