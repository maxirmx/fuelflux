// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "user_cache.h"
#include <gtest/gtest.h>
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
    oss << "fuelflux_user_cache_test-" 
        << std::hex << std::setfill('0') << std::setw(4) << dis(gen) << "-"
        << std::setw(4) << dis(gen) << "-"
        << std::setw(4) << dis(gen) << ".db";
    
    auto path = std::filesystem::temp_directory_path() / oss.str();
    return path.string();
}

} // namespace

class UserCacheTest : public ::testing::Test {
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

// Test basic construction and opening
TEST_F(UserCacheTest, ConstructionAndOpening) {
    UserCache cache(dbPath_);
    EXPECT_TRUE(cache.IsOpen());
}

// Test in-memory database
TEST_F(UserCacheTest, InMemoryDatabase) {
    UserCache cache(":memory:");
    EXPECT_TRUE(cache.IsOpen());
}

// Test GetEntry returns nullopt for non-existent entry
TEST_F(UserCacheTest, GetEntryNotFound) {
    UserCache cache(dbPath_);
    auto entry = cache.GetEntry("non-existent-uid");
    EXPECT_FALSE(entry.has_value());
}

// Test UpdateEntry and GetEntry
TEST_F(UserCacheTest, UpdateAndGetEntry) {
    UserCache cache(dbPath_);
    
    EXPECT_TRUE(cache.UpdateEntry("uid-123", 100.5, 1));
    
    auto entry = cache.GetEntry("uid-123");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->uid, "uid-123");
    EXPECT_DOUBLE_EQ(entry->allowance, 100.5);
    EXPECT_EQ(entry->roleId, 1);
}

// Test updating existing entry
TEST_F(UserCacheTest, UpdateExistingEntry) {
    UserCache cache(dbPath_);
    
    EXPECT_TRUE(cache.UpdateEntry("uid-123", 100.0, 1));
    EXPECT_TRUE(cache.UpdateEntry("uid-123", 50.0, 2));
    
    auto entry = cache.GetEntry("uid-123");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->uid, "uid-123");
    EXPECT_DOUBLE_EQ(entry->allowance, 50.0);
    EXPECT_EQ(entry->roleId, 2);
}

// Test GetCount
TEST_F(UserCacheTest, GetCount) {
    UserCache cache(dbPath_);
    
    EXPECT_EQ(cache.GetCount(), 0);
    
    cache.UpdateEntry("uid-1", 100.0, 1);
    EXPECT_EQ(cache.GetCount(), 1);
    
    cache.UpdateEntry("uid-2", 200.0, 2);
    EXPECT_EQ(cache.GetCount(), 2);
    
    cache.UpdateEntry("uid-1", 150.0, 1); // Update existing
    EXPECT_EQ(cache.GetCount(), 2);
}

// Test BeginPopulation and AbortPopulation
TEST_F(UserCacheTest, BeginAndAbortPopulation) {
    UserCache cache(dbPath_);
    
    // Add some initial data
    cache.UpdateEntry("uid-1", 100.0, 1);
    EXPECT_EQ(cache.GetCount(), 1);
    
    // Begin population
    EXPECT_TRUE(cache.BeginPopulation());
    
    // Abort without committing
    cache.AbortPopulation();
    
    // Original data should still be there
    EXPECT_EQ(cache.GetCount(), 1);
    auto entry = cache.GetEntry("uid-1");
    ASSERT_TRUE(entry.has_value());
    EXPECT_DOUBLE_EQ(entry->allowance, 100.0);
}

// Test flip/flop mechanism with CommitPopulation
TEST_F(UserCacheTest, FlipFlopMechanism) {
    UserCache cache(dbPath_);
    
    // Add initial data to active table
    cache.UpdateEntry("uid-1", 100.0, 1);
    cache.UpdateEntry("uid-2", 200.0, 2);
    EXPECT_EQ(cache.GetCount(), 2);
    
    // Begin population (writes to standby table)
    EXPECT_TRUE(cache.BeginPopulation());
    
    // Add new data to standby table
    EXPECT_TRUE(cache.AddPopulationEntry("uid-3", 300.0, 1));
    EXPECT_TRUE(cache.AddPopulationEntry("uid-4", 400.0, 2));
    
    // Active table should still have old data
    EXPECT_EQ(cache.GetCount(), 2);
    
    // Commit population (swap tables)
    EXPECT_TRUE(cache.CommitPopulation());
    
    // Active table should now have new data
    EXPECT_EQ(cache.GetCount(), 2);
    
    auto entry3 = cache.GetEntry("uid-3");
    ASSERT_TRUE(entry3.has_value());
    EXPECT_DOUBLE_EQ(entry3->allowance, 300.0);
    
    auto entry4 = cache.GetEntry("uid-4");
    ASSERT_TRUE(entry4.has_value());
    EXPECT_DOUBLE_EQ(entry4->allowance, 400.0);
    
    // Old data should be gone
    auto entry1 = cache.GetEntry("uid-1");
    EXPECT_FALSE(entry1.has_value());
}

