// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "backlog_worker.h"
#include "message_storage.h"
#include <sqlite3.h>
#include <filesystem>
#include <random>

using namespace fuelflux;
using ::testing::Return;
using ::testing::StrictMock;

class MockBackendForBacklog : public IBackend {
public:
    MOCK_METHOD(void, CancelPendingRequests, (), (override));
    MOCK_METHOD(bool, SendReportPayload, (const std::string& payload, bool intake, bool canonicalTankId), (override));
    MOCK_METHOD(bool, Authorize, (const std::string& uid), (override));
    MOCK_METHOD(bool, Deauthorize, (), (override));
    MOCK_METHOD(bool, Refuel, (TankNumber tankNumber, Volume volume), (override));
    MOCK_METHOD(bool, Intake, (TankNumber tankNumber, Volume volume, IntakeDirection direction), (override));
    MOCK_METHOD(bool, RefuelPayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IntakePayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IsAuthorized, (), (const, override));
    MOCK_METHOD(std::string, GetToken, (), (const, override));
    MOCK_METHOD(int, GetRoleId, (), (const, override));
    MOCK_METHOD(double, GetAllowance, (), (const, override));
    MOCK_METHOD(double, GetPrice, (), (const, override));
    MOCK_METHOD(const std::vector<BackendTankInfo>&, GetFuelTanks, (), (const, override));
    MOCK_METHOD(const std::string&, GetLastError, (), (const, override));
    MOCK_METHOD(bool, IsNetworkError, (), (const, override));
    MOCK_METHOD(std::vector<UserCard>, FetchUserCards, (int first, int number), (override));
    MOCK_METHOD(std::vector<FuelTank>, FetchFuelTanks, (int first, int number), (override));
    MOCK_METHOD(const std::string&, GetControllerUid, (), (const, override));
};

TEST(BacklogWorkerTest, ProcessesBacklogSuccessfully) {
    auto storage = std::make_shared<MessageStorage>(":memory:");
    ASSERT_TRUE(storage->AddBacklog("uid-1", MessageMethod::Refuel, "{\"TankNumber\":1}"));

    auto backend = std::make_shared<StrictMock<MockBackendForBacklog>>();
    EXPECT_CALL(*backend, CancelPendingRequests()).Times(::testing::AnyNumber());
    EXPECT_CALL(*backend, IsAuthorized()).WillRepeatedly(Return(false));
    EXPECT_CALL(*backend, Authorize("uid-1")).WillOnce(Return(true));
    EXPECT_CALL(*backend, SendReportPayload("{\"TankNumber\":1}", false, false)).WillOnce(Return(true));
    EXPECT_CALL(*backend, Deauthorize()).WillOnce(Return(true));

    BacklogWorker worker(storage, backend, std::chrono::milliseconds(1));
    EXPECT_TRUE(worker.ProcessOnce());
    EXPECT_EQ(storage->BacklogCount(), 0);
    EXPECT_EQ(storage->DeadMessageCount(), 0);
}

TEST(BacklogWorkerTest, KeepsBacklogOnNetworkError) {
    auto storage = std::make_shared<MessageStorage>(":memory:");
    ASSERT_TRUE(storage->AddBacklog("uid-2", MessageMethod::Refuel, "{\"TankNumber\":2}"));

    auto backend = std::make_shared<StrictMock<MockBackendForBacklog>>();
    EXPECT_CALL(*backend, CancelPendingRequests()).Times(::testing::AnyNumber());
    EXPECT_CALL(*backend, IsAuthorized()).WillRepeatedly(Return(false));
    EXPECT_CALL(*backend, Authorize("uid-2")).WillOnce(Return(false));
    EXPECT_CALL(*backend, IsNetworkError()).WillOnce(Return(true));

    BacklogWorker worker(storage, backend, std::chrono::milliseconds(1));
    EXPECT_FALSE(worker.ProcessOnce());
    EXPECT_EQ(storage->BacklogCount(), 1);
    EXPECT_EQ(storage->DeadMessageCount(), 0);
}

