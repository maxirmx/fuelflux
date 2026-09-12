#include <gtest/gtest.h>
#include "message_storage.h"
#include <sqlite3.h>
#include <filesystem>
#include <random>

using namespace fuelflux;
namespace {
AuthorizationSnapshot SavedCard(std::string uid = "card", double allowance = 100.0) {
    return {{std::move(uid), UserRole::Customer, allowance, 0.0}, {{42, 7, "Tank", 500.0}}};
}
struct TempDatabase {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("fuelflux-delivery-" + std::to_string(std::random_device{}()) + ".db");
    ~TempDatabase() { std::error_code ec; std::filesystem::remove(path, ec); }
};
}

TEST(ReportDeliveryTest, RetryKeepsPayloadAndDeductsOnlyOnce) {
    MessageStorage storage(":memory:");
    auto id = storage.EnqueueReport(MessageMethod::Refuel, "{\"TankNumber\":42,\"TimeAt\":123}", SavedCard(), 10);
    ASSERT_TRUE(id);
    auto card = storage.GetProtectedSnapshot("card");
    ASSERT_TRUE(card);
    EXPECT_TRUE(card->pending);
    EXPECT_DOUBLE_EQ(card->authorization.user.allowance, 90);
    auto first = storage.ClaimNextBacklog();
    ASSERT_TRUE(first);
    EXPECT_TRUE(first->canonicalTankId);
    EXPECT_FALSE(storage.ClaimNextBacklog());
    ASSERT_TRUE(storage.CompleteDelivery(*first, MessageStorage::DeliveryResult::Retry, 0));
    auto retry = storage.ClaimNextBacklog();
    ASSERT_TRUE(retry);
    EXPECT_GT(retry->attempt, first->attempt);
    EXPECT_EQ(retry->data, first->data);
    EXPECT_FALSE(storage.CompleteDelivery(*first, MessageStorage::DeliveryResult::Accepted));
    EXPECT_TRUE(storage.HasPendingReports("card"));
    ASSERT_TRUE(storage.CompleteDelivery(*retry, MessageStorage::DeliveryResult::Accepted));
    EXPECT_FALSE(storage.HasPendingReports("card"));
    EXPECT_DOUBLE_EQ(storage.GetProtectedSnapshot("card")->authorization.user.allowance, 90);
    auto second = storage.EnqueueReport(MessageMethod::Refuel, "{}", card->authorization, 5);
    ASSERT_TRUE(second);
    EXPECT_GT(*second, *id);
    EXPECT_DOUBLE_EQ(storage.GetProtectedSnapshot("card")->authorization.user.allowance, 85);
}

TEST(ReportDeliveryTest, AtomicRollbackWhenReportInsertFails) {
    TempDatabase file;
    MessageStorage storage(file.path.string());
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(file.path.string().c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TRIGGER fail_insert BEFORE INSERT ON backlog BEGIN SELECT RAISE(ABORT,'full'); END", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_FALSE(storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard(), 10));
    EXPECT_FALSE(storage.GetProtectedSnapshot("card"));
    EXPECT_EQ(storage.BacklogCount(), 0);
    sqlite3_close(db);
}

TEST(ReportDeliveryTest, AuthorizationSnapshotIsImmutableAndPrefersPendingCardData) {
    MessageStorage storage(":memory:");
    auto general = SavedCard("card", 500);
    ASSERT_TRUE(storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard(), 10));
    const auto captured = storage.CaptureAuthorizationState("card", [&] { return general; });
    ASSERT_TRUE(captured.saved);
    EXPECT_TRUE(captured.reportStorageAvailable);
    EXPECT_TRUE(captured.pendingReports);
    EXPECT_DOUBLE_EQ(captured.saved->user.allowance, 90);
    storage.EnqueueReport(MessageMethod::Refuel, "{}", *captured.saved, 5);
    general.user.allowance = 1000;
    EXPECT_DOUBLE_EQ(captured.saved->user.allowance, 90);
    EXPECT_DOUBLE_EQ(storage.CaptureAuthorizationState("card", [&] { return general; }).saved->user.allowance, 85);
}

TEST(ReportDeliveryTest, AuthorizationSnapshotRequiresWritableReportStorage) {
    TempDatabase file;
    MessageStorage storage(file.path.string());
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(file.path.string().c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TRIGGER fail_write BEFORE UPDATE ON device_settings BEGIN SELECT RAISE(ABORT,'full'); END", nullptr, nullptr, nullptr), SQLITE_OK);
    const auto captured = storage.CaptureAuthorizationState("card", [] { return SavedCard(); });
    EXPECT_FALSE(captured.reportStorageAvailable);
    EXPECT_FALSE(captured.saved);
    sqlite3_close(db);
}

TEST(ReportDeliveryTest, PerCardOrderAndOtherCardsCanProgress) {
    MessageStorage storage(":memory:");
    auto a = storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard(), 10);
    storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard("card", 90), 10);
    auto b = storage.EnqueueReport(MessageMethod::Intake, "{}", SavedCard("other"), 0);
    auto first = storage.ClaimNextBacklog();
    ASSERT_TRUE(first); EXPECT_EQ(first->id, a);
    auto other = storage.ClaimNextBacklog();
    ASSERT_TRUE(other); EXPECT_EQ(other->id, b);
    EXPECT_FALSE(storage.ClaimNextBacklog());
    ASSERT_TRUE(storage.CompleteDelivery(*first, MessageStorage::DeliveryResult::Rejected));
    EXPECT_EQ(storage.DeadMessageCount(), 1);
    EXPECT_TRUE(storage.HasPendingReports("card"));
    auto next = storage.ClaimNextBacklog();
    ASSERT_TRUE(next);
    ASSERT_TRUE(storage.CompleteDelivery(*next, MessageStorage::DeliveryResult::Rejected));
    EXPECT_FALSE(storage.HasPendingReports("card"));
}

