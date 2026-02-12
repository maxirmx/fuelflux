// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include "cares_resolver.h"

#ifdef USE_CARES

namespace fuelflux {
namespace {

// Global test environment that initializes c-ares library before all tests
class CaresEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (!InitializeCaresLibrary()) {
            FAIL() << "Failed to initialize c-ares library";
        }
    }
    
    void TearDown() override {
        CleanupCaresLibrary();
    }
};

// Register the environment (will be called once before all tests)
// Google Test takes ownership, so we just need the side effect of registration
namespace {
[[maybe_unused]] const auto g_cares_env_init = []() {
    ::testing::AddGlobalTestEnvironment(new CaresEnvironment);
    return true;
}();
} // anonymous namespace

class CaresResolverTest : public ::testing::Test {
protected:
    CaresResolver resolver;
};

// Test resolving localhost
TEST_F(CaresResolverTest, ResolvesLocalhost) {
    std::string ip = resolver.Resolve("localhost");
    EXPECT_FALSE(ip.empty());
    // localhost should resolve to 127.0.0.1
    EXPECT_EQ(ip, "127.0.0.1");
}

// Test resolving an IP address (should return as-is)
TEST_F(CaresResolverTest, ReturnsIPv4AddressAsIs) {
    std::string ip = resolver.Resolve("8.8.8.8");
    EXPECT_EQ(ip, "8.8.8.8");
}

// Test resolving another IPv4 address
TEST_F(CaresResolverTest, ReturnsAnotherIPv4AddressAsIs) {
    std::string ip = resolver.Resolve("192.168.1.1");
    EXPECT_EQ(ip, "192.168.1.1");
}

// Test resolving an IPv6 address (should return as-is)
TEST_F(CaresResolverTest, ReturnsIPv6AddressAsIs) {
    std::string ip = resolver.Resolve("::1");
    EXPECT_EQ(ip, "::1");
}

// Test resolving with empty hostname
TEST_F(CaresResolverTest, ReturnsEmptyForEmptyHostname) {
    std::string ip = resolver.Resolve("");
    EXPECT_TRUE(ip.empty());
}

// Test that resolver can be used multiple times
TEST_F(CaresResolverTest, CanBeReusedMultipleTimes) {
    std::string ip1 = resolver.Resolve("localhost");
    EXPECT_FALSE(ip1.empty());
    
    std::string ip2 = resolver.Resolve("8.8.8.8");
    EXPECT_EQ(ip2, "8.8.8.8");
    
    std::string ip3 = resolver.Resolve("127.0.0.1");
    EXPECT_EQ(ip3, "127.0.0.1");
}

// Test resolving with interface parameter
// Note: This test just ensures the interface parameter doesn't cause crashes
// even if the interface doesn't exist
TEST_F(CaresResolverTest, HandlesInterfaceParameter) {
    // Just test that it doesn't crash with a non-existent interface
    std::string ip = resolver.Resolve("localhost", "nonexistent0");
    // localhost should still resolve even with a non-existent interface
    EXPECT_FALSE(ip.empty());
}

// Test that GetNameserverFromInterface doesn't crash
TEST_F(CaresResolverTest, GetNameserverDoesNotCrash) {
    // We can't test the actual result since we don't know what's in /etc/resolv.conf
    // but we can ensure the function doesn't crash
    EXPECT_NO_THROW({
        resolver.Resolve("localhost", "eth0");
    });
}

} // namespace
} // namespace fuelflux

#endif // USE_CARES
