// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "backend.h"
#include <httplib.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <future>

using namespace fuelflux;

TEST(BackendIntegrationTest, DelayedDeauthorizationUsesCapturedTokenAndPreservesNewSession) {
    httplib::Server server;
    std::atomic<int> authorizationCount{0};
    std::promise<std::string> deauthorizationToken;
    auto deauthorizationTokenFuture = deauthorizationToken.get_future();
    std::promise<void> releaseDeauthorization;
    auto releaseFuture = releaseDeauthorization.get_future().share();
    std::promise<void> deauthorizationCompleted;
    auto deauthorizationCompletedFuture = deauthorizationCompleted.get_future();

    server.Post("/api/pump/authorize", [&](const httplib::Request&, httplib::Response& res) {
        const auto token = authorizationCount.fetch_add(1) == 0 ? "token-a" : "token-b";
        nlohmann::json response = {
            {"CodeError", 0},
            {"TextError", ""},
            {"Token", token},
            {"RoleId", 1},
            {"Allowance", 100.0},
            {"Price", 50.0},
            {"fuelTanks", nlohmann::json::array({
                {{"idTank", 1}, {"visualNumberTank", 1}, {"nameTank", "Tank 1"},
                 {"isCheckEnoughFuel", 1}, {"allowanceTank", "120.0"}}
            })}
        };
        res.set_content(response.dump(), "application/json");
    });

    server.Post("/api/pump/deauthorize", [&](const httplib::Request& req, httplib::Response& res) {
        deauthorizationToken.set_value(req.get_header_value("Authorization"));
        releaseFuture.wait();
        res.set_content(nlohmann::json{{"CodeError", 0}, {"TextError", ""}}.dump(),
                        "application/json");
        deauthorizationCompleted.set_value();
    });

    const int port = server.bind_to_any_port("127.0.0.1");
    ASSERT_NE(port, -1);
    std::thread serverThread([&server] { server.listen_after_bind(); });

    const std::string baseAPI = "http://127.0.0.1:" + std::to_string(port);
    auto backend = std::make_shared<Backend>(baseAPI, "test-controller");
    const bool firstAuthorized = backend->Authorize("first-user");
    const std::string firstToken = backend->GetToken();
    const bool deauthorizationSubmitted = firstAuthorized && backend->Deauthorize();

    const bool requestStarted = deauthorizationSubmitted &&
        deauthorizationTokenFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    bool secondAuthorized = false;
    std::string secondToken;
    if (requestStarted) {
        secondAuthorized = backend->Authorize("second-user");
        secondToken = backend->GetToken();
    }

    releaseDeauthorization.set_value();
    const bool requestCompleted = deauthorizationCompletedFuture.wait_for(std::chrono::seconds(5)) ==
                                  std::future_status::ready;

    EXPECT_TRUE(firstAuthorized);
    EXPECT_EQ(firstToken, "token-a");
    EXPECT_TRUE(deauthorizationSubmitted);
    EXPECT_TRUE(requestStarted);
    if (requestStarted) {
        EXPECT_EQ(deauthorizationTokenFuture.get(), "Bearer token-a");
    }
    EXPECT_TRUE(secondAuthorized);
    EXPECT_EQ(secondToken, "token-b");
    EXPECT_TRUE(requestCompleted);
    EXPECT_TRUE(backend->IsAuthorized());
    EXPECT_EQ(backend->GetToken(), "token-b");

    server.stop();
    serverThread.join();
}

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
            {"fuelTanks", nlohmann::json::array({
                {{"idTank", 1}, {"visualNumberTank", 1}, {"nameTank", "Tank 1"}, {"isCheckEnoughFuel", 1}, {"allowanceTank", "120.0"}}
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
    
    const int numCycles = 50;  // Enough cycles to stress a single-worker bounded executor (1 worker thread with a finite queue)
    
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
