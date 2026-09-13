#include <gtest/gtest.h>
#include "controller.h"
#include "cache_manager.h"
#include "backend_utils.h"
#include <filesystem>
#include <random>
#include <map>
#include <fstream>
#include <future>
#include <sqlite3.h>
#include "display/st_bitmap_text.h"
#include "peripherals/keyboard_utils.h"

using namespace fuelflux;
using namespace std::chrono_literals;
namespace {
struct Network {
    std::atomic<bool> holdAuth{false}, holdReports{false}, ignoreCancellation{false};
    std::atomic<bool> rejectAuth{false}, rejectReports{false}, failReports{false};
    std::atomic<int> tankId{42};
    std::atomic<int> reportCalls{0}, closedSessions{0};
    std::mutex mutex;
    std::map<std::string, int> authorizations;
    std::vector<nlohmann::json> reports;
    int AuthCount(const std::string& uid) {
        std::lock_guard<std::mutex> lock(mutex); return authorizations[uid];
    }
};

class TestBackend : public BackendBase {
public:
    explicit TestBackend(std::shared_ptr<Network> network)
        : BackendBase("test-controller", nullptr), network_(std::move(network)) {}
    std::shared_ptr<IBackend> CreateIndependentSession() const override {
        return std::make_shared<TestBackend>(network_);
    }
private:
    nlohmann::json HttpRequestWrapper(const std::string& endpoint, const std::string&,
                                      const nlohmann::json& body, bool) override {
        networkError_ = false;
        if (endpoint == "/api/pump/authorize") {
            const auto uid = body.at("CardUid").get<std::string>();
            { std::lock_guard<std::mutex> lock(network_->mutex); ++network_->authorizations[uid]; }
            while (network_->holdAuth && (network_->ignoreCancellation || !cancelled_))
                std::this_thread::sleep_for(2ms);
            if (cancelled_ && !network_->ignoreCancellation) { networkError_ = true; return BuildWrapperErrorResponse(); }
            if (network_->rejectAuth) return {{"CodeError", 1}, {"TextError", "Denied"}};
            return {{"Token", "token-" + uid}, {"RoleId", uid == "operator" ? 2 : 1}, {"Allowance", 100.0},
                    {"fuelTanks", nlohmann::json::array({{{"idTank", network_->tankId.load()}, {"visualNumberTank", 7}, {"volume", 500.0}}})}};
        }
        ++network_->reportCalls;
        { std::lock_guard<std::mutex> lock(network_->mutex); network_->reports.push_back(body); }
        while (network_->holdReports && !cancelled_) std::this_thread::sleep_for(2ms);
        if (cancelled_ || network_->failReports) { networkError_ = true; return BuildWrapperErrorResponse(); }
        if (network_->rejectReports) return {{"CodeError", 1}, {"TextError", "Rejected"}};
        return nullptr;
    }
    nlohmann::json HttpRequestWrapper(const std::string& e, const std::string& m,
                                      const nlohmann::json& b, const std::string&) override {
        return HttpRequestWrapper(e, m, b, true);
    }
    void CancelPendingRequests() override { BackendBase::CancelPendingRequests(); }
    void SendAsyncDeauthorizeRequest(const std::string&) override { ++network_->closedSessions; }
    std::shared_ptr<Network> network_;
};

class TestPump : public peripherals::IPump {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    bool isConnected() const override { return true; }
    void start() override { running = true; ++starts; }
    void stop() override { running = false; }
    bool isRunning() const override { return running.load(); }
    void setPumpStateCallback(PumpStateCallback) override {}
    std::atomic<bool> running{false};
    std::atomic<int> starts{0};
};

class TestDisplay : public peripherals::IDisplay {
public:
    bool initialize() override { return true; }
    void shutdown() override { ++shutdownCalls; }
    bool isConnected() const override { return true; }
    void showMessage(const DisplayMessage&) override { if (onShow) onShow(); }
    void clear() override {}
    void setBacklight(bool) override {}
    std::function<void()> onShow;
    std::atomic<int> shutdownCalls{0};
};

class ForegroundBackendTest : public ::testing::Test {
protected:
    std::shared_ptr<Network> network = std::make_shared<Network>();
    std::filesystem::path directory;
    std::unique_ptr<Controller> controller;
    std::unique_ptr<MessageStorage> storage;
    TestPump* pump = nullptr;
    std::thread thread;
    void SetUp() override {
        directory = std::filesystem::temp_directory_path() /
            ("fuelflux-foreground-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directory(directory);
    }
    void Start(std::chrono::milliseconds threshold = 50ms, bool saved = false, bool startLoop = true) {
        // A deliberately unavailable general cache keeps these tests entirely
        // local. Protected snapshots exercise the same production fallback path.
        std::ofstream(directory / "no-cache") << "not a directory";
        controller = std::make_unique<Controller>("test-controller", std::make_shared<TestBackend>(network),
            30s, ControllerPersistencePaths{(directory / "no-cache" / "cache.db").string(),
                                           (directory / "reports.db").string()}, threshold);
        storage = std::make_unique<MessageStorage>((directory / "reports.db").string());
        auto testPump = std::make_unique<TestPump>();
        pump = testPump.get();
        controller->setPump(std::move(testPump));
        if (saved) Save();
        ASSERT_TRUE(controller->initialize());
        if (startLoop) thread = std::thread([this] { controller->run(); });
    }
    void TearDown() override {
        network->holdAuth = false;
        network->holdReports = false;
        if (controller) controller->shutdown();
        if (thread.joinable()) thread.join();
        controller.reset(); storage.reset();
        std::error_code ec; std::filesystem::remove_all(directory, ec);
    }
    bool Wait(std::function<bool()> condition, std::chrono::milliseconds timeout = 2000ms) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) return true;
            std::this_thread::sleep_for(5ms);
        }
        return condition();
    }
    bool State(SystemState state) { return Wait([&] { return controller->getStateMachine().isInState(state); }); }
    void Save(std::string uid = "card") {
        AuthorizationSnapshot saved{{uid, UserRole::Customer, 80, 0}, {{42, 7, "Saved tank", 500}}};
        ASSERT_TRUE(storage->EnqueueReport(MessageMethod::Refuel, "{}", saved, 0));
        auto report = storage->ClaimNextBacklog();
        ASSERT_TRUE(report);
        ASSERT_TRUE(storage->CompleteDelivery(*report, MessageStorage::DeliveryResult::Accepted));
    }
    void Scan(const std::string& uid) { controller->handleCardPresented(uid); }
    void Refuel(double volume = 10) {
        const auto starts = pump->starts.load();
        controller->enterVolume(volume);
        ASSERT_TRUE(State(SystemState::Refueling));
        ASSERT_TRUE(Wait([&] { return pump->starts.load() > starts; }));
        controller->handleFlowUpdate(volume);
        controller->postEvent(Event::RefuelingStopped);
    }
};
}

