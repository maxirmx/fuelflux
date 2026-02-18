// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

#ifndef _WIN32
#include <termios.h>
#endif

#include "peripherals/peripheral_interface.h"

namespace fuelflux {

// Forward declaration - ConsoleDisplay is now defined in display/console_display.h
namespace display {
    class ConsoleDisplay;
}

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

    // Allow external dispatcher to inject a character as a key press
    void injectKey(char c);

private:
    bool isConnected_;
    bool inputEnabled_;
    KeyPressCallback keyPressCallback_;
    std::thread inputThread_;
    std::atomic<bool> shouldStop_;
    mutable std::mutex callbackMutex_;
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

    // Dispatcher helper: forward a raw character to the keyboard (if available)
    void dispatchKey(char c);

    // Process a character according to current system state: in Waiting state
    // collect command input (card/help/quit), in other states forward as key
    // Returns true if a exit command was issued
    bool processKeyboardInput(char c, SystemState state);
    void setInputMode(bool commandMode);
    void logLine(const std::string& message) const;
    void logBlock(const std::string& message) const;
    bool consumeModeSwitchRequest();

    // Start/stop input dispatcher loop (runs in internal thread)
    void startInputDispatcher(std::atomic<bool>& runningFlag);
    void stopInputDispatcher();

private:
    // Keep weak references to created peripherals for command processing
    ConsoleCardReader* cardReader_;
    ConsoleFlowMeter* flowMeter_;
    ConsoleKeyboard* keyboard_;

    // command assembly in command mode
    std::string commandBuffer_;
    mutable std::mutex commandMutex_;
    std::atomic<bool> requestKeyModeSwitch_{false};

    std::thread inputThread_;
    std::atomic<bool>* runningFlag_{nullptr};
    std::atomic<bool> shouldStop_{false};

#ifndef _WIN32
    void restoreTerminal(const struct ::termios& origTerm, bool haveTerm);
#endif
    void inputDispatcherLoop();
    
    void printAvailableCommands() const;
};

} // namespace fuelflux
