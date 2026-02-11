// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "backend.h"
#include <httplib.h>
#include <thread>
#include <chrono>

using namespace fuelflux;

// Test to verify that rapid deauthorization doesn't exhaust resources
TEST(BackendIntegrationTest, RapidDeauthorizationBounded) {
    // Start a mock HTTP server
    httplib::Server server;
    
    // Track concurrent deauthorization requests
    std::atomic<int> concurrentRequests{0};
    std::atomic<int> maxConcurrentRequests{0};
    std::atomic<int> totalRequests{0};
    
    server.Post("/api/pump/authorize", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json response = {
            {"CodeError", 0},
            {"TextError", ""},
            {"Token", "mock-token-12345"},
            {"RoleId", 1},
            {"Allowance", 100.0},
            {"Price", 50.0},
            {"FuelTanks", nlohmann::json::array({
                {{"IdTank", 1}, {"NameTank", "Tank 1"}}
            })}
        };
        res.set_content(response.dump(), "application/json");
    });
    
    server.Post("/api/pump/deauthorize", [&](const httplib::Request&, httplib::Response& res) {
        int current = ++concurrentRequests;
        totalRequests++;
        
        // Update max concurrent requests
        int expected = maxConcurrentRequests.load();
        while (current > expected && 
               !maxConcurrentRequests.compare_exchange_weak(expected, current)) {
        }
        
        // Simulate slow deauthorization (this would cause unbounded thread creation to fail)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        --concurrentRequests;
        
        nlohmann::json response = {
            {"CodeError", 0},
            {"TextError", ""}
        };
        res.set_content(response.dump(), "application/json");
    });
    
    // Start server in background
    int port = server.bind_to_any_port("127.0.0.1");
    ASSERT_NE(port, -1);
    
    std::thread serverThread([&server]() {
        server.listen_after_bind();
    });
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create backend and perform rapid authorize/deauthorize cycles
    std::string baseAPI = "http://127.0.0.1:" + std::to_string(port);
    auto backend = std::make_shared<Backend>(baseAPI, "test-controller");
    
    const int numCycles = 50;  // More than thread pool size (4) + queue size (100)
    
    for (int i = 0; i < numCycles; ++i) {
        ASSERT_TRUE(backend->Authorize("test-uid-" + std::to_string(i)));
        ASSERT_TRUE(backend->Deauthorize());
    }
    
    // Wait for async deauthorize requests to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Verify that concurrent requests were bounded
    // With 1 worker thread, we should never see more than 1 concurrent request
    EXPECT_LE(maxConcurrentRequests.load(), 1) 
        << "Bounded executor should limit concurrent deauthorization to thread pool size";
    
    // Some requests may have been dropped if queue was full, but we should have processed many
    EXPECT_GT(totalRequests.load(), 0) 
        << "At least some deauthorization requests should have been processed";
    
    // Cleanup
    server.stop();
    if (serverThread.joinable()) {
        serverThread.join();
    }
}