TEST(BacklogWorkerTest, MovesToDeadOnNonNetworkError) {
    auto storage = std::make_shared<MessageStorage>(":memory:");
    ASSERT_TRUE(storage->AddBacklog("uid-3", MessageMethod::Refuel, "{\"TankNumber\":3}"));

    auto backend = std::make_shared<StrictMock<MockBackendForBacklog>>();
    EXPECT_CALL(*backend, CancelPendingRequests()).Times(::testing::AnyNumber());
    EXPECT_CALL(*backend, IsAuthorized()).WillRepeatedly(Return(false));
    EXPECT_CALL(*backend, Authorize("uid-3")).WillOnce(Return(true));
    EXPECT_CALL(*backend, SendReportPayload("{\"TankNumber\":3}", false, false)).WillOnce(Return(false));
    EXPECT_CALL(*backend, Deauthorize()).WillOnce(Return(true));
    EXPECT_CALL(*backend, IsNetworkError()).WillOnce(Return(false));

    BacklogWorker worker(storage, backend, std::chrono::milliseconds(1));
    EXPECT_TRUE(worker.ProcessOnce());
    EXPECT_EQ(storage->BacklogCount(), 0);
    EXPECT_EQ(storage->DeadMessageCount(), 1);
}

TEST(BacklogWorkerTest, RetriesStartupRecoveryBeforeSendingAnyReport) {
    const auto path = std::filesystem::temp_directory_path() /
        ("fuelflux-recovery-" + std::to_string(std::random_device{}()) + ".db");
    struct RemoveDatabase {
        std::filesystem::path path;
        ~RemoveDatabase() { std::error_code error; std::filesystem::remove(path, error); }
    } removeDatabase{path};
    auto storage = std::make_shared<MessageStorage>(path.string());
    ASSERT_TRUE(storage->AddBacklog("first", MessageMethod::Refuel, "first-payload"));
    ASSERT_TRUE(storage->ClaimNextBacklog());
    ASSERT_TRUE(storage->AddBacklog("second", MessageMethod::Refuel, "second-payload"));
    sqlite3* database = nullptr;
    ASSERT_EQ(sqlite3_open(path.string().c_str(), &database), SQLITE_OK);
    struct CloseDatabase { sqlite3* db; ~CloseDatabase() { sqlite3_close(db); } } closeDatabase{database};
    ASSERT_EQ(sqlite3_exec(database,
        "CREATE TRIGGER block_recovery BEFORE UPDATE ON backlog WHEN OLD.in_flight=1 AND NEW.in_flight=0 BEGIN SELECT RAISE(ABORT,'temporarily unavailable'); END",
        nullptr, nullptr, nullptr), SQLITE_OK);
    auto backend = std::make_shared<StrictMock<MockBackendForBacklog>>();
    std::atomic<int> sent{0};
    EXPECT_CALL(*backend, CancelPendingRequests()).Times(::testing::AnyNumber());
    {
        ::testing::InSequence order;
        EXPECT_CALL(*backend, Authorize("first")).WillOnce(Return(true));
        EXPECT_CALL(*backend, SendReportPayload("first-payload", false, false)).WillOnce([&] { ++sent; return true; });
        EXPECT_CALL(*backend, Deauthorize()).WillOnce(Return(true));
        EXPECT_CALL(*backend, Authorize("second")).WillOnce(Return(true));
        EXPECT_CALL(*backend, SendReportPayload("second-payload", false, false)).WillOnce([&] { ++sent; return true; });
        EXPECT_CALL(*backend, Deauthorize()).WillOnce(Return(true));
    }
    BacklogWorker worker(storage, backend, std::chrono::milliseconds(10));
    worker.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_EQ(sent.load(), 0);
    EXPECT_EQ(sqlite3_exec(database, "DROP TRIGGER block_recovery", nullptr, nullptr, nullptr), SQLITE_OK);
    worker.Wake();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (storage->BacklogCount() != 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    worker.Stop();
    EXPECT_EQ(sent.load(), 2);
    EXPECT_EQ(storage->BacklogCount(), 0);
}
