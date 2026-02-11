// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "backlog_worker.h"
#include "message_storage.h"

using namespace fuelflux;
using ::testing::Return;
using ::testing::StrictMock;

class MockBackendForBacklog : public IBackend {
public:
    MOCK_METHOD(bool, Authorize, (const std::string& uid), (override));
    MOCK_METHOD(void, Deauthorize, (), (override));
    MOCK_METHOD(bool, Refuel, (TankNumber tankNumber, Volume volume), (override));
    MOCK_METHOD(bool, Intake, (TankNumber tankNumber, Volume volume, IntakeDirection direction), (override));
    MOCK_METHOD(bool, RefuelPayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IntakePayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IsAuthorized, (), (const, override));
    MOCK_METHOD(const std::string&, GetToken, (), (const, override));
    MOCK_METHOD(int, GetRoleId, (), (const, override));
    MOCK_METHOD(double, GetAllowance, (), (const, override));
    MOCK_METHOD(double, GetPrice, (), (const, override));
    MOCK_METHOD(const std::vector<BackendTankInfo>&, GetFuelTanks, (), (const, override));
    MOCK_METHOD(const std::string&, GetLastError, (), (const, override));
    MOCK_METHOD(bool, IsNetworkError, (), (const, override));
};

TEST(BacklogWorkerTest, ProcessesBacklogSuccessfully) {
    auto storage = std::make_shared<MessageStorage>(":memory:");
    ASSERT_TRUE(storage->AddBacklog("uid-1", MessageMethod::Refuel, "{\"TankNumber\":1}"));

    auto backend = std::make_shared<StrictMock<MockBackendForBacklog>>();
    EXPECT_CALL(*backend, Authorize("uid-1")).WillOnce(Return(true));
    EXPECT_CALL(*backend, RefuelPayload("{\"TankNumber\":1}")).WillOnce(Return(true));
    EXPECT_CALL(*backend, Deauthorize()).Times(1);

    BacklogWorker worker(storage, backend, std::chrono::milliseconds(1));
    EXPECT_TRUE(worker.ProcessOnce());
    EXPECT_EQ(storage->BacklogCount(), 0);
    EXPECT_EQ(storage->DeadMessageCount(), 0);
}

TEST(BacklogWorkerTest, KeepsBacklogOnNetworkError) {
    auto storage = std::make_shared<MessageStorage>(":memory:");
    ASSERT_TRUE(storage->AddBacklog("uid-2", MessageMethod::Refuel, "{\"TankNumber\":2}"));

    auto backend = std::make_shared<StrictMock<MockBackendForBacklog>>();
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
    EXPECT_CALL(*backend, Authorize("uid-3")).WillOnce(Return(true));
    EXPECT_CALL(*backend, RefuelPayload("{\"TankNumber\":3}")).WillOnce(Return(false));
    EXPECT_CALL(*backend, Deauthorize()).Times(1);
    EXPECT_CALL(*backend, IsNetworkError()).WillOnce(Return(false));

    BacklogWorker worker(storage, backend, std::chrono::milliseconds(1));
    EXPECT_FALSE(worker.ProcessOnce());
    EXPECT_EQ(storage->BacklogCount(), 0);
    EXPECT_EQ(storage->DeadMessageCount(), 1);
}
