// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "console_emulator.h"
#include "logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#endif

namespace fuelflux {

// ConsoleDisplay implementation
ConsoleDisplay::ConsoleDisplay()
    : isConnected_(false)
    , backlightEnabled_(true)
{
}

ConsoleDisplay::~ConsoleDisplay() {
    shutdown();
}

bool ConsoleDisplay::initialize() {
    isConnected_ = true;
    clear();
    LOG_PERIPH_DEBUG("Console display initialized");
    return true;
}

void ConsoleDisplay::shutdown() {
    isConnected_ = false;
    LOG_PERIPH_DEBUG("Console display shutdown");
}

bool ConsoleDisplay::isConnected() const {
    return isConnected_;
}

void ConsoleDisplay::showMessage(const DisplayMessage& message) {
    std::lock_guard<std::mutex> lock(displayMutex_);
    currentMessage_ = message;
    printDisplay();
}

void ConsoleDisplay::clear() {
    std::lock_guard<std::mutex> lock(displayMutex_);
    currentMessage_ = DisplayMessage{};
    printDisplay();
}

void ConsoleDisplay::setBacklight(bool enabled) {
    backlightEnabled_ = enabled;
}

void ConsoleDisplay::printDisplay() const {
    const size_t displayWidth = 40;

    std::cout << "\n";
    printBorder(false);
    std::cout << "│" << padLine(currentMessage_.line1, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line2, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line3, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line4, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line5, displayWidth) << "│\n";
    printBorder(true);
    std::cout << std::flush;
}

void ConsoleDisplay::printBorder(bool bottom) const {
    const size_t displayWidth = 40;
#ifdef _WIN32
    // Use Unicode box drawing characters now that console is UTF-8 enabled
    if (!bottom) {
        std::cout << "┌";
        for (size_t i = 0; i < displayWidth; ++i) std::cout << "─";
        std::cout << "┐\n";
    } else {
        std::cout << "└";
        for (size_t i = 0; i < displayWidth; ++i) std::cout << "─";
        std::cout << "┘\n";
    }
#else
    // Use ASCII borders on non-Windows platforms
    std::cout << "+" << std::string(displayWidth, '-') << "+\n";
#endif
}

std::string ConsoleDisplay::padLine(const std::string& line, size_t width) const {
    if (line.length() >= width) {
        return line.substr(0, width);
    }
    
    size_t padding = width - line.length();
    size_t leftPad = padding / 2;
    size_t rightPad = padding - leftPad;
    
    return std::string(leftPad, ' ') + line + std::string(rightPad, ' ');
}

// ConsoleKeyboard implementation
ConsoleKeyboard::ConsoleKeyboard()
    : isConnected_(false)
    , inputEnabled_(false)
    , shouldStop_(false)
{
}

ConsoleKeyboard::~ConsoleKeyboard() {
    shutdown();
}

bool ConsoleKeyboard::initialize() {
    isConnected_ = true;
    shouldStop_ = false;
    // Do not start an internal thread; dispatcher will inject keys
    printKeyboardHelp();
    return true;
}

void ConsoleKeyboard::shutdown() {
    shouldStop_ = true;
    if (inputThread_.joinable()) {
        inputThread_.join();
    }
    isConnected_ = false;
}

bool ConsoleKeyboard::isConnected() const {
    return isConnected_;
}

void ConsoleKeyboard::setKeyPressCallback(KeyPressCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    keyPressCallback_ = callback;
}

void ConsoleKeyboard::enableInput(bool enabled) {
    inputEnabled_ = enabled;
}

void ConsoleKeyboard::injectKey(char c) {
    if (!inputEnabled_) return;
    KeyCode keyCode = charToKeyCode(c);
    if (keyCode == static_cast<KeyCode>(0)) return;
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (keyPressCallback_) keyPressCallback_(keyCode);
}

KeyCode ConsoleKeyboard::charToKeyCode(char c) const {
    switch (std::toupper(static_cast<unsigned char>(c))) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return static_cast<KeyCode>(c);
        case '*': return KeyCode::KeyMax;
        case '#': return KeyCode::KeyClear;
        case 'A': return KeyCode::KeyStart;
        case 'B': return KeyCode::KeyStop;
        default: return static_cast<KeyCode>(0);
    }
}