// Test that BeginPopulation clears standby table
TEST_F(UserCacheTest, BeginPopulationClearsStandbyTable) {
    UserCache cache(dbPath_);
    
    // First population
    EXPECT_TRUE(cache.BeginPopulation());
    EXPECT_TRUE(cache.AddPopulationEntry("uid-1", 100.0, 1));
    EXPECT_TRUE(cache.CommitPopulation());
    
    EXPECT_EQ(cache.GetCount(), 1);
    
    // Second population - should start fresh
    EXPECT_TRUE(cache.BeginPopulation());
    EXPECT_TRUE(cache.AddPopulationEntry("uid-2", 200.0, 2));
    EXPECT_TRUE(cache.CommitPopulation());
    
    // Should only have uid-2, not uid-1
    EXPECT_EQ(cache.GetCount(), 1);
    
    auto entry2 = cache.GetEntry("uid-2");
    ASSERT_TRUE(entry2.has_value());
    
    auto entry1 = cache.GetEntry("uid-1");
    EXPECT_FALSE(entry1.has_value());
}

// Test cannot begin population twice
TEST_F(UserCacheTest, CannotBeginPopulationTwice) {
    UserCache cache(dbPath_);
    
    EXPECT_TRUE(cache.BeginPopulation());
    EXPECT_FALSE(cache.BeginPopulation()); // Should fail
    
    cache.AbortPopulation();
    EXPECT_TRUE(cache.BeginPopulation()); // Should succeed after abort
}

// Test cannot add population entry without beginning
TEST_F(UserCacheTest, CannotAddPopulationEntryWithoutBeginning) {
    UserCache cache(dbPath_);
    
    EXPECT_FALSE(cache.AddPopulationEntry("uid-1", 100.0, 1));
}

// Test cannot commit without beginning
TEST_F(UserCacheTest, CannotCommitWithoutBeginning) {
    UserCache cache(dbPath_);
    
    EXPECT_FALSE(cache.CommitPopulation());
}

// Test population with many entries
TEST_F(UserCacheTest, PopulationWithManyEntries) {
    UserCache cache(dbPath_);
    
    EXPECT_TRUE(cache.BeginPopulation());
    
    // Add 100 entries
    for (int i = 0; i < 100; i++) {
        std::string uid = "uid-" + std::to_string(i);
        EXPECT_TRUE(cache.AddPopulationEntry(uid, 100.0 + i, i % 3 + 1));
    }
    
    EXPECT_TRUE(cache.CommitPopulation());
    
    // Verify count
    EXPECT_EQ(cache.GetCount(), 100);
    
    // Verify some random entries
    auto entry0 = cache.GetEntry("uid-0");
    ASSERT_TRUE(entry0.has_value());
    EXPECT_DOUBLE_EQ(entry0->allowance, 100.0);
    EXPECT_EQ(entry0->roleId, 1);
    
    auto entry50 = cache.GetEntry("uid-50");
    ASSERT_TRUE(entry50.has_value());
    EXPECT_DOUBLE_EQ(entry50->allowance, 150.0);
    EXPECT_EQ(entry50->roleId, 3);
    
    auto entry99 = cache.GetEntry("uid-99");
    ASSERT_TRUE(entry99.has_value());
    EXPECT_DOUBLE_EQ(entry99->allowance, 199.0);
    EXPECT_EQ(entry99->roleId, 1);
}

// Test persistence across instances
TEST_F(UserCacheTest, PersistenceAcrossInstances) {
    {
        UserCache cache(dbPath_);
        cache.UpdateEntry("uid-1", 100.0, 1);
        cache.UpdateEntry("uid-2", 200.0, 2);
    }
    
    // Create a new instance
    {
        UserCache cache(dbPath_);
        EXPECT_EQ(cache.GetCount(), 2);
        
        auto entry1 = cache.GetEntry("uid-1");
        ASSERT_TRUE(entry1.has_value());
        EXPECT_DOUBLE_EQ(entry1->allowance, 100.0);
        
        auto entry2 = cache.GetEntry("uid-2");
        ASSERT_TRUE(entry2.has_value());
        EXPECT_DOUBLE_EQ(entry2->allowance, 200.0);
    }
}