TEST_F(ForegroundBackendTest, AutomaticFallbackDiscardsLateOnlineSuccess) {
    Start(350ms, true); network->holdAuth = true; network->ignoreCancellation = true;
    const auto started = std::chrono::steady_clock::now();
    Scan("card");
    ASSERT_TRUE(Wait([&] { return network->AuthCount("card") == 1; }));
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Authorization);
    ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_GE(std::chrono::steady_clock::now() - started, 350ms);
    EXPECT_TRUE(controller->isSessionAuthorizedFromCache());
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 80);
    network->holdAuth = false;
    ASSERT_TRUE(Wait([&] { return network->closedSessions.load() == 1; }));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 80);
    EXPECT_DOUBLE_EQ(storage->GetProtectedSnapshot("card")->authorization.user.allowance, 80);
}

TEST_F(ForegroundBackendTest, MissingCacheShowsWarningAndCancelAllowsDifferentCard) {
    Start(350ms); network->holdAuth = true; network->ignoreCancellation = true;
    const auto started = std::chrono::steady_clock::now();
    Scan("unknown");
    ASSERT_TRUE(Wait([&] { return network->AuthCount("unknown") == 1; }));
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(controller->isAuthorizationSlow());
    ASSERT_TRUE(Wait([&] { return controller->isAuthorizationSlow(); }));
    EXPECT_GE(std::chrono::steady_clock::now() - started, 350ms);
    const auto display = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(display.line1, "Медленное соединение");
    EXPECT_EQ(display.line3, "Ожидайте или");
    EXPECT_NE(display.line4.find("ОТМЕНА"), std::string::npos);
    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(State(SystemState::Waiting));
    network->holdAuth = false;
    Scan("next");
    ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_EQ(controller->getCurrentUser().uid, "next");
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
    ASSERT_TRUE(Wait([&] { return network->closedSessions.load() >= 1; }));
    EXPECT_EQ(controller->getCurrentUser().uid, "next");
}

