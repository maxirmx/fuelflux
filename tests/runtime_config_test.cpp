// Copyright (C) 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "runtime_config.h"
#include "config.h"
#include "controller.h"
#include "cache_manager.h"
#include "backlog_worker.h"
#ifdef USE_CARES
#include "cares_resolver.h"
#endif
#include <gtest/gtest.h>
#include <httplib.h>
#include <atomic>
#include <map>
#include <thread>

using namespace fuelflux;

namespace fuelflux {
struct RuntimeConfigurationTestAccess {
    static auto BackendOf(const Controller& controller) { return controller.backend_; }
    static auto CacheOf(const Controller& controller) { return controller.cacheManager_; }
#ifdef USE_CARES
    static auto DnsTarget(const Backend& backend) { return backend.resolver_->GetTargetedHostnameForTesting(); }
#endif
};
}

namespace {
RuntimeConfig Settings(const std::map<std::string, std::string>& env,
                       ConfigurationWarning warn = {}) {
    return LoadRuntimeConfig([&](const char* name) -> std::optional<std::string> {
        const auto value = env.find(name);
        return value == env.end() ? std::nullopt : std::optional<std::string>(value->second);
    }, std::move(warn));
}
}

TEST(RuntimeConfigTest, DefaultsAndBlankValues) {
    for (const auto& blank : {"", " ", "\r\n\t"}) {
        const auto config = Settings({{"FUELFLUX_CONTROLLER_ID", blank}, {"FUELFLUX_BACKEND_URL", blank}});
        EXPECT_EQ(config.controllerId, CONTROLLER_UID);
        EXPECT_EQ(config.backendUrl, BACKEND_API_URL);
    }
    const auto config = Settings({});
    EXPECT_EQ(config.controllerId, CONTROLLER_UID);
    EXPECT_EQ(config.backendUrl, BACKEND_API_URL);
}

TEST(RuntimeConfigTest, IndependentOverridesAndOpaqueIds) {
    for (const auto& id : {"00000123", "device-A-01"}) {
        const auto config = Settings({{"FUELFLUX_CONTROLLER_ID", id}});
        EXPECT_EQ(config.controllerId, id);
        EXPECT_EQ(config.backendUrl, BACKEND_API_URL);
    }
    const auto config = Settings({{"FUELFLUX_BACKEND_URL", "https://example.org:8443/fleet/"}});
    EXPECT_EQ(config.controllerId, CONTROLLER_UID);
    EXPECT_EQ(config.backendUrl, "https://example.org:8443/fleet");
}

TEST(RuntimeConfigTest, SupportedUrlsAndNormalization) {
    for (const auto& pair : std::map<std::string, std::string>{
        {"http://example.org", "http://example.org"},
        {" https://example.org/// \t", "https://example.org"},
        {"https://example.org:8443/fleet/api///", "https://example.org:8443/fleet/api"},
        {"http://127.0.0.1:22000/base", "http://127.0.0.1:22000/base"},
        {"http://[::1]:8080/base/", "http://[::1]:8080/base"},
        {"https://example.org/a%20b/", "https://example.org/a%20b"}}) {
        int warnings = 0;
        const auto config = Settings({{"FUELFLUX_BACKEND_URL", pair.first}}, [&](auto&) { ++warnings; });
        EXPECT_EQ(config.backendUrl, pair.second) << pair.first;
        EXPECT_EQ(warnings, 0);
    }
}

TEST(RuntimeConfigTest, InvalidUrlWarnsOnceWithoutLeakingCredentialsAndRetainsId) {
    for (const auto& value : {"example.org", "ftp://example.org", "http://", "https:///", "http:///example.org",
         "http://user:secret@example.org", "http://@example.org", "http://example.org?",
         "http://example.org?q=secret", "http://example.org#", "http://example.org/#secret",
         "http://exa mple.org", "http://example.org:65536", "http://example.org:0",
         "http://example.org:bad", "http://example.org:", "http://[broken", "http://example.org/a%GG",
         "http://example.org/\\evil", "http://example.org/\nsecret"}) {
        int warnings = 0;
        std::string warning;
        const auto config = Settings({{"FUELFLUX_CONTROLLER_ID", "0000-A"}, {"FUELFLUX_BACKEND_URL", value}},
            [&](const std::string& message) { ++warnings; warning = message; });
        EXPECT_EQ(config.controllerId, "0000-A");
        EXPECT_EQ(config.backendUrl, BACKEND_API_URL) << value;
        EXPECT_EQ(warnings, 1) << value;
        EXPECT_NE(warning.find("FUELFLUX_BACKEND_URL"), std::string::npos);
        EXPECT_EQ(warning.find("secret"), std::string::npos);
    }
}

TEST(RuntimeConfigTest, SnapshotSurvivesEnvironmentChangesAndFactoriesKeepCompatibility) {
    std::string id = "000-device";
    const auto config = LoadRuntimeConfig([&](const char*) { return std::optional<std::string>(id); });
    id = "changed";
    EXPECT_EQ(config.controllerId, "000-device");
    const auto backend = Controller::CreateDefaultBackend(config);
    EXPECT_EQ(backend->GetControllerUid(), config.controllerId);
    EXPECT_EQ(backend->GetBaseApiUrl(), config.backendUrl);
    EXPECT_EQ(Controller::CreateDefaultBackend()->GetControllerUid(), CONTROLLER_UID);
    EXPECT_EQ(Controller::CreateDefaultBackendShared("custom-id")->GetControllerUid(), "custom-id");
    ControllerRuntimeOptions options;
    options.startCacheSynchronization = false;
    Controller controller("supplied-id", nullptr, std::chrono::seconds(30), {":memory:", ":memory:"}, options);
    EXPECT_EQ(RuntimeConfigurationTestAccess::BackendOf(controller)->GetControllerUid(), "supplied-id");
}