TEST(ReportDeliveryTest, RestartInvalidatesOldAttemptsAndPreservesBalance) {
    TempDatabase file;
    StoredMessage old;
    {
        MessageStorage storage(file.path.string());
        ASSERT_TRUE(storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard(), 12));
        old = *storage.ClaimNextBacklog();
    }
    {
        MessageStorage storage(file.path.string());
        EXPECT_FALSE(storage.ClaimNextBacklog());
        ASSERT_TRUE(storage.RecoverInFlight());
        EXPECT_FALSE(storage.CompleteDelivery(old, MessageStorage::DeliveryResult::Accepted));
        ASSERT_TRUE(storage.RecoverInFlight());
        auto resumed = storage.ClaimNextBacklog();
        ASSERT_TRUE(resumed);
        EXPECT_EQ(resumed->id, old.id);
        EXPECT_EQ(resumed->attempt, old.attempt + 1);
        EXPECT_FALSE(storage.CompleteDelivery(old, MessageStorage::DeliveryResult::Accepted));
        EXPECT_DOUBLE_EQ(storage.GetProtectedSnapshot("card")->authorization.user.allowance, 88);
    }
}

TEST(ReportDeliveryTest, StaleSynchronizationCannotReleaseLocalAllowance) {
    MessageStorage storage(":memory:");
    storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard(), 10);
    EXPECT_TRUE(storage.ResolvedSnapshotVersions().empty());
    auto first = storage.ClaimNextBacklog();
    ASSERT_TRUE(first);
    storage.CompleteDelivery(*first, MessageStorage::DeliveryResult::Accepted);
    const auto versions = storage.ResolvedSnapshotVersions();
    ASSERT_EQ(versions.size(), 1u);
    storage.ClearProtectedSnapshot("card");
    storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard("card", 90), 10);
    auto next = storage.ClaimNextBacklog();
    storage.CompleteDelivery(*next, MessageStorage::DeliveryResult::Accepted);
    storage.ReleaseResolvedSnapshots(versions);
    ASSERT_TRUE(storage.GetProtectedSnapshot("card"));
    EXPECT_DOUBLE_EQ(storage.GetProtectedSnapshot("card")->authorization.user.allowance, 80);
    storage.ReleaseResolvedSnapshots(storage.ResolvedSnapshotVersions());
    EXPECT_FALSE(storage.GetProtectedSnapshot("card"));
}

TEST(ReportDeliveryTest, MigratesLegacyPayloadsAndOrder) {
    TempDatabase file;
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(file.path.string().c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE backlog(uid TEXT,method TEXT,data TEXT); INSERT INTO backlog(rowid,uid,method,data) VALUES(4,'a','Refuel','legacy-a'),(9,'b','Intake','legacy-b')", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
    MessageStorage storage(file.path.string());
    auto a = storage.ClaimNextBacklog();
    ASSERT_TRUE(a); EXPECT_EQ(a->id, 4); EXPECT_EQ(a->data, "legacy-a"); EXPECT_FALSE(a->canonicalTankId);
    auto b = storage.ClaimNextBacklog();
    ASSERT_TRUE(b); EXPECT_EQ(b->id, 9); EXPECT_EQ(b->data, "legacy-b");
    storage.CompleteDelivery(*a, MessageStorage::DeliveryResult::Accepted);
    storage.CompleteDelivery(*b, MessageStorage::DeliveryResult::Accepted);
    ASSERT_TRUE(storage.AddBacklog("c", MessageMethod::Refuel, "new"));
    EXPECT_GT(storage.GetNextBacklog()->id, 9);
}

TEST(ReportDeliveryTest, FreshAuthorizationCannotExposeOlderSynchronizationData) {
    MessageStorage storage(":memory:");
    storage.EnqueueReport(MessageMethod::Refuel, "{}", SavedCard(), 10);
    const auto report = storage.ClaimNextBacklog();
    ASSERT_TRUE(report);
    storage.CompleteDelivery(*report, MessageStorage::DeliveryResult::Accepted);
    const auto oldSync = storage.ResolvedSnapshotVersions();
    ASSERT_TRUE(storage.RefreshResolvedSnapshot(SavedCard("card", 70)));
    storage.ReleaseResolvedSnapshots(oldSync);
    ASSERT_TRUE(storage.GetProtectedSnapshot("card"));
    EXPECT_DOUBLE_EQ(storage.GetProtectedSnapshot("card")->authorization.user.allowance, 70);
    EXPECT_FALSE(storage.HasPendingReports("card"));
    storage.ReleaseResolvedSnapshots(storage.ResolvedSnapshotVersions());
    EXPECT_FALSE(storage.GetProtectedSnapshot("card"));
}