TEST_F(ForegroundBackendTest, ConfiguredThresholdAlsoControlsReportReleaseAndSameCardUsesCache) {
    Start(350ms);
    Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
    network->holdReports = true;
    const auto started = std::chrono::steady_clock::now();
    Refuel();
    ASSERT_TRUE(State(SystemState::RefuelingComplete));
    EXPECT_GE(std::chrono::steady_clock::now() - started, 350ms);
    ASSERT_EQ(network->reportCalls.load(), 1);
    EXPECT_TRUE(storage->HasPendingReports("card"));
    Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_TRUE(controller->isSessionAuthorizedFromCache());
    EXPECT_EQ(network->AuthCount("card"), 1);
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 90);
    network->holdReports = false;
    ASSERT_TRUE(Wait([&] { return !storage->HasPendingReports("card"); }));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 90);
    EXPECT_EQ(network->reportCalls.load(), 1);
    { std::lock_guard<std::mutex> lock(network->mutex); EXPECT_EQ(network->reports[0].at("TankNumber"), 42); }
    controller->postEvent(Event::CancelPressed); ASSERT_TRUE(State(SystemState::Waiting));
    Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
    EXPECT_EQ(network->AuthCount("card"), 2);
}

TEST_F(ForegroundBackendTest, LateReportRejectionDoesNotAffectDifferentCard) {
    Start(); Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    network->holdReports = true; network->rejectReports = true;
    Refuel(); ASSERT_TRUE(State(SystemState::RefuelingComplete));
    Scan("other"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
    network->holdReports = false;
    ASSERT_TRUE(Wait([&] { return storage->DeadMessageCount() == 1; }));
    EXPECT_EQ(controller->getCurrentUser().uid, "other");
    EXPECT_FALSE(storage->HasPendingReports("card"));
    controller->postEvent(Event::CancelPressed); ASSERT_TRUE(State(SystemState::Waiting));
    Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
}

TEST_F(ForegroundBackendTest, ShutdownCancelsBlockedAuthorization) {
    Start(); network->holdAuth = true; Scan("card");
    ASSERT_TRUE(Wait([&] { return network->AuthCount("card") == 1; }));
    const auto started = std::chrono::steady_clock::now();
    EXPECT_TRUE(controller->shutdown());
    thread.join();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 1500ms);
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
}

TEST_F(ForegroundBackendTest, ShutdownDuringReportingRestoresPendingCardAllowance) {
    Start(); Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    network->holdReports = true;
    Refuel(); ASSERT_TRUE(State(SystemState::RefuelingComplete));
    ASSERT_EQ(network->reportCalls.load(), 1);
    EXPECT_TRUE(controller->shutdown());
    thread.join();
    controller.reset(); storage.reset();
    Start(); Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_TRUE(controller->isSessionAuthorizedFromCache());
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 90);
    EXPECT_TRUE(storage->HasPendingReports("card"));
}

TEST_F(ForegroundBackendTest, ShutdownBeforeRunPreventsQueuedPeripheralActions) {
    Start(50ms, false, false);
    auto display = std::make_unique<TestDisplay>();
    std::atomic<int> calls{0};
    display->onShow = [&] { ++calls; };
    controller->setDisplay(std::move(display));
    controller->postEvent(Event::FlowDisplayRefresh);
    std::promise<void> launch;
    auto ready = launch.get_future();
    thread = std::thread([&, ready = std::move(ready)]() mutable { ready.wait(); controller->run(); });
    EXPECT_TRUE(controller->shutdown());
    launch.set_value();
    thread.join();
    EXPECT_EQ(calls.load(), 0);
}

