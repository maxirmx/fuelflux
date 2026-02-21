// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/card_reader.h"
#include "logger.h"

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
constexpr auto kPollDelay = std::chrono::milliseconds(150);
constexpr auto kReadCooldown = std::chrono::milliseconds(500);

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

std::optional<std::string> pollForUid(nfc_device* device) {
    nfc_modulation nm{};
    nm.nmt = NMT_ISO14443A;
    nm.nbr = NBR_106;
    nfc_target target{};

    int res = nfc_initiator_poll_target(device, &nm, 1, 20, 2, &target);
    if (res <= 0) {
        return std::nullopt;
    }

    if (target.nm.nmt == NMT_ISO14443A) {
        const auto& nai = target.nti.nai;
        if (nai.szUidLen > 0) {
            return toString(nai.abtUid, nai.szUidLen);
        }
    }

    return std::string("<unknown target>");
}
} // namespace
#endif

HardwareCardReader::HardwareCardReader(const std::string& connstring)
    : isConnected_(false)
    , readingEnabled_(false)
    , shouldStop_(false)
    , connstring_(connstring)
    , context_(nullptr)
    , device_(nullptr) {
}

HardwareCardReader::~HardwareCardReader() {
    shutdown();
}

bool HardwareCardReader::initialize() {
#ifdef TARGET_REAL_CARD_READER
    if (isConnected_) {
        return true;
    }

    const std::string connstring = connstring_.empty() 
        ? std::string("pn532_i2c:") + hardware::config::card_reader::I2C_DEVICE
        : connstring_;
    nfc_init(&context_);
    if (!context_) {
        LOG_ERROR("NFC init failed");
        return false;
    }

    device_ = nfc_open(context_, connstring.c_str());
    if (!device_) {
        LOG_ERROR("Unable to open NFC device using connstring: {}", connstring);
        nfc_exit(context_);
        context_ = nullptr;
        return false;
    }

    if (nfc_initiator_init(device_) < 0) {
        LOG_ERROR("nfc_initiator_init failed: {}", nfc_strerror(device_));
        nfc_close(device_);
        nfc_exit(context_);
        device_ = nullptr;
        context_ = nullptr;
        return false;
    }

    LOG_INFO("Opened NFC device: {}", nfc_device_get_name(device_));
    LOG_INFO("Using NFC connstring: {}", connstring);

    isConnected_ = true;
    shouldStop_ = false;
    pollingThread_ = std::thread(&HardwareCardReader::pollingLoop, this);
    return true;
#else
    isConnected_ = true;
    return true;
#endif
}

void HardwareCardReader::shutdown() {
    shouldStop_ = true;
    readingEnabled_ = false;

#ifdef TARGET_REAL_CARD_READER
    if (pollingThread_.joinable()) {
        pollingThread_.join();
    }

    if (device_) {
        nfc_close(device_);
        device_ = nullptr;
    }

    if (context_) {
        nfc_exit(context_);
        context_ = nullptr;
    }
#endif

    isConnected_ = false;
}

bool HardwareCardReader::isConnected() const {
    return isConnected_;
}

void HardwareCardReader::setCardPresentedCallback(CardPresentedCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    cardPresentedCallback_ = callback;
}

void HardwareCardReader::enableReading(bool enabled) {
    readingEnabled_ = enabled;
}

void HardwareCardReader::pollingLoop() {
#ifdef TARGET_REAL_CARD_READER
    while (!shouldStop_) {
        if (!readingEnabled_) {
            std::this_thread::sleep_for(kPollDelay);
            continue;
        }

        if (!device_) {
            std::this_thread::sleep_for(kPollDelay);
            continue;
        }

        auto uid = pollForUid(device_);
        if (uid.has_value() && readingEnabled_) {
			LOG_INFO("Card presented with UID: {}", *uid);
            CardPresentedCallback callback;
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                callback = cardPresentedCallback_;
            }
            if (callback) {
                callback(*uid);
            }
            std::this_thread::sleep_for(kReadCooldown);
        } else {
            std::this_thread::sleep_for(kPollDelay);
        }
    }
#endif
}

} // namespace fuelflux::peripherals
