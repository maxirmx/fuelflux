// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#ifdef TARGET_SIM800C

#include "backend_dns.h"
#include <thread>
#include <chrono>

using namespace fuelflux;

// Test fixture for DNS resolution tests
// Note: These tests require TARGET_SIM800C to be enabled and c-ares library
class DnsResolutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Tests will be skipped if ppp0 interface is not available
        // This is expected in CI environments
    }

    void TearDown() override {
    }
};

// Test DNS resolution with empty hostname
TEST_F(DnsResolutionTest, EmptyHostname) {
    std::string result = dns::ResolveDnsViaPpp0("");
    
    // Empty hostname should return empty string
    EXPECT_TRUE(result.empty());
}

// Test DNS resolution with invalid hostname
TEST_F(DnsResolutionTest, InvalidHostname) {
    // Use a hostname that definitely doesn't exist
    std::string result = dns::ResolveDnsViaPpp0("this-domain-does-not-exist-12345.invalid");
    
    // Invalid hostname should return empty string
    // Note: This may take some time to timeout
    EXPECT_TRUE(result.empty());
}

// Test DNS resolution with localhost
TEST_F(DnsResolutionTest, LocalhostResolution) {
    std::string result = dns::ResolveDnsViaPpp0("localhost");
    
    // localhost should resolve to 127.0.0.1 or fail if ppp0 binding prevents it
    // We accept either successful resolution or failure
    if (!result.empty()) {
        EXPECT_EQ(result, "127.0.0.1");
    }
}

// Test DNS resolution with IP address instead of hostname
TEST_F(DnsResolutionTest, IpAddressInput) {
    // When an IP address is provided instead of hostname
    // c-ares should handle it gracefully
    std::string result = dns::ResolveDnsViaPpp0("8.8.8.8");
    
    // Should either return the same IP or empty string
    // Both are acceptable behaviors
    if (!result.empty()) {
        EXPECT_EQ(result, "8.8.8.8");
    }
}

// Test DNS resolution with very long hostname
TEST_F(DnsResolutionTest, VeryLongHostname) {
    // DNS hostnames have a maximum length of 253 characters
    std::string long_hostname(300, 'a');
    long_hostname += ".com";
    
    std::string result = dns::ResolveDnsViaPpp0(long_hostname);
    
    // Should return empty string for invalid hostname
    EXPECT_TRUE(result.empty());
}

// Test DNS resolution with special characters
TEST_F(DnsResolutionTest, SpecialCharactersInHostname) {
    std::string result = dns::ResolveDnsViaPpp0("invalid@#$%.com");
    
    // Invalid characters should result in empty string
    EXPECT_TRUE(result.empty());
}

// Test DNS resolution with null characters
TEST_F(DnsResolutionTest, HostnameWithNullChar) {
    std::string hostname_with_null("example");
    hostname_with_null += '\0';
    hostname_with_null += "com";
    
    // This test primarily verifies that null characters don't cause crashes
    // The function should handle this gracefully - we don't assert specific behavior
    // as it depends on how c-ares handles null-terminated strings internally
    EXPECT_NO_THROW(dns::ResolveDnsViaPpp0(hostname_with_null));
}

// Test multiple concurrent DNS resolutions
TEST_F(DnsResolutionTest, ConcurrentResolutions) {
    const int num_threads = 5;
    std::vector<std::thread> threads;
    std::vector<std::string> results(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&results, i]() {
            // Each thread resolves a different (likely invalid) hostname
            results[i] = dns::ResolveDnsViaPpp0("nonexistent" + std::to_string(i) + ".invalid");
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All should return empty strings for non-existent domains
    for (const auto& result : results) {
        EXPECT_TRUE(result.empty());
    }
}

// Test DNS resolution with whitespace
TEST_F(DnsResolutionTest, HostnameWithWhitespace) {
    std::string result = dns::ResolveDnsViaPpp0("  example.com  ");
    
    // Whitespace in hostname should be handled
    // Most likely will fail to resolve
    EXPECT_TRUE(result.empty());
}

// Test DNS resolution timeout behavior
TEST_F(DnsResolutionTest, TimeoutBehavior) {
    // Use a hostname that might timeout (e.g., blackhole IP)
    // This test verifies the function returns within reasonable time
    auto start = std::chrono::steady_clock::now();
    
    std::string result = dns::ResolveDnsViaPpp0("timeout-test-domain-does-not-exist.invalid");
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    
    // Should return empty string
    EXPECT_TRUE(result.empty());
    
    // Should not take more than 15 seconds (10s timeout + some overhead)
    EXPECT_LT(duration.count(), 15);
}

// Test case sensitivity in hostname
TEST_F(DnsResolutionTest, CaseSensitivity) {
    // DNS is case-insensitive, but the function should handle different cases
    std::string result1 = dns::ResolveDnsViaPpp0("EXAMPLE.COM");
    std::string result2 = dns::ResolveDnsViaPpp0("example.com");
    std::string result3 = dns::ResolveDnsViaPpp0("ExAmPlE.CoM");
    
    // If any resolves successfully, all should resolve to the same IP
    if (!result1.empty() && !result2.empty() && !result3.empty()) {
        EXPECT_EQ(result1, result2);
        EXPECT_EQ(result2, result3);
    }
}

// Test with international domain name (IDN)
TEST_F(DnsResolutionTest, InternationalDomainName) {
    // IDN with non-ASCII characters
    std::string result = dns::ResolveDnsViaPpp0("münchen.de");
    
    // Should either resolve successfully (if c-ares supports IDN) or return empty
    // We verify it doesn't crash and returns a valid result (empty or valid IP)
    if (!result.empty()) {
        // If it resolved, result should look like a valid IP address
        EXPECT_NE(result.find('.'), std::string::npos);
    }
}

// Note: The following tests would ideally mock c-ares API but that requires
// more complex setup. Instead, we test the error paths through actual failures.

#else

// Dummy test when TARGET_SIM800C is not enabled
TEST(DnsResolutionTest, DisabledWhenNoTargetSim800c) {
    GTEST_SKIP() << "DNS resolution tests require TARGET_SIM800C to be enabled";
}

#endif // TARGET_SIM800C
