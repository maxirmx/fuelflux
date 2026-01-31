// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include "backend.h"
#include "backlog.h"
#include <httplib.h>
#include <filesystem>

using namespace fuelflux;

namespace {

class MockHTTPServer {
public:
    httplib::Server server;
    std::thread serverThread;
    int port = 0;
    bool running = false;

    std::atomic<int> authorizeCount{0};
    std::atomic<int> deauthorizeCount{0};
    std::atomic<int> refuelCount{0};
    std::atomic<int> intakeCount{0};
    std::atomic<int> roleId{1};

    void start() {
        if (running) return;
        server.Post("/api/pump/authorize", [this](const httplib::Request&,
                                                 httplib::Response& res) {
            authorizeCount++;
            nlohmann::json response;
            response["Token"] = "token";
            response["RoleId"] = roleId.load();
            response["Allowance"] = 50.0;
            response["Price"] = 1.0;
            response["fuelTanks"] = nlohmann::json::array({{{"idTank", 1}, {"nameTank", "A"}}});
            res.status = 200;
            res.set_content(response.dump(), "application/json");
        });
        server.Post("/api/pump/deauthorize", [this](const httplib::Request&,
                                                   httplib::Response& res) {
            deauthorizeCount++;
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        server.Post("/api/pump/refuel", [this](const httplib::Request&,
                                              httplib::Response& res) {
            refuelCount++;
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        server.Post("/api/pump/fuel-intake", [this](const httplib::Request&,
                                                   httplib::Response& res) {
            intakeCount++;
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        port = server.bind_to_any_port("127.0.0.1");
        if (port == -1) {
            throw std::runtime_error("Failed to bind to port");
        }
        running = true;
        serverThread = std::thread([this]() { server.listen_after_bind(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void stop() {
        if (!running) return;
        server.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
        running = false;
    }

    std::string baseUrl() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }

    ~MockHTTPServer() {
        stop();
    }
};

std::filesystem::path MakeTempDbPath(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

} // namespace

TEST(BacklogStoreTest, EnqueueFetchRemove) {
    auto dbPath = MakeTempDbPath("fuelflux_backlog_store.sqlite");
    BacklogStore store(dbPath.string());
    EXPECT_TRUE(store.Initialize());
    EXPECT_EQ(store.Count(), 0u);

    EXPECT_TRUE(store.Enqueue("uid-1", BacklogMethod::Refuel, R"({"TankNumber":1})"));
    EXPECT_TRUE(store.Enqueue("uid-2", BacklogMethod::Intake, R"({"TankNumber":2})"));
    EXPECT_EQ(store.Count(), 2u);

    auto items = store.FetchAll();
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].uid, "uid-1");
    EXPECT_EQ(items[0].method, BacklogMethod::Refuel);
    EXPECT_EQ(items[1].uid, "uid-2");
    EXPECT_EQ(items[1].method, BacklogMethod::Intake);

    EXPECT_TRUE(store.Remove(items[0].id));
    EXPECT_EQ(store.Count(), 1u);
    EXPECT_TRUE(store.Remove(items[1].id));
    EXPECT_EQ(store.Count(), 0u);
}

TEST(BacklogStoreTest, PersistsAcrossInstances) {
    auto dbPath = MakeTempDbPath("fuelflux_backlog_persist.sqlite");
    {
        BacklogStore store(dbPath.string());
        EXPECT_TRUE(store.Initialize());
        EXPECT_TRUE(store.Enqueue("uid-1", BacklogMethod::Refuel, R"({"TankNumber":1})"));
    }
    BacklogStore store(dbPath.string());
    EXPECT_TRUE(store.Initialize());
    EXPECT_EQ(store.Count(), 1u);
    auto items = store.FetchAll();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].uid, "uid-1");
}

TEST(BackendBacklogTest, EnqueuesOnNetworkFailure) {
    auto dbPath = MakeTempDbPath("fuelflux_backlog_network.sqlite");
    Backend backend("http://127.0.0.1:1", "controller", dbPath.string(),
                    std::chrono::milliseconds(100));
    backend.StopBacklogWorker();

    EXPECT_FALSE(backend.Authorize("uid-1"));
    EXPECT_EQ(backend.ProcessBacklogOnce(), false);

    BacklogStore store(dbPath.string());
    EXPECT_TRUE(store.Initialize());
    EXPECT_EQ(store.Count(), 0u);

    MockHTTPServer server;
    server.start();
    Backend connected(server.baseUrl(), "controller", dbPath.string(),
                      std::chrono::milliseconds(100));
    connected.StopBacklogWorker();
    EXPECT_TRUE(connected.Authorize("uid-1"));
    server.stop();
    EXPECT_FALSE(connected.Refuel(1, 10.0));
    EXPECT_EQ(store.Count(), 1u);
}

TEST(BackendBacklogTest, ProcessesRefuelBacklog) {
    auto dbPath = MakeTempDbPath("fuelflux_backlog_process.sqlite");
    BacklogStore store(dbPath.string());
    EXPECT_TRUE(store.Initialize());
    EXPECT_TRUE(store.Enqueue("uid-1", BacklogMethod::Refuel,
                              R"({"TankNumber":1,"FuelVolume":10.0,"TimeAt":123})"));

    MockHTTPServer server;
    server.start();
    Backend backend(server.baseUrl(), "controller", dbPath.string(),
                    std::chrono::milliseconds(100));
    backend.StopBacklogWorker();
    EXPECT_TRUE(backend.ProcessBacklogOnce());
    EXPECT_EQ(store.Count(), 0u);
    EXPECT_EQ(server.authorizeCount.load(), 1);
    EXPECT_EQ(server.refuelCount.load(), 1);
    EXPECT_EQ(server.deauthorizeCount.load(), 1);
}

TEST(BackendBacklogTest, ProcessesIntakeBacklog) {
    auto dbPath = MakeTempDbPath("fuelflux_backlog_process_intake.sqlite");
    BacklogStore store(dbPath.string());
    EXPECT_TRUE(store.Initialize());
    EXPECT_TRUE(store.Enqueue("uid-2", BacklogMethod::Intake,
                              R"({"TankNumber":1,"IntakeVolume":5.0,"Direction":1,"TimeAt":123})"));

    MockHTTPServer server;
    server.roleId = 2;
    server.start();
    Backend backend(server.baseUrl(), "controller", dbPath.string(),
                    std::chrono::milliseconds(100));
    backend.StopBacklogWorker();
    EXPECT_TRUE(backend.ProcessBacklogOnce());
    EXPECT_EQ(store.Count(), 0u);
    EXPECT_EQ(server.authorizeCount.load(), 1);
    EXPECT_EQ(server.intakeCount.load(), 1);
    EXPECT_EQ(server.deauthorizeCount.load(), 1);
}