TEST(RuntimeConfigIntegrationTest, ForegroundCacheAndBacklogUseConfiguredServerAndIdentity) {
    httplib::Server server;
    std::atomic<int> authorizations{0}, cards{0}, tanks{0}, refuels{0}, deauthorizations{0};
    std::atomic<bool> correctIdentity{true};
    const std::string id = "000-controller-A";
    auto check = [&](const httplib::Request& request) {
        if (nlohmann::json::parse(request.body).value("PumpControllerUid", "") != id) correctIdentity = false;
    };
    server.Post("/fleet/api/pump/authorize", [&](const auto& req, auto& res) {
        check(req);
        const auto body = nlohmann::json::parse(req.body);
        ++authorizations;
        res.set_content(nlohmann::json{{"Token", "configured-token-" + body.value("CardUid", "")},
            {"RoleId", body.value("CardUid", "") == id ? 3 : 1},
            {"Allowance", 100.0}, {"Price", 1.0}, {"fuelTanks", nlohmann::json::array({
                {{"idTank", 7}, {"visualNumberTank", 1}, {"volume", 100.0}}})}}.dump(), "application/json");
    });
    server.Post("/fleet/api/pump/cards", [&](const auto& req, auto& res) {
        check(req); ++cards;
        if (req.get_header_value("Authorization") != "Bearer configured-token-" + id) correctIdentity = false;
        res.set_content("[]", "application/json");
    });
    server.Post("/fleet/api/pump/tanks", [&](const auto& req, auto& res) {
        check(req); ++tanks;
        if (req.get_header_value("Authorization") != "Bearer configured-token-" + id) correctIdentity = false;
        res.set_content("[]", "application/json");
    });
    server.Post("/fleet/api/pump/refuel", [&](const auto& req, auto& res) {
        // Reports identify the controller through its authorized bearer session.
        if (req.get_header_value("Authorization") != "Bearer configured-token-user-A") correctIdentity = false;
        EXPECT_EQ(nlohmann::json::parse(req.body).at("TankNumber"), 7);
        ++refuels;
        res.set_content("null", "application/json");
    });
    server.Post("/fleet/api/pump/deauthorize", [&](const auto&, auto& res) {
        ++deauthorizations; res.set_content("null", "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread thread([&] { server.listen_after_bind(); });
    {
        const auto config = Settings({{"FUELFLUX_CONTROLLER_ID", id},
            {"FUELFLUX_BACKEND_URL", "http://127.0.0.1:" + std::to_string(port) + "/fleet/"}});
        auto foreground = Controller::CreateDefaultBackend(config);
        EXPECT_TRUE(foreground->Authorize("user-A"));
        EXPECT_TRUE(foreground->Refuel(1, 2.0));
        EXPECT_TRUE(foreground->DeauthorizeAndWait());

        Controller controller(id, foreground, std::chrono::seconds(30), {":memory:", ":memory:"});
        auto cache = RuntimeConfigurationTestAccess::CacheOf(controller);
        const auto before = cache->GetLastPopulationTime();
        EXPECT_TRUE(cache->Start());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (cache->GetLastPopulationTime() <= before && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cache->Stop();
        EXPECT_TRUE(cache->GetLastPopulationSuccess());
        EXPECT_EQ(cards, 1); EXPECT_EQ(tanks, 1);

        auto storage = std::make_shared<MessageStorage>(":memory:");
        EXPECT_TRUE(storage->AddBacklog("user-A", MessageMethod::Refuel,
            nlohmann::json{{"TankNumber", 1}, {"FuelVolume", 2.0}, {"TimeAt", 123456}}.dump()));
        BacklogWorker backlog(storage, Controller::CreateDefaultBackend(config), std::chrono::seconds(1));
        EXPECT_TRUE(backlog.ProcessOnce());
        EXPECT_EQ(storage->BacklogCount(), 0);
        EXPECT_EQ(authorizations, 3); EXPECT_EQ(refuels, 2);
        EXPECT_TRUE(correctIdentity);
        const auto done = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (deauthorizations < 3 && std::chrono::steady_clock::now() < done)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(deauthorizations, 3);
    }
    server.stop(); thread.join();
}

TEST(RuntimeConfigIntegrationTest, ConnectionFailuresDoNotChangeConfiguredServer) {
    httplib::Server server;
    std::atomic<int> requests{0};
    server.Post("/fleet/api/pump/authorize", [&](const auto&, auto& res) {
        ++requests; res.status = 503;
        res.set_content("unavailable", "text/plain");
    });
    const auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread thread([&] { server.listen_after_bind(); });
    const auto config = Settings({{"FUELFLUX_CONTROLLER_ID", "000-A"},
        {"FUELFLUX_BACKEND_URL", "http://127.0.0.1:" + std::to_string(port) + "/fleet"}});
    const auto backend = Controller::CreateDefaultBackend(config);
    EXPECT_FALSE(backend->Authorize("user"));
    EXPECT_FALSE(backend->Authorize("user"));
    EXPECT_EQ(backend->GetBaseApiUrl(), config.backendUrl);
    EXPECT_EQ(backend->GetControllerUid(), "000-A");
    EXPECT_EQ(requests, 2);
    server.stop(); thread.join();
}

#ifdef USE_CARES
TEST(RuntimeConfigTest, ProductionDnsCacheTargetsConfiguredHostname) {
    Backend backend("https://custom.example.org:8443/fleet", "device");
    EXPECT_EQ(RuntimeConfigurationTestAccess::DnsTarget(backend), "custom.example.org");
}
#endif
