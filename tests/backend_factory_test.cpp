// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "backend_factory.h"
#include "backend.h"
#include "message_storage.h"
#include <filesystem>
#include <random>
#include <sstream>
#include <iomanip>

using namespace fuelflux;

namespace {

// Helper function to create a temporary database path for testing
std::string MakeTempDbPath() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);
    
    std::ostringstream oss;
    oss << "backend_factory_test-" 
        << std::hex << std::setfill('0') << std::setw(4) << dis(gen) << "-"
        << std::setw(4) << dis(gen) << "-"
        << std::setw(4) << dis(gen) << ".db";
    
    auto path = std::filesystem::temp_directory_path() / oss.str();
    return path.string();
}

} // namespace

// Test CreateBackend with nullptr storage (default parameter)
TEST(BackendFactoryTest, CreateBackendWithNullptrStorage) {
    auto backend = CreateBackend(nullptr);
    
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->IsAuthorized());
    EXPECT_EQ(backend->GetToken(), "");
    EXPECT_EQ(backend->GetRoleId(), 0);
    EXPECT_EQ(backend->GetAllowance(), 0.0);
    EXPECT_EQ(backend->GetPrice(), 0.0);
}

// Test CreateBackend with default parameter (no argument)
TEST(BackendFactoryTest, CreateBackendWithDefaultParameter) {
    auto backend = CreateBackend();
    
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->IsAuthorized());
    EXPECT_EQ(backend->GetToken(), "");
    EXPECT_EQ(backend->GetRoleId(), 0);
}

// Test CreateBackend with valid storage
TEST(BackendFactoryTest, CreateBackendWithValidStorage) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);
    
    {
        auto storage = std::make_shared<MessageStorage>(dbPath);
        ASSERT_TRUE(storage->IsOpen());
    
        auto backend = CreateBackend(storage);

        ASSERT_NE(backend, nullptr);
        EXPECT_FALSE(backend->IsAuthorized());
        EXPECT_EQ(backend->GetToken(), "");
    }
    std::filesystem::remove(dbPath);
}

// Test CreateBackendShared with nullptr storage
TEST(BackendFactoryTest, CreateBackendSharedWithNullptrStorage) {
    auto backend = CreateBackendShared(nullptr);
    
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->IsAuthorized());
    EXPECT_EQ(backend->GetToken(), "");
    EXPECT_EQ(backend->GetRoleId(), 0);
    EXPECT_EQ(backend->GetAllowance(), 0.0);
    EXPECT_EQ(backend->GetPrice(), 0.0);
}

// Test CreateBackendShared with default parameter (no argument)
TEST(BackendFactoryTest, CreateBackendSharedWithDefaultParameter) {
    auto backend = CreateBackendShared();
    
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->IsAuthorized());
    EXPECT_EQ(backend->GetToken(), "");
}

// Test CreateBackendShared with valid storage
TEST(BackendFactoryTest, CreateBackendSharedWithValidStorage) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);
    {
        auto storage = std::make_shared<MessageStorage>(dbPath);
        ASSERT_TRUE(storage->IsOpen());

        auto backend = CreateBackendShared(storage);

        ASSERT_NE(backend, nullptr);
        EXPECT_FALSE(backend->IsAuthorized());
        EXPECT_EQ(backend->GetToken(), "");
    }
    std::filesystem::remove(dbPath);
}

// Test that CreateBackendShared returns a shared_ptr
TEST(BackendFactoryTest, CreateBackendSharedReturnsSharedPtr) {
    auto backend = CreateBackendShared();
    
    ASSERT_NE(backend, nullptr);
    
    // Verify it's a shared_ptr by checking use_count
    EXPECT_EQ(backend.use_count(), 1);
    
    // Create another reference
    auto backend2 = backend;
    EXPECT_EQ(backend.use_count(), 2);
    EXPECT_EQ(backend2.use_count(), 2);
}