void ConsoleKeyboard::printKeyboardHelp() const {
    std::cout << "\n=== KEYBOARD MAPPINGS ===\n";
    std::cout << "0-9: Number keys\n";
    std::cout << "*  : Max (maximum volume)\n";
    std::cout << "#  : Clear (delete last digit)\n";
    std::cout << "A  : Start/Enter (confirm input)\n";
    std::cout << "B  : Stop/Cancel (cancel operation)\n";
    std::cout << "\n=== IMPORTANT ===\n";
    std::cout << "In KEY mode: You must press 'A' (not Enter)\n";
    std::cout << "Press Tab to switch between command/key modes\n";
    std::cout << "=========================\n\n";
}

// ConsoleCardReader implementation
ConsoleCardReader::ConsoleCardReader()
    : isConnected_(false)
    , readingEnabled_(false)
{
}

ConsoleCardReader::~ConsoleCardReader() {
    shutdown();
}

bool ConsoleCardReader::initialize() {
    isConnected_ = true;
    return true;
}

void ConsoleCardReader::shutdown() {
    isConnected_ = false;
}

bool ConsoleCardReader::isConnected() const {
    return isConnected_;
}

void ConsoleCardReader::setCardPresentedCallback(CardPresentedCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    cardPresentedCallback_ = callback;
}

void ConsoleCardReader::enableReading(bool enabled) {
    readingEnabled_ = enabled;
}

void ConsoleCardReader::simulateCardPresented(const UserId& userId) {
    if (!readingEnabled_) {
        std::cout << "[CardReader] Reading disabled, ignoring card: " << userId << std::endl;
        return;
    }
    
    std::cout << "[CardReader] Card presented: " << userId << std::endl;
    
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (cardPresentedCallback_) {
        cardPresentedCallback_(userId);
    }
}

// ConsolePump implementation
ConsolePump::ConsolePump()
    : isConnected_(false)
    , isRunning_(false)
{
}

ConsolePump::~ConsolePump() {
    shutdown();
}

bool ConsolePump::initialize() {
    isConnected_ = true;
    isRunning_ = false;
    return true;
}

void ConsolePump::shutdown() {
    stop();
    isConnected_ = false;
}

bool ConsolePump::isConnected() const {
    return isConnected_;
}

void ConsolePump::start() {
    if (!isConnected_) return;
    
    bool wasRunning = isRunning_.exchange(true);
    if (!wasRunning) {
        std::cout << "[Pump] Started" << std::endl;
        notifyStateChange();
    }
}

void ConsolePump::stop() {
    bool wasRunning = isRunning_.exchange(false);
    if (wasRunning) {
        std::cout << "[Pump] Stopped" << std::endl;
        notifyStateChange();
    }
}

bool ConsolePump::isRunning() const {
    return isRunning_;
}

void ConsolePump::setPumpStateCallback(PumpStateCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    pumpStateCallback_ = callback;
}

void ConsolePump::notifyStateChange() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (pumpStateCallback_) {
        pumpStateCallback_(isRunning_);
    }
}

// ConsoleFlowMeter implementation
ConsoleFlowMeter::ConsoleFlowMeter()
    : isConnected_(false)
    , isMeasuring_(false)
    , currentVolume_(0.0)
    , totalVolume_(0.0)
    , flowRate_(2.0) // 2 liters per second default
    , shouldStop_(false)
{
}

ConsoleFlowMeter::~ConsoleFlowMeter() {
    shutdown();
}

bool ConsoleFlowMeter::initialize() {
    isConnected_ = true;
    return true;
}

void ConsoleFlowMeter::shutdown() {
    stopMeasurement();
    shouldStop_ = true;
    if (simulationThread_.joinable()) {
        simulationThread_.join();
    }
    isConnected_ = false;
}

bool ConsoleFlowMeter::isConnected() const {
    return isConnected_;
}

