// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include "cares_resolver.h"
#include "backend.h"
#include <chrono>
#include <future>
#include <thread>

#ifdef USE_CARES

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

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
        BackendBase::ShutdownAsyncRequests();
        CleanupCaresLibrary();
    }
};

// Register the environment (will be called once before all tests)
// Google Test takes ownership, so we just need the side effect of registration
[[maybe_unused]] const auto g_cares_env_init = []() {
    ::testing::AddGlobalTestEnvironment(new CaresEnvironment);
    return true;
}();

class CaresResolverTest : public ::testing::Test {
protected:
    CaresResolver resolver;
};

TEST_F(CaresResolverTest, PreCancelledResolutionDoesNotStart) {
    std::atomic<bool> cancelled{true};
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(resolver.Resolve("example.invalid", "", &cancelled).empty());
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(250));
}

TEST_F(CaresResolverTest, CancellationInterruptsResolverLockWait) {
    std::atomic<bool> hold{false}, entered{false}, release{false}, cancelled{false};
    CaresResolver local("localhost", [&] {
        if (hold) {
            entered = true;
            while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return CaresResolver::Clock::now();
    });
    ASSERT_FALSE(local.Resolve("localhost").empty());
    hold = true;
    auto owner = std::async(std::launch::async, [&] { return local.Resolve("localhost"); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto waiter = std::async(std::launch::async, [&] { return local.Resolve("localhost", "", &cancelled); });
    cancelled = true;
    const auto status = waiter.wait_for(std::chrono::milliseconds(500));
    release = true;
    EXPECT_TRUE(entered);
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(waiter.get().empty());
    owner.get();
}

TEST_F(CaresResolverTest, CancellationInterruptsPendingDnsResponse) {
    // Receive a real DNS query locally, then deliberately withhold the reply.
    const int socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(socketFd, 0);
    struct SocketOwner { int fd; ~SocketOwner() { close(fd); } } owner{socketFd};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    socklen_t length = sizeof(address);
    ASSERT_EQ(getsockname(socketFd, reinterpret_cast<sockaddr*>(&address), &length), 0);
    CaresResolver local("", CaresResolver::Clock::now,
        "127.0.0.1:" + std::to_string(ntohs(address.sin_port)));
    std::atomic<bool> cancelled{false};
    auto request = std::async(std::launch::async, [&] {
        return local.Resolve("foreground-cancellation.invalid", "", &cancelled);
    });
    pollfd descriptor{socketFd, POLLIN, 0};
    const int received = poll(&descriptor, 1, 1000);
    const auto beforeCancel = request.wait_for(std::chrono::milliseconds(0));
    cancelled = true;
    EXPECT_EQ(received, 1);
    EXPECT_EQ(beforeCancel, std::future_status::timeout);
    EXPECT_EQ(request.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_TRUE(request.get().empty());
}

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

// Test that resolving with a specific interface doesn't crash
TEST_F(CaresResolverTest, ResolveWithInterfaceDoesNotCrash) {
    // We can't test the actual result since we don't know the system DNS configuration,
    // but we can ensure that resolving with an interface parameter doesn't crash
    EXPECT_NO_THROW({
        resolver.Resolve("localhost", "eth0");
    });
}

TEST_F(CaresResolverTest, CachesOnlyConfiguredBackendHostname) {
    CaresResolver::TimePoint now = CaresResolver::Clock::now();
    CaresResolver targetedResolver("localhost", [&now]() { return now; });

    const std::string backendIp = targetedResolver.Resolve("localhost");
    ASSERT_EQ(backendIp, "127.0.0.1");
    EXPECT_TRUE(targetedResolver.HasValidTargetedCacheForTesting());
    EXPECT_EQ(targetedResolver.GetTargetedCachedIpForTesting(), backendIp);

    const std::string nonTargetIp = targetedResolver.Resolve("127.0.0.1");
    EXPECT_EQ(nonTargetIp, "127.0.0.1");
    EXPECT_EQ(targetedResolver.GetTargetedCachedIpForTesting(), backendIp);
}

TEST_F(CaresResolverTest, UsesCacheFor24HoursAndExpiresAfterTtl) {
    CaresResolver::TimePoint now = CaresResolver::Clock::now();
    CaresResolver targetedResolver("localhost", [&now]() { return now; });

    const std::string firstIp = targetedResolver.Resolve("localhost");
    ASSERT_EQ(firstIp, "127.0.0.1");
    ASSERT_TRUE(targetedResolver.HasValidTargetedCacheForTesting());

    now += std::chrono::hours(23);
    const std::string cachedIp = targetedResolver.Resolve("localhost", "definitely_nonexistent_interface0");
    EXPECT_EQ(cachedIp, firstIp);
    EXPECT_TRUE(targetedResolver.HasValidTargetedCacheForTesting());

    now += std::chrono::hours(2);
    EXPECT_FALSE(targetedResolver.HasValidTargetedCacheForTesting());

    const std::string refreshedIp = targetedResolver.Resolve("localhost");
    EXPECT_EQ(refreshedIp, firstIp);
    EXPECT_TRUE(targetedResolver.HasValidTargetedCacheForTesting());
}

} // namespace
} // namespace fuelflux

#endif // USE_CARES