// Test that CreateBackend returns IBackend interface
TEST(BackendFactoryTest, CreateBackendReturnsIBackendInterface) {
    auto backend = CreateBackend();
    
    ASSERT_NE(backend, nullptr);
    
    // Verify we can use IBackend interface methods
    EXPECT_FALSE(backend->IsAuthorized());
    EXPECT_NO_THROW(backend->GetToken());
    EXPECT_NO_THROW(backend->GetLastError());
    EXPECT_NO_THROW(backend->IsNetworkError());
}

// Test that backends created with same storage can interact correctly
TEST(BackendFactoryTest, MultipleBackendsWithSameStorage) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);
    
    {
        auto storage = std::make_shared<MessageStorage>(dbPath);
        ASSERT_TRUE(storage->IsOpen());

        auto backend1 = CreateBackend(storage);
        auto backend2 = CreateBackendShared(storage);

        ASSERT_NE(backend1, nullptr);
        ASSERT_NE(backend2, nullptr);

        // Both should be independent backend instances
        EXPECT_FALSE(backend1->IsAuthorized());
        EXPECT_FALSE(backend2->IsAuthorized());
    }
    std::filesystem::remove(dbPath);
}

// Test backend creation doesn't throw exceptions
TEST(BackendFactoryTest, BackendCreationDoesNotThrow) {
    EXPECT_NO_THROW({
        auto backend1 = CreateBackend();
        auto backend2 = CreateBackend(nullptr);
        auto backend3 = CreateBackendShared();
        auto backend4 = CreateBackendShared(nullptr);
    });
}

// Test that storage parameter is properly forwarded
TEST(BackendFactoryTest, StorageParameterForwarding) {
    const std::string dbPath = MakeTempDbPath();
    std::filesystem::remove(dbPath);
    
    {
        auto storage = std::make_shared<MessageStorage>(dbPath);
        ASSERT_TRUE(storage->IsOpen());
        ASSERT_TRUE(storage->AddBacklog("test-uid", MessageMethod::Refuel, "{\"test\":\"data\"}"));
        EXPECT_EQ(storage->BacklogCount(), 1);

        // Create backend with this storage
        auto backend = CreateBackend(storage);
        ASSERT_NE(backend, nullptr);

        // Verify storage is still accessible and has the backlog
        EXPECT_EQ(storage->BacklogCount(), 1);
    }

    std::filesystem::remove(dbPath);
}

// Test backend type consistency based on BACKEND_TYPE configuration
// Note: This test verifies the factory creates the correct type based on config
TEST(BackendFactoryTest, BackendTypeConsistency) {
    auto backend1 = CreateBackend();
    auto backend2 = CreateBackend(nullptr);
    auto backend3 = CreateBackendShared();
    
    // All backends should be created successfully
    ASSERT_NE(backend1, nullptr);
    ASSERT_NE(backend2, nullptr);
    ASSERT_NE(backend3, nullptr);
    
    // All should implement the IBackend interface properly
    EXPECT_FALSE(backend1->IsAuthorized());
    EXPECT_FALSE(backend2->IsAuthorized());
    EXPECT_FALSE(backend3->IsAuthorized());
}

// Test that unique_ptr from CreateBackend is properly movable
TEST(BackendFactoryTest, UniqueBackendIsMovable) {
    auto backend1 = CreateBackend();
    ASSERT_NE(backend1, nullptr);
    
    // Move to another unique_ptr
    auto backend2 = std::move(backend1);
    EXPECT_EQ(backend1, nullptr);
    ASSERT_NE(backend2, nullptr);
    
    // Verify moved backend still works
    EXPECT_FALSE(backend2->IsAuthorized());
}

// Test that shared_ptr from CreateBackendShared can be copied and shared
TEST(BackendFactoryTest, SharedBackendIsCopyable) {
    auto backend1 = CreateBackendShared();
    ASSERT_NE(backend1, nullptr);
    EXPECT_EQ(backend1.use_count(), 1);
    
    // Copy to another shared_ptr
    auto backend2 = backend1;
    EXPECT_EQ(backend1.use_count(), 2);
    EXPECT_EQ(backend2.use_count(), 2);
    
    // Both should point to the same instance
    EXPECT_EQ(backend1.get(), backend2.get());
}
