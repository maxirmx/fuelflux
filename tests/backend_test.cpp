// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "backend.h"
#include <httplib.h>

using namespace fuelflux;
using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgReferee;

// Mock HTTP server for testing
class MockHTTPServer {
public:
    httplib::Server server;
    std::thread serverThread;
    int port;
    bool running;

    MockHTTPServer() : port(0), running(false) {
        // Setup mock endpoints
        server.Post("/api/pump/authorize", [this](const httplib::Request& req, httplib::Response& res) {
            handleAuthorize(req, res);
        });

        server.Post("/api/pump/deauthorize", [this](const httplib::Request& req, httplib::Response& res) {
            handleDeauthorize(req, res);
        });

        server.Post("/api/pump/refuel", [this](const httplib::Request& req, httplib::Response& res) {
            handleRefuel(req, res);
        });

        server.Post("/api/pump/fuel-intake", [this](const httplib::Request& req, httplib::Response& res) {
            handleFuelIntake(req, res);
        });
    }

    void start() {
        if (running) return;
        
        // Find an available port
        port = server.bind_to_any_port("127.0.0.1");
        if (port == -1) {
            throw std::runtime_error("Failed to bind to port");
        }

        running = true;
        serverThread = std::thread([this]() {
            server.listen_after_bind();
        });

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void stop() {
        if (!running) return;
        
        server.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
        running = false;
    }

    std::string getBaseURL() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }

    ~MockHTTPServer() {
        stop();
    }

    // Response handlers (can be customized per test)
    std::function<void(const httplib::Request&, httplib::Response&)> handleAuthorize;
    std::function<void(const httplib::Request&, httplib::Response&)> handleDeauthorize;
    std::function<void(const httplib::Request&, httplib::Response&)> handleRefuel;
    std::function<void(const httplib::Request&, httplib::Response&)> handleFuelIntake;
};

class BackendTest : public ::testing::Test {
protected:
    std::unique_ptr<MockHTTPServer> mockServer;
    std::string baseAPI;
    std::string controllerUid;

    void SetUp() override {
        mockServer = std::make_unique<MockHTTPServer>();
        mockServer->start();
        baseAPI = mockServer->getBaseURL();
        controllerUid = "test-controller-001";
    }

    void TearDown() override {
        mockServer->stop();
        mockServer.reset();
    }

    void setupSuccessfulAuthorizeResponse() {
        mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
            nlohmann::json response;
            response["Token"] = "test-token-12345";
            response["RoleId"] = 1;  // Customer
            response["Allowance"] = 50.0;
            response["Price"] = 45.5;
            response["fuelTanks"] = nlohmann::json::array();
            response["fuelTanks"].push_back({{"idTank", 1}, {"nameTank", "АИ-95"}});
            response["fuelTanks"].push_back({{"idTank", 2}, {"nameTank", "ДТ"}});
            
            res.status = 200;
            res.set_content(response.dump(), "application/json");
        };
    }

    void setupFailedAuthorizeResponse() {
        mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
            nlohmann::json response;
            response["CodeError"] = 1;
            response["TextError"] = "Invalid card";
            
            res.status = 200;
            res.set_content(response.dump(), "application/json");
        };
    }

    void setupSuccessfulDeauthorizeResponse() {
        mockServer->handleDeauthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
            res.status = 200;
            res.set_content("null", "application/json");
        };
    }

    void setupSuccessfulRefuelResponse() {
        mockServer->handleRefuel = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
            res.status = 200;
            res.set_content("null", "application/json");
        };
    }

    void setupSuccessfulIntakeResponse() {
        mockServer->handleFuelIntake = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
            res.status = 200;
            res.set_content("null", "application/json");
        };
    }
};

// Test Backend constructor
TEST_F(BackendTest, Constructor) {
    ASSERT_NO_THROW({
        Backend backend(baseAPI, controllerUid);
        EXPECT_FALSE(backend.IsAuthorized());
        EXPECT_EQ(backend.GetToken(), "");
        EXPECT_EQ(backend.GetRoleId(), 0);
        EXPECT_EQ(backend.GetAllowance(), 0.0);
        EXPECT_EQ(backend.GetPrice(), 0.0);
    });
}

// Test successful authorization
TEST_F(BackendTest, AuthorizeSuccess) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_FALSE(backend.IsAuthorized());
    
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_TRUE(result);
    EXPECT_TRUE(backend.IsAuthorized());
    EXPECT_EQ(backend.GetToken(), "test-token-12345");
    EXPECT_EQ(backend.GetRoleId(), 1);
    EXPECT_EQ(backend.GetAllowance(), 50.0);
    EXPECT_EQ(backend.GetPrice(), 45.5);
    EXPECT_EQ(backend.GetFuelTanks().size(), 2);
    EXPECT_EQ(backend.GetFuelTanks()[0].idTank, 1);
    EXPECT_EQ(backend.GetFuelTanks()[0].nameTank, "АИ-95");
    EXPECT_EQ(backend.GetFuelTanks()[1].idTank, 2);
    EXPECT_EQ(backend.GetFuelTanks()[1].nameTank, "ДТ");
}

