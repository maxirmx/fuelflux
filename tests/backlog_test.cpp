// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "backlog_storage.h"
#include "backlog_worker.h"

using namespace fuelflux;

namespace {

class FakeBackend : public IBackend {
public:
    bool Authorize(const std::string& uid) override {
        calls.push_back("authorize");
        lastAuthorizedUid = uid;
        authorized = authorizeResult;
        return authorizeResult;
    }

    bool Deauthorize() override {
        calls.push_back("deauthorize");
        authorized = false;
        return deauthorizeResult;
    }

    bool Refuel(TankNumber, Volume) override { return true; }
    bool Intake(TankNumber, Volume, IntakeDirection) override { return true; }

    bool RefuelFromPayload(const std::string& payload) override {
        calls.push_back("refuel");
        lastPayload = payload;
        return refuelResult;
    }

    bool IntakeFromPayload(const std::string& payload) override {
        calls.push_back("intake");
        lastPayload = payload;
        return intakeResult;
    }

    bool IsAuthorized() const override { return authorized; }
    const std::string& GetToken() const override { return token; }
    int GetRoleId() const override { return 0; }
    double GetAllowance() const override { return 0.0; }
    double GetPrice() const override { return 0.0; }
    const std::vector<BackendTankInfo>& GetFuelTanks() const override { return tanks; }
    const std::string& GetLastError() const override { return lastError; }
    bool WasLastErrorNetwork() const override { return false; }
    const std::string& GetLastRequestPayload() const override { return lastPayload; }

    bool authorizeResult = true;
    bool deauthorizeResult = true;
    bool refuelResult = true;
    bool intakeResult = true;
    bool authorized = false;
    std::string lastAuthorizedUid;
    std::string lastPayload;
    std::string token;
    std::string lastError;
    std::vector<BackendTankInfo> tanks;
    std::vector<std::string> calls;
};

std::string MakeTempDbPath(const std::string& name) {
    const auto base = std::filesystem::temp_directory_path();
    return (base / name).string();
}

} // namespace

TEST(BacklogStorageTest, PersistsItemsAcrossRestarts) {
    const std::string path = MakeTempDbPath("fuelflux_backlog_test.db");
    std::filesystem::remove(path);

    {
        BacklogStorage storage(path);
        ASSERT_TRUE(storage.IsOpen());
        EXPECT_TRUE(storage.AddItem("uid-1", BacklogMethod::Refuel, "payload-1"));
        EXPECT_TRUE(storage.AddItem("uid-2", BacklogMethod::Intake, "payload-2"));
    }

    BacklogStorage storage(path);
    ASSERT_TRUE(storage.IsOpen());
    auto items = storage.LoadAll();
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].uid, "uid-1");
    EXPECT_EQ(items[0].method, BacklogMethod::Refuel);
    EXPECT_EQ(items[0].data, "payload-1");

    EXPECT_TRUE(storage.RemoveItem(items[0].id));
    auto remaining = storage.LoadAll();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0].uid, "uid-2");

    std::filesystem::remove(path);
}

TEST(BacklogWorkerTest, ProcessesAndRemovesItem) {
    const std::string path = MakeTempDbPath("fuelflux_backlog_worker.db");
    std::filesystem::remove(path);

    auto storage = std::make_shared<BacklogStorage>(path);
    ASSERT_TRUE(storage->IsOpen());
    ASSERT_TRUE(storage->AddItem("uid-123", BacklogMethod::Refuel, "payload-123"));

    auto backend = std::make_unique<FakeBackend>();
    auto backendPtr = backend.get();
    BacklogWorker worker(storage, std::move(backend), std::chrono::milliseconds(10));

    EXPECT_TRUE(worker.ProcessOnce());
    auto items = storage->LoadAll();
    EXPECT_TRUE(items.empty());
    EXPECT_EQ(backendPtr->lastAuthorizedUid, "uid-123");
    EXPECT_EQ(backendPtr->lastPayload, "payload-123");
    ASSERT_EQ(backendPtr->calls.size(), 3u);
    EXPECT_EQ(backendPtr->calls[0], "authorize");
    EXPECT_EQ(backendPtr->calls[1], "refuel");
    EXPECT_EQ(backendPtr->calls[2], "deauthorize");

    std::filesystem::remove(path);
}

TEST(BacklogWorkerTest, KeepsItemOnFailure) {
    const std::string path = MakeTempDbPath("fuelflux_backlog_worker_fail.db");
    std::filesystem::remove(path);

    auto storage = std::make_shared<BacklogStorage>(path);
    ASSERT_TRUE(storage->IsOpen());
    ASSERT_TRUE(storage->AddItem("uid-456", BacklogMethod::Intake, "payload-456"));

    auto backend = std::make_unique<FakeBackend>();
    auto backendPtr = backend.get();
    backendPtr->intakeResult = false;
    BacklogWorker worker(storage, std::move(backend), std::chrono::milliseconds(10));

    EXPECT_FALSE(worker.ProcessOnce());
    auto items = storage->LoadAll();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].uid, "uid-456");
    ASSERT_EQ(backendPtr->calls.size(), 3u);
    EXPECT_EQ(backendPtr->calls[0], "authorize");
    EXPECT_EQ(backendPtr->calls[1], "intake");
    EXPECT_EQ(backendPtr->calls[2], "deauthorize");

    std::filesystem::remove(path);
}