// Test active table state persists
TEST_F(UserCacheTest, ActiveTableStatePersists) {
    {
        UserCache cache(dbPath_);
        
        // Add data to table A
        cache.UpdateEntry("uid-a", 100.0, 1);
        
        // Flip to table B
        EXPECT_TRUE(cache.BeginPopulation());
        EXPECT_TRUE(cache.AddPopulationEntry("uid-b", 200.0, 2));
        EXPECT_TRUE(cache.CommitPopulation());
    }
    
    // Create a new instance - should remember table B is active
    {
        UserCache cache(dbPath_);
        
        auto entryB = cache.GetEntry("uid-b");
        ASSERT_TRUE(entryB.has_value());
        EXPECT_DOUBLE_EQ(entryB->allowance, 200.0);
        
        auto entryA = cache.GetEntry("uid-a");
        EXPECT_FALSE(entryA.has_value());
    }
}

// Test zero and negative allowance
TEST_F(UserCacheTest, ZeroAndNegativeAllowance) {
    UserCache cache(dbPath_);
    
    cache.UpdateEntry("uid-zero", 0.0, 1);
    cache.UpdateEntry("uid-negative", -50.0, 1);
    
    auto entryZero = cache.GetEntry("uid-zero");
    ASSERT_TRUE(entryZero.has_value());
    EXPECT_DOUBLE_EQ(entryZero->allowance, 0.0);
    
    auto entryNeg = cache.GetEntry("uid-negative");
    ASSERT_TRUE(entryNeg.has_value());
    EXPECT_DOUBLE_EQ(entryNeg->allowance, -50.0);
}

// Test special characters in UID
TEST_F(UserCacheTest, SpecialCharactersInUID) {
    UserCache cache(dbPath_);
    
    std::string specialUID = "uid-with-special!@#$%^&*()_+-=[]{}|;':\",./<>?";
    cache.UpdateEntry(specialUID, 100.0, 1);
    
    auto entry = cache.GetEntry(specialUID);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->uid, specialUID);
}

// Test very long UID
TEST_F(UserCacheTest, VeryLongUID) {
    UserCache cache(dbPath_);
    
    std::string longUID(1000, 'x');
    cache.UpdateEntry(longUID, 100.0, 1);
    
    auto entry = cache.GetEntry(longUID);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->uid, longUID);
}

// Test various role IDs
TEST_F(UserCacheTest, VariousRoleIDs) {
    UserCache cache(dbPath_);
    
    cache.UpdateEntry("uid-0", 100.0, 0);
    cache.UpdateEntry("uid-1", 100.0, 1);
    cache.UpdateEntry("uid-2", 100.0, 2);
    cache.UpdateEntry("uid-999", 100.0, 999);
    cache.UpdateEntry("uid-negative", 100.0, -1);
    
    auto entry0 = cache.GetEntry("uid-0");
    ASSERT_TRUE(entry0.has_value());
    EXPECT_EQ(entry0->roleId, 0);
    
    auto entry999 = cache.GetEntry("uid-999");
    ASSERT_TRUE(entry999.has_value());
    EXPECT_EQ(entry999->roleId, 999);
    
    auto entryNeg = cache.GetEntry("uid-negative");
    ASSERT_TRUE(entryNeg.has_value());
    EXPECT_EQ(entryNeg->roleId, -1);
}

// Test that updates during population are preserved after flip
TEST_F(UserCacheTest, UpdatesDuringPopulationArePreserved) {
    UserCache cache(dbPath_);
    
    // Add initial data to active table
    cache.UpdateEntry("uid-1", 100.0, 1);
    cache.UpdateEntry("uid-2", 200.0, 2);
    EXPECT_EQ(cache.GetCount(), 2);
    
    // Begin population (prepares standby table)
    EXPECT_TRUE(cache.BeginPopulation());
    
    // Add new data to standby table
    EXPECT_TRUE(cache.AddPopulationEntry("uid-3", 300.0, 1));
    EXPECT_TRUE(cache.AddPopulationEntry("uid-4", 400.0, 2));
    
    // Simulate a refuel update during population - this should update BOTH tables
    EXPECT_TRUE(cache.UpdateEntry("uid-1", 50.0, 1));  // Deduct 50 from uid-1
    
    // Commit population (swap tables)
    EXPECT_TRUE(cache.CommitPopulation());
    
    // Verify the update to uid-1 is preserved after the flip
    auto entry1 = cache.GetEntry("uid-1");
    ASSERT_TRUE(entry1.has_value());
    EXPECT_DOUBLE_EQ(entry1->allowance, 50.0);  // Should have the updated value, not original 100.0
    EXPECT_EQ(entry1->roleId, 1);
    
    // Verify new entries are present
    auto entry3 = cache.GetEntry("uid-3");
    ASSERT_TRUE(entry3.has_value());
    EXPECT_DOUBLE_EQ(entry3->allowance, 300.0);
    
    auto entry4 = cache.GetEntry("uid-4");
    ASSERT_TRUE(entry4.has_value());
    EXPECT_DOUBLE_EQ(entry4->allowance, 400.0);
}