void ConsoleFlowMeter::startMeasurement() {
    if (!isConnected_) return;
    
    isMeasuring_ = true;
    std::cout << "[FlowMeter] Started measurement" << std::endl;
}

void ConsoleFlowMeter::stopMeasurement() {
    isMeasuring_ = false;
    std::cout << "[FlowMeter] Stopped measurement" << std::endl;
}

void ConsoleFlowMeter::resetCounter() {
    {
        std::lock_guard<std::mutex> lock(volumeMutex_);
        currentVolume_ = 0.0;
    }
    std::cout << "[FlowMeter] Counter reset" << std::endl;
    notifyFlowUpdate();
}

Volume ConsoleFlowMeter::getCurrentVolume() const {
    std::lock_guard<std::mutex> lock(volumeMutex_);
    return currentVolume_;
}

Volume ConsoleFlowMeter::getTotalVolume() const {
    std::lock_guard<std::mutex> lock(volumeMutex_);
    return totalVolume_;
}

void ConsoleFlowMeter::setFlowCallback(FlowCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    flowCallback_ = callback;
}

void ConsoleFlowMeter::simulateFlow(Volume targetVolume) {
    if (simulationThread_.joinable()) {
        simulationThread_.join();
    }
    
    shouldStop_ = false;
    simulationThread_ = std::thread(&ConsoleFlowMeter::simulationThreadFunction, this, targetVolume);
}

void ConsoleFlowMeter::simulationThreadFunction(Volume targetVolume) {
    const auto updateInterval = std::chrono::milliseconds(100);
    const Volume volumePerUpdate = flowRate_ * 0.1; // 100ms updates
    
    while (!shouldStop_ && isMeasuring_) {
        {
            std::lock_guard<std::mutex> lock(volumeMutex_);
            if (currentVolume_ >= targetVolume) {
                break;
            }
        }
        std::this_thread::sleep_for(updateInterval);

        if (!isMeasuring_) break;

        Volume newVolume;
        {
            std::lock_guard<std::mutex> lock(volumeMutex_);
            newVolume = std::min(currentVolume_ + volumePerUpdate, targetVolume);
            currentVolume_ = newVolume;
            totalVolume_ += volumePerUpdate;
        }

        notifyFlowUpdate();
        
        std::cout << "[FlowMeter] Volume: " << std::fixed << std::setprecision(2) 
                  << newVolume << " L" << std::endl;
    }
    
    {
        std::lock_guard<std::mutex> lock(volumeMutex_);
        if (currentVolume_ >= targetVolume) {
            std::cout << "[FlowMeter] Target volume reached: " << targetVolume << " L" << std::endl;
        }
    }
}

void ConsoleFlowMeter::notifyFlowUpdate() {
    Volume current;
    Volume total;
    {
        std::lock_guard<std::mutex> lock(volumeMutex_);
        current = currentVolume_;
        total = totalVolume_;
    }
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (flowCallback_) {
        flowCallback_(current);
    }
}

// ConsoleEmulator implementation
ConsoleEmulator::ConsoleEmulator()
    : cardReader_(nullptr)
    , flowMeter_(nullptr)
    , keyboard_(nullptr)
    , commandBuffer_()
{
}

ConsoleEmulator::~ConsoleEmulator() = default;

std::unique_ptr<peripherals::IDisplay> ConsoleEmulator::createDisplay() {
    return std::make_unique<ConsoleDisplay>();
}

std::unique_ptr<peripherals::IKeyboard> ConsoleEmulator::createKeyboard() {
    auto kb = std::make_unique<ConsoleKeyboard>();
    keyboard_ = kb.get();
    return kb;
}

std::unique_ptr<peripherals::ICardReader> ConsoleEmulator::createCardReader() {
    auto reader = std::make_unique<ConsoleCardReader>();
    cardReader_ = reader.get();
    return reader;
}

std::unique_ptr<peripherals::IPump> ConsoleEmulator::createPump() {
    return std::make_unique<ConsolePump>();
}