TEST_F(ForegroundBackendTest, UnexpectedLoopExitStillAllowsShutdown) {
    Start(50ms, false, false);
    auto display = std::make_unique<TestDisplay>();
    auto* displayPtr = display.get();
    display->onShow = [] { throw std::runtime_error("display failure"); };
    controller->setDisplay(std::move(display));
    std::atomic<bool> exited{false};
    thread = std::thread([&] {
        try { controller->run(); } catch (const std::runtime_error&) { exited = true; }
    });
    controller->postEvent(Event::FlowDisplayRefresh);
    EXPECT_TRUE(Wait([&] { return exited.load(); }));
    EXPECT_TRUE(controller->shutdown());
    thread.join();
    EXPECT_EQ(displayPtr->shutdownCalls.load(), 1);
}

TEST_F(ForegroundBackendTest, ShutdownDeadlinePreservesLiveStateAndAllowsRetry) {
    Start(50ms, false, false);
    auto display = std::make_unique<TestDisplay>();
    auto* displayPtr = display.get();
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<bool> entered{false};
    display->onShow = [&, released] { entered = true; released.wait(); };
    controller->setDisplay(std::move(display));
    thread = std::thread([this] { controller->run(); });
    controller->postEvent(Event::FlowDisplayRefresh);
    EXPECT_TRUE(Wait([&] { return entered.load(); }));
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(controller->shutdown());
    EXPECT_LT(std::chrono::steady_clock::now() - started, timing::kShutdownDeadline + 1s);
    EXPECT_EQ(displayPtr->shutdownCalls.load(), 0);
    release.set_value();
    thread.join();
    EXPECT_TRUE(controller->shutdown());
    EXPECT_EQ(displayPtr->shutdownCalls.load(), 1);
}

TEST_F(ForegroundBackendTest, SynchronousOnlyBackendIsRejectedBeforeControllerUse) {
    class SynchronousBackend : public TestBackend {
    public:
        using TestBackend::TestBackend;
        std::shared_ptr<IBackend> CreateIndependentSession() const override { return {}; }
    };
    EXPECT_THROW(Controller("controller", std::make_shared<SynchronousBackend>(network), 30s,
        ControllerPersistencePaths{(directory / "cache.db").string(), (directory / "reports.db").string()}),
        std::invalid_argument);
}

TEST_F(ForegroundBackendTest, PendingReportsAccumulateDeductionsAndLateRepliesDoNotRestoreThem) {
    Start(); Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    network->holdReports = true;
    Refuel(10); ASSERT_TRUE(State(SystemState::RefuelingComplete));
    Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    Refuel(5); ASSERT_TRUE(State(SystemState::RefuelingComplete));
    EXPECT_EQ(network->reportCalls.load(), 1);
    Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 85);
    EXPECT_EQ(network->AuthCount("card"), 1);
    network->holdReports = false;
    ASSERT_TRUE(Wait([&] { return !storage->HasPendingReports("card"); }));
    EXPECT_EQ(network->reportCalls.load(), 2);
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 85);
    EXPECT_DOUBLE_EQ(storage->GetProtectedSnapshot("card")->authorization.user.allowance, 85);
}

TEST_F(ForegroundBackendTest, EarlyDenialDoesNotUseSavedData) {
    Start(350ms, true); network->rejectAuth = true;
    Scan("card"); ASSERT_TRUE(State(SystemState::NotAuthorized));
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
    EXPECT_FALSE(controller->isAuthorizationSlow());
}

TEST_F(ForegroundBackendTest, CancelBeforeThresholdCannotCancelLaterSuccess) {
    Start(350ms); network->holdAuth = true;
    Scan("card"); ASSERT_TRUE(Wait([&] { return network->AuthCount("card") == 1; }));
    controller->postEvent(Event::CancelPressed);
    network->holdAuth = false;
    ASSERT_TRUE(State(SystemState::VolumeEntry));
    EXPECT_FALSE(controller->isSessionAuthorizedFromCache());
}

