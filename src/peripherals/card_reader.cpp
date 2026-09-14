// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/card_reader.h"
#include "logger.h"
#include <stdexcept>
#include <utility>

#ifdef TARGET_REAL_CARD_READER
#include "hardware/hardware_config.h"
#include <nfc/nfc.h>

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#endif

namespace fuelflux::peripherals {

#ifdef TARGET_REAL_CARD_READER
namespace {
constexpr auto kReadCooldown = std::chrono::milliseconds(hardware::config::card_reader::READ_COOLDOWN_MS);

std::string toString(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        //    if (i) oss << ":";
        // oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        oss << std::setw(3) << std::setfill('0') << static_cast<int>(data[i]);
        // oss <<  static_cast<int>(data[i]);
    }
    return oss.str();
}

HardwareCardReader::PollResult pollForUid(nfc_device* device) {
    nfc_modulation nm{};
    nm.nmt = NMT_ISO14443A;
    nm.nbr = NBR_106;
    nfc_target target{};

    int res = nfc_initiator_poll_target(device, &nm, 1, 1, 2, &target);
    if (res <= 0) {
        return {res, {}};
    }

    if (target.nm.nmt == NMT_ISO14443A) {
        const auto& nai = target.nti.nai;
        if (nai.szUidLen > 0) {
            return {1, toString(nai.abtUid, nai.szUidLen)};
        }
    }

    return {0, {}};
}
} // namespace
#endif

HardwareCardReader::HardwareCardReader(const std::string& connstring)
    : isConnected_(false), readingEnabled_(false),
      connstring_(connstring), context_(nullptr), device_(nullptr) {}
HardwareCardReader::HardwareCardReader(Transport transport)
    : HardwareCardReader(std::string{}) { transport_ = std::move(transport); }
HardwareCardReader::~HardwareCardReader() { shutdown(); }

bool HardwareCardReader::initialize() {
    if (pollingThread_.joinable()) return true;
#ifdef TARGET_REAL_CARD_READER
    if (!transport_.poll) {
        transport_.open = [this] {
            nfc_init(&context_);
            if (!context_) throw std::runtime_error("NFC initialization failed");
            const auto connection = connstring_.empty()
                ? std::string("pn532_i2c:") + hardware::config::card_reader::I2C_DEVICE : connstring_;
            device_ = nfc_open(context_, connection.c_str());
            if (!device_) throw std::runtime_error("Unable to open NFC device");
            if (nfc_device_set_property_int(device_, NP_TIMEOUT_COMMAND,
                    static_cast<int>(timing::kInputCommandTimeout.count())) < 0 || nfc_initiator_init(device_) < 0)
                throw std::runtime_error(nfc_strerror(device_));
        };
        transport_.poll = [this] { return pollForUid(device_); };
        transport_.close = [this] {
            if (device_) { nfc_close(device_); device_ = nullptr; }
            if (context_) { nfc_exit(context_); context_ = nullptr; }
        };
    }
#endif
    monitored_ = static_cast<bool>(transport_.poll);
    if (!monitored_) { isConnected_ = true; return true; }
    health_.start();
    pollingThread_ = std::thread(&HardwareCardReader::pollingLoop, this);
    return true;
}
void HardwareCardReader::shutdown() {
    readingEnabled_ = false;
    health_.stop();
    if (pollingThread_.joinable()) pollingThread_.join();
    isConnected_ = false;
}
bool HardwareCardReader::isConnected() const { return isConnected_; }
std::optional<InputHealth> HardwareCardReader::getInputHealth() const {
    return monitored_ ? std::optional<InputHealth>(health_.snapshot()) : std::nullopt;
}
void HardwareCardReader::setCardPresentedCallback(CardPresentedCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    cardPresentedCallback_ = std::move(callback);
}
void HardwareCardReader::enableReading(bool enabled) { readingEnabled_ = enabled; }
void HardwareCardReader::pollingLoop() {
    auto delay = std::chrono::seconds(1);
    auto nextDelivery = std::chrono::steady_clock::time_point{};
    while (!health_.stopped()) {
        try {
            if (!isConnected_) {
                if (transport_.open) transport_.open();
            }
            // Communication continues while delivery is disabled; no target is
            // a successful poll, not evidence of an unhealthy device.
            auto result = transport_.poll();
            if (result.status < 0) throw std::runtime_error("NFC poll error " + std::to_string(result.status));
            if (!isConnected_) {
                health_.connected();
                isConnected_ = true;
                LOG_INFO("NFC communication available");
            }
            health_.success();
            delay = std::chrono::seconds(1);
            const auto now = std::chrono::steady_clock::now();
            if (result.status > 0 && readingEnabled_ && now >= nextDelivery && !health_.stopped()) {
                CardPresentedCallback callback;
                { std::lock_guard<std::mutex> lock(callbackMutex_); callback = cardPresentedCallback_; }
                if (callback) callback(result.uid);
#ifdef TARGET_REAL_CARD_READER
                nextDelivery = now + kReadCooldown;
#else
                nextDelivery = now + std::chrono::seconds(1);
#endif
            }
            health_.wait(std::chrono::milliseconds(50));
        } catch (const std::exception& error) {
            isConnected_ = false;
            health_.failure(error.what());
            LOG_ERROR("NFC I/O failed; retry in {} seconds: {}", delay.count(), error.what());
            if (transport_.close) transport_.close();
            if (health_.wait(delay)) break;
            delay = InputHealthTracker::nextDelay(delay);
        }
    }
    if (transport_.close) transport_.close();
    isConnected_ = false;
}
} // namespace fuelflux::peripherals
