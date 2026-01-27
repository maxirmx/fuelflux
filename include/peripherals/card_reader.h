#pragma once

#include "peripheral_interface.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct nfc_context;
struct nfc_device;

namespace fuelflux::peripherals {

class HardwareCardReader : public ICardReader {
public:
    explicit HardwareCardReader(const std::string& connstring = "");
    ~HardwareCardReader() override;
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    void setCardPresentedCallback(CardPresentedCallback callback) override;
    void enableReading(bool enabled) override;

private:
    void pollingLoop();
    std::string resolveConnstring() const;

    std::atomic<bool> isConnected_;
    std::atomic<bool> readingEnabled_;
    std::atomic<bool> shouldStop_;
    CardPresentedCallback cardPresentedCallback_;
    std::mutex callbackMutex_;
    std::thread pollingThread_;
    std::string connstring_;
    nfc_context* context_;
    nfc_device* device_;
};

} // namespace fuelflux::peripherals