TEST_F(ForegroundBackendTest, SlowIntakeReleasesScreenAndRetainsCanonicalPayload) {
    Start(); Scan("operator"); ASSERT_TRUE(State(SystemState::IntakeDirectionSelection));
    controller->handleKeyPress(KeyCode::Key1);
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(State(SystemState::IntakeVolumeEntry));
    network->holdReports = true;
    controller->enterIntakeVolume(12.5);
    ASSERT_TRUE(State(SystemState::IntakeComplete));
    Scan("operator"); ASSERT_TRUE(State(SystemState::IntakeDirectionSelection));
    EXPECT_TRUE(controller->isSessionAuthorizedFromCache());
    network->holdReports = false;
    ASSERT_TRUE(Wait([&] { return !storage->HasPendingReports("operator"); }));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::IntakeDirectionSelection);
    std::lock_guard<std::mutex> lock(network->mutex);
    ASSERT_EQ(network->reports.size(), 1u);
    EXPECT_EQ(network->reports[0].at("IntakeVolume"), 12.5);
    EXPECT_EQ(network->reports[0].at("TankNumber"), 42);
}

TEST_F(ForegroundBackendTest, FailedPersistenceShowsStorageErrorWithoutReleasingTransaction) {
    Start(); Scan("card"); ASSERT_TRUE(State(SystemState::VolumeEntry));
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open((directory / "reports.db").string().c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TRIGGER fail_report BEFORE INSERT ON backlog BEGIN SELECT RAISE(ABORT,'full'); END", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
    Refuel();
    ASSERT_TRUE(State(SystemState::Error));
    EXPECT_EQ(controller->getLastErrorMessage(), "Ошибка записи");
    EXPECT_EQ(storage->BacklogCount(), 0);
    EXPECT_FALSE(storage->GetProtectedSnapshot("card"));
    EXPECT_EQ(network->reportCalls.load(), 0);
}

TEST(ForegroundDisplayTest, WarningAndBothCancelLabelsFitSmallDisplay) {
    fuelflux::display::StBitmapText font(fuelflux::display::StBitmapFontSize::Small6x12);
    EXPECT_EQ(font.fittedGlyphCount("Медленное соединение", 124), 20u);
    for (const auto type : {fuelflux::peripherals::KeyboardType::Legacy, fuelflux::peripherals::KeyboardType::Vid}) {
        const std::string prompt(fuelflux::peripherals::keyboardUiProfile(type).cancelPrompt);
        const auto count = font.fittedGlyphCount(prompt, 124);
        EXPECT_EQ(count, type == fuelflux::peripherals::KeyboardType::Vid ? 14u : 18u);
    }
    EXPECT_GT(timing::kForegroundBackendWaitTimeout.count(), 0);
}

TEST(ReportWorkerTest, NetworkRetryKeepsCanonicalTankTimestampAndSingleDeduction) {
    auto network = std::make_shared<Network>();
    auto storage = std::make_shared<MessageStorage>(":memory:");
    auto backend = std::make_shared<TestBackend>(network);
    BacklogWorker worker(storage, backend, 1ms);
    AuthorizationSnapshot snapshot{{"card", UserRole::Customer, 100, 0}, {{42, 7, "Saved tank", 500}}};
    const std::string payload = R"({"TankNumber":42,"FuelVolume":10,"TimeAt":123456})";
    ASSERT_TRUE(worker.Submit(MessageMethod::Refuel, payload, snapshot, 10));
    network->failReports = true;
    EXPECT_FALSE(worker.ProcessOnce());
    EXPECT_TRUE(storage->HasPendingReports("card"));
    network->failReports = false;
    network->tankId = 99;
    EXPECT_TRUE(worker.ProcessOnce());
    EXPECT_FALSE(storage->HasPendingReports("card"));
    EXPECT_DOUBLE_EQ(storage->GetProtectedSnapshot("card")->authorization.user.allowance, 90);
    std::lock_guard<std::mutex> lock(network->mutex);
    ASSERT_EQ(network->reports.size(), 2u);
    EXPECT_EQ(network->reports[0], nlohmann::json::parse(payload));
    EXPECT_EQ(network->reports[1], network->reports[0]);
}