// Test failed authorization
TEST_F(BackendTest, AuthorizeFailed) {
    setupFailedAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("invalid-card");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_EQ(backend.GetToken(), "");
}

// Test double authorization
TEST_F(BackendTest, DoubleAuthorize) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    EXPECT_TRUE(backend.IsAuthorized());
    
    // Try to authorize again - should fail
    bool result = backend.Authorize("card-uid-67890");
    EXPECT_FALSE(result);
}

// Test successful deauthorization
TEST_F(BackendTest, DeauthorizeSuccess) {
    setupSuccessfulAuthorizeResponse();
    setupSuccessfulDeauthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    EXPECT_TRUE(backend.IsAuthorized());
    
    bool result = backend.Deauthorize();
    
    EXPECT_TRUE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_EQ(backend.GetToken(), "");
    EXPECT_EQ(backend.GetRoleId(), 0);
    EXPECT_EQ(backend.GetAllowance(), 0.0);
    EXPECT_EQ(backend.GetFuelTanks().size(), 0);
}

// Test deauthorization without authorization
TEST_F(BackendTest, DeauthorizeWithoutAuthorize) {
    Backend backend(baseAPI, controllerUid);
    EXPECT_FALSE(backend.IsAuthorized());
    
    bool result = backend.Deauthorize();
    EXPECT_FALSE(result);
}

// Test successful refuel for customer
TEST_F(BackendTest, RefuelSuccess) {
    setupSuccessfulAuthorizeResponse();
    setupSuccessfulRefuelResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    
    bool result = backend.Refuel(1, 25.0);
    
    EXPECT_TRUE(result);
    EXPECT_EQ(backend.GetAllowance(), 25.0);  // 50.0 - 25.0
}

// Test refuel without authorization
TEST_F(BackendTest, RefuelWithoutAuthorization) {
    Backend backend(baseAPI, controllerUid);
    
    bool result = backend.Refuel(1, 25.0);
    EXPECT_FALSE(result);
}

// Test refuel with invalid tank
TEST_F(BackendTest, RefuelInvalidTank) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    
    // Tank 99 doesn't exist
    bool result = backend.Refuel(99, 25.0);
    EXPECT_FALSE(result);
}

// Test refuel with negative volume
TEST_F(BackendTest, RefuelNegativeVolume) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    
    bool result = backend.Refuel(1, -10.0);
    EXPECT_FALSE(result);
}

// Test refuel exceeding allowance
TEST_F(BackendTest, RefuelExceedingAllowance) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    
    // Allowance is 50.0, try to refuel 60.0
    bool result = backend.Refuel(1, 60.0);
    EXPECT_FALSE(result);
}

// Test successful fuel intake for operator
TEST_F(BackendTest, IntakeSuccess) {
    // Setup operator authorization
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        nlohmann::json response;
        response["Token"] = "operator-token";
        response["RoleId"] = 2;  // Operator
        response["Allowance"] = nlohmann::json();  // null
        response["Price"] = nlohmann::json();  // null
        response["fuelTanks"] = nlohmann::json::array();
        response["fuelTanks"].push_back({{"idTank", 1}, {"nameTank", "АИ-95"}});
        
        res.status = 200;
        res.set_content(response.dump(), "application/json");
    };
    
    setupSuccessfulIntakeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("operator-card-uid"));
    
    bool result = backend.Intake(1, 100.0, IntakeDirection::In);
    
    EXPECT_TRUE(result);
}

// Test intake with customer role (should fail)
TEST_F(BackendTest, IntakeWithCustomerRole) {
    setupSuccessfulAuthorizeResponse();  // This sets role to Customer
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    
    // Customer should not be able to do fuel intake
    bool result = backend.Intake(1, 100.0, IntakeDirection::In);
    EXPECT_FALSE(result);
}

// Test intake without authorization
TEST_F(BackendTest, IntakeWithoutAuthorization) {
    Backend backend(baseAPI, controllerUid);
    
    bool result = backend.Intake(1, 100.0, IntakeDirection::In);
    EXPECT_FALSE(result);
}

