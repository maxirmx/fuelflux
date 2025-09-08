#pragma once

#include "peripherals/peripheral_interface.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace fuelflux {

// Console-based display emulation
class ConsoleDisplay : public peripherals::IDisplay {
public:
    ConsoleDisplay();
    ~ConsoleDisplay() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IDisplay implementation
    void showMessage(const DisplayMessage& message) override;
    void clear() override;
    void setBacklight(bool enabled) override;

private:
    bool isConnected_;
    bool backlightEnabled_;
    DisplayMessage currentMessage_;
    mutable std::mutex displayMutex_;

    void printDisplay() const;
    void printTopBorder() const;
    void printBottomBorder() const;
    std::string padLine(const std::string& line, size_t width) const;
};

// Console-based keyboard emulation
class ConsoleKeyboard : public peripherals::IKeyboard {
public:
    ConsoleKeyboard();
    ~ConsoleKeyboard() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IKeyboard implementation
    void setKeyPressCallback(KeyPressCallback callback) override;
    void enableInput(bool enabled) override;

    // Console input handling
    void processConsoleInput();

private:
    bool isConnected_;
    bool inputEnabled_;
    KeyPressCallback keyPressCallback_;
    std::thread inputThread_;
    std::atomic<bool> shouldStop_;
    mutable std::mutex callbackMutex_;

    void inputThreadFunction();
    KeyCode charToKeyCode(char c) const;
    void printKeyboardHelp() const;
};

// Console-based card reader emulation
class ConsoleCardReader : public peripherals::ICardReader {
public:
    ConsoleCardReader();
    ~ConsoleCardReader() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // ICardReader implementation
    void setCardPresentedCallback(CardPresentedCallback callback) override;
    void enableReading(bool enabled) override;

    // Manual card simulation
    void simulateCardPresented(const UserId& userId);

private:
    bool isConnected_;
    bool readingEnabled_;
    CardPresentedCallback cardPresentedCallback_;
    mutable std::mutex callbackMutex_;
};

// Console-based pump emulation
class ConsolePump : public peripherals::IPump {
public:
    ConsolePump();
    ~ConsolePump() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IPump implementation
    void start() override;
    void stop() override;
    bool isRunning() const override;
    void setPumpStateCallback(PumpStateCallback callback) override;

private:
    bool isConnected_;
    std::atomic<bool> isRunning_;
    PumpStateCallback pumpStateCallback_;
    mutable std::mutex callbackMutex_;

    void notifyStateChange();
};

// Console-based flow meter emulation
class ConsoleFlowMeter : public peripherals::IFlowMeter {
public:
    ConsoleFlowMeter();
    ~ConsoleFlowMeter() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IFlowMeter implementation
    void startMeasurement() override;
    void stopMeasurement() override;
    void resetCounter() override;
    Volume getCurrentVolume() const override;
    Volume getTotalVolume() const override;
    void setFlowCallback(FlowCallback callback) override;

    // Simulation control
    void setFlowRate(Volume ratePerSecond) { flowRate_ = ratePerSecond; }
    void simulateFlow(Volume targetVolume);

private:
    bool isConnected_;
    std::atomic<bool> isMeasuring_;
    Volume currentVolume_;
    Volume totalVolume_;
    mutable std::mutex volumeMutex_;
    Volume flowRate_; // liters per second
    FlowCallback flowCallback_;
    std::thread simulationThread_;
    std::atomic<bool> shouldStop_;
    mutable std::mutex callbackMutex_;

    void simulationThreadFunction(Volume targetVolume);
    void notifyFlowUpdate();
};

// Console emulator that manages all peripheral emulations
class ConsoleEmulator {
public:
    ConsoleEmulator();
    ~ConsoleEmulator();

    // Get peripheral instances
    std::unique_ptr<peripherals::IDisplay> createDisplay();
    std::unique_ptr<peripherals::IKeyboard> createKeyboard();
    std::unique_ptr<peripherals::ICardReader> createCardReader();
    std::unique_ptr<peripherals::IPump> createPump();
    std::unique_ptr<peripherals::IFlowMeter> createFlowMeter();

    // Console interface
    void printWelcome() const;
    void printHelp() const;
    void processCommand(const std::string& command);

    // Test card simulation
    void simulateCard(const UserId& userId);

private:
    // Keep weak references to created peripherals for command processing
    ConsoleCardReader* cardReader_;
    ConsoleFlowMeter* flowMeter_;
    
    void printAvailableCommands() const;
};

} // namespace fuelflux