std::unique_ptr<peripherals::IFlowMeter> ConsoleEmulator::createFlowMeter() {
    auto meter = std::make_unique<ConsoleFlowMeter>();
    flowMeter_ = meter.get();
    return meter;
}

void ConsoleEmulator::dispatchKey(char c) {
    if (keyboard_) {
        keyboard_->injectKey(c);
    }
}

bool ConsoleEmulator::processKeyboardInput(char c, SystemState state) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    if (state == SystemState::Waiting) {
        // command mode: build command buffer, accept backspace and enter
        if (c == '\r' || c == '\n') {
            std::string cmd = commandBuffer_;
            // handle command
            if (cmd == "quit" || cmd == "exit") {
                processCommand(cmd);
                commandBuffer_.clear();
                std::cout << std::flush;
                return true;
            } else if (cmd.rfind("card", 0) == 0) {
                processCommand(cmd);
            } else if (cmd == "help") {
                printHelp();
            } else {
                if (!cmd.empty()) std::cout << "Unknown command: " << cmd << std::endl;
            }
            commandBuffer_.clear();
            std::cout << std::flush;
            return false;
        } else if (c == 127 || c == 8) {
            if (!commandBuffer_.empty()) {
                commandBuffer_.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            return false;
        } else {
            commandBuffer_.push_back(c);
            std::cout << c << std::flush;
            return false;
        }
    } else {
        // key mode: forward raw key to keyboard
        if (keyboard_) keyboard_->injectKey(c);
        return false;
    }
}

void ConsoleEmulator::printWelcome() const {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    FUEL FLUX CONTROLLER                      ║\n";
    std::cout << "║                    Console Emulator                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    printHelp();
}

void ConsoleEmulator::printHelp() const {
    std::cout << "=== CONSOLE COMMANDS ===\n";
    std::cout << "card <user_id>  : Simulate card presentation\n";
    std::cout << "flow <volume>   : Simulate fuel flow\n";
    std::cout << "keymode        : Switch to key input mode\n";
    std::cout << "help           : Show this help\n";
    std::cout << "quit           : Exit application\n";
    std::cout << "\n=== MODE SWITCHING ===\n";
    std::cout << "Tab            : Switch between command/key modes\n";
    std::cout << "  Command mode : Type full commands, press Enter\n";
    std::cout << "  Key mode     : Press individual keys (A, B, 0-9, *, #)\n";
    std::cout << "\nTest Users:\n";
    std::cout << "  1111-1111-1111-1111 (Operator)\n";
    std::cout << "  2222-2222-2222-2222 (Customer)\n";
    std::cout << "  3333-3333-3333-3333 (Controller)\n";
    std::cout << "========================\n\n";
}

void ConsoleEmulator::processCommand(const std::string& command) {
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "card") {
        std::string userId;
        iss >> userId;
        if (!userId.empty()) {
            simulateCard(userId);
        } else {
            std::cout << "Usage: card <user_id>\n";
        }
    } else if (cmd == "flow") {
        std::string volumeStr;
        iss >> volumeStr;
        if (!volumeStr.empty()) {
            try {
                Volume volume = std::stod(volumeStr);
                if (flowMeter_) {
                    flowMeter_->simulateFlow(volume);
                }
            } catch (const std::exception&) {
                std::cout << "Invalid volume: " << volumeStr << std::endl;
            }
        } else {
            std::cout << "Usage: flow <volume>\n";
        }
    } else if (cmd == "help") {
        printHelp();
    } else if (cmd == "quit" || cmd == "exit") {
        // Handled by main loop
    } else if (!cmd.empty()) {
        std::cout << "Unknown command: " << cmd << std::endl;
        printAvailableCommands();
    }
}

void ConsoleEmulator::simulateCard(const UserId& userId) {
    if (cardReader_) {
        cardReader_->simulateCardPresented(userId);
    } else {
        std::cout << "Card reader not available\n";
    }
}

void ConsoleEmulator::printAvailableCommands() const {
    std::cout << "Available commands: card, flow, help, quit\n";
}

} // namespace fuelflux