// Test intake with invalid tank
TEST_F(BackendTest, IntakeInvalidTank) {
    // Setup operator authorization
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        nlohmann::json response;
        response["Token"] = "operator-token";
        response["RoleId"] = 2;  // Operator
        response["Allowance"] = nlohmann::json();
        response["Price"] = nlohmann::json();
        response["fuelTanks"] = nlohmann::json::array();
        response["fuelTanks"].push_back({{"idTank", 1}, {"nameTank", "АИ-95"}});
        
        res.status = 200;
        res.set_content(response.dump(), "application/json");
    };
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("operator-card-uid"));
    
    // Tank 99 doesn't exist
    bool result = backend.Intake(99, 100.0, IntakeDirection::In);
    EXPECT_FALSE(result);
}

// Test intake with negative volume
TEST_F(BackendTest, IntakeNegativeVolume) {
    // Setup operator authorization
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        nlohmann::json response;
        response["Token"] = "operator-token";
        response["RoleId"] = 2;  // Operator
        response["Allowance"] = nlohmann::json();
        response["Price"] = nlohmann::json();
        response["fuelTanks"] = nlohmann::json::array();
        response["fuelTanks"].push_back({{"idTank", 1}, {"nameTank", "АИ-95"}});
        
        res.status = 200;
        res.set_content(response.dump(), "application/json");
    };
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("operator-card-uid"));
    
    bool result = backend.Intake(1, -100.0, IntakeDirection::In);
    EXPECT_FALSE(result);
}

// Test connection error handling
TEST_F(BackendTest, ConnectionErrorHandling) {
    // Use an invalid port to simulate connection failure
    Backend backend("http://127.0.0.1:1", controllerUid);
    
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    // Verify error message is set
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test timeout error handling
TEST_F(BackendTest, TimeoutErrorHandling) {
    // Setup a server that doesn't respond
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        // Simulate slow response that will timeout
        std::this_thread::sleep_for(std::chrono::seconds(11));
        res.status = 200;
        res.set_content("null", "application/json");
    };
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test HTTP 404 error handling
TEST_F(BackendTest, Http404ErrorHandling) {
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 404;
        res.set_content("Not Found", "text/plain");
    };
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test HTTP 500 error handling
TEST_F(BackendTest, Http500ErrorHandling) {
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
    };
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test HTTP 503 error handling
TEST_F(BackendTest, Http503ErrorHandling) {
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 503;
        res.set_content("Service Unavailable", "text/plain");
    };
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test malformed JSON response handling
TEST_F(BackendTest, MalformedJsonErrorHandling) {
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 200;
        res.set_content("{invalid json: missing quotes and brackets", "application/json");
    };
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test incomplete JSON response handling
TEST_F(BackendTest, IncompleteJsonErrorHandling) {
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"Token\": \"test-token\", \"RoleId\": ", "application/json");
    };
    
    Backend backend(baseAPI, controllerUid);
    bool result = backend.Authorize("card-uid-12345");
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.IsAuthorized());
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test wrapper error response propagation in Deauthorize
TEST_F(BackendTest, DeauthorizeConnectionError) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    EXPECT_TRUE(backend.IsAuthorized());
    
    // Setup connection error for deauthorize
    mockServer->handleDeauthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        // Simulate timeout-like connection error with an HTTP error status
        res.status = 504; // Gateway Timeout
        res.set_content("Simulated timeout error", "text/plain");
    };
    
    bool result = backend.Deauthorize();
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test wrapper error response propagation in Refuel
TEST_F(BackendTest, RefuelHttpError) {
    setupSuccessfulAuthorizeResponse();
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("card-uid-12345"));
    
    // Setup HTTP error for refuel
    mockServer->handleRefuel = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 500;
        res.set_content("Server Error", "text/plain");
    };
    
    bool result = backend.Refuel(1, 25.0);
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.GetLastError().empty());
}

// Test wrapper error response propagation in Intake
TEST_F(BackendTest, IntakeJsonParseError) {
    // Setup operator authorization
    mockServer->handleAuthorize = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        nlohmann::json response;
        response["Token"] = "operator-token";
        response["RoleId"] = 2;  // Operator
        response["Allowance"] = nlohmann::json();
        response["Price"] = nlohmann::json();
        response["fuelTanks"] = nlohmann::json::array();
        response["fuelTanks"].push_back({{"idTank", 1}, {"nameTank", "АИ-95"}});
        
        res.status = 200;
        res.set_content(response.dump(), "application/json");
    };
    
    Backend backend(baseAPI, controllerUid);
    EXPECT_TRUE(backend.Authorize("operator-card-uid"));
    
    // Setup malformed JSON for intake
    mockServer->handleFuelIntake = [](const httplib::Request& req [[maybe_unused]], httplib::Response& res) {
        res.status = 200;
        res.set_content("not valid json at all!", "application/json");
    };
    
    bool result = backend.Intake(1, 100.0, IntakeDirection::In);
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(backend.GetLastError().empty());
}
