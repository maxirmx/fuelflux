// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"
#include "input_health.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct nfc_context;
struct nfc_device;

namespace fuelflux::peripherals {

class HardwareCardReader : public ICardReader {
public:
    struct PollResult { int status = 0; std::string uid; };
    struct Transport {
        std::function<void()> open;
        std::function<PollResult()> poll;
        std::function<void()> close;
    };
    explicit HardwareCardReader(const std::string& connstring = "");
    explicit HardwareCardReader(Transport transport);
    ~HardwareCardReader() override;
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    void setCardPresentedCallback(CardPresentedCallback callback) override;
    void enableReading(bool enabled) override;
    std::optional<InputHealth> getInputHealth() const override;

private:
    void pollingLoop();
    Transport transport_;
    InputHealthTracker health_;
    bool monitored_ = false;

    std::atomic<bool> isConnected_;
    std::atomic<bool> readingEnabled_;
    CardPresentedCallback cardPresentedCallback_;
    std::mutex callbackMutex_;
    std::thread pollingThread_;
    std::string connstring_;
    nfc_context* context_;
    nfc_device* device_;
};

} // namespace fuelflux::peripherals
