#include <gtest/gtest.h>
#include "peripherals/card_reader.h"
#include "peripherals/keyboard.h"
#include <condition_variable>
#include <mutex>
#include <atomic>

using namespace fuelflux::peripherals;
namespace {
struct Progress {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0;
    void tick() { { std::lock_guard<std::mutex> lock(mutex); ++count; } cv.notify_all(); }
    bool await(int n) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(4), [&] { return count >= n; });
    }
};
}
TEST(InputRecovery, KeyboardReopensAfterScanErrorAndJoinsWorker) {
    Progress opens;
    std::atomic<int> scans{0}, closes{0};
    HardwareKeyboard keyboard(HardwareKeyboard::Transport{
        [&] { opens.tick(); },
        [&] {
            if (scans.fetch_add(1) == 2) throw std::runtime_error("injected I2C fault");
            return PhysicalKey::None;
        },
        [&] { ++closes; }
    });
    EXPECT_TRUE(keyboard.initialize());
    EXPECT_TRUE(opens.await(2));
    keyboard.shutdown();
    EXPECT_GE(closes.load(), 2);
    EXPECT_GE(scans.load(), 3);
    EXPECT_FALSE(keyboard.isConnected());
}
TEST(InputRecovery, NfcNoCardAndDisabledDeliveryStillCheckCommunication) {
    Progress polls;
    std::atomic<int> deliveries{0};
    HardwareCardReader reader(HardwareCardReader::Transport{
        [] {}, [&] { polls.tick(); return HardwareCardReader::PollResult{0, {}}; }, [] {}
    });
    reader.setCardPresentedCallback([&](const std::string&) { ++deliveries; });
    EXPECT_TRUE(reader.initialize());
    reader.enableReading(false);
    EXPECT_TRUE(polls.await(4));
    auto health = reader.getInputHealth();
    EXPECT_TRUE(health && health->healthy);
    EXPECT_EQ(deliveries.load(), 0);
    reader.shutdown();
}
TEST(InputRecovery, NfcNegativePollReopensWithoutConcurrentHandleUse) {
    Progress opens;
    std::atomic<int> polls{0}, handles{0};
    HardwareCardReader reader(HardwareCardReader::Transport{
        [&] { EXPECT_EQ(handles.fetch_add(1), 0); opens.tick(); },
        [&] { return HardwareCardReader::PollResult{polls.fetch_add(1) == 1 ? -1 : 0, {}}; },
        [&] { EXPECT_EQ(handles.fetch_sub(1), 1); }
    });
    EXPECT_TRUE(reader.initialize());
    EXPECT_TRUE(opens.await(2));
    reader.shutdown();
    EXPECT_EQ(handles.load(), 0);
}
TEST(InputRecovery, ShutdownInterruptsReconnectBackoff) {
    Progress failures;
    HardwareCardReader reader(HardwareCardReader::Transport{
        [&] { failures.tick(); throw std::runtime_error("unavailable"); },
        [] { return HardwareCardReader::PollResult{}; }, [] {}
    });
    EXPECT_TRUE(reader.initialize());
    EXPECT_TRUE(failures.await(1));
    const auto start = std::chrono::steady_clock::now();
    reader.shutdown();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
}
