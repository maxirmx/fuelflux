#include "console_emulator.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

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
    return true;
}

void ConsoleDisplay::shutdown() {
    isConnected_ = false;
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
    printBorder();
    std::cout << "│" << padLine(currentMessage_.line1, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line2, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line3, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line4, displayWidth) << "│\n";
    std::cout << "│" << padLine(currentMessage_.line5, displayWidth) << "│\n";
    printBorder();
    std::cout << std::flush;
}

void ConsoleDisplay::printBorder() const {
    std::cout << "┌────────────────────────────────────────┐\n";
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
    inputThread_ = std::thread(&ConsoleKeyboard::inputThreadFunction, this);
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

void ConsoleKeyboard::inputThreadFunction() {
    char input;
    while (!shouldStop_ && std::cin.get(input)) {
        if (!inputEnabled_) {
            continue;
        }
        
        KeyCode keyCode = charToKeyCode(input);
        if (keyCode != static_cast<KeyCode>(0)) {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (keyPressCallback_) {
                keyPressCallback_(keyCode);
            }
        }
    }
}

KeyCode ConsoleKeyboard::charToKeyCode(char c) const {
    switch (c) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '*': case '#': case 'A': case 'B': case 'C': case 'D':
        case 'a': case 'b': case 'c': case 'd':
            return static_cast<KeyCode>(std::toupper(c));
        default:
            return static_cast<KeyCode>(0);
    }
}

void ConsoleKeyboard::printKeyboardHelp() const {
    std::cout << "\n=== KEYBOARD HELP ===\n";
    std::cout << "0-9: Number keys\n";
    std::cout << "*  : Max (maximum volume/amount)\n";
    std::cout << "#  : Clear (clear last digit)\n";
    std::cout << "A  : Start/Enter\n";
    std::cout << "B  : Stop/Cancel\n";
    std::cout << "C  : Liters mode\n";
    std::cout << "D  : Rubles mode\n";
    std::cout << "====================\n\n";
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
    currentVolume_ = 0.0;
    std::cout << "[FlowMeter] Counter reset" << std::endl;
    notifyFlowUpdate();
}

Volume ConsoleFlowMeter::getCurrentVolume() const {
    return currentVolume_;
}

Volume ConsoleFlowMeter::getTotalVolume() const {
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
    
    while (!shouldStop_ && isMeasuring_ && currentVolume_ < targetVolume) {
        std::this_thread::sleep_for(updateInterval);
        
        if (!isMeasuring_) break;
        
        Volume newVolume = std::min(currentVolume_.load() + volumePerUpdate, targetVolume);
        currentVolume_ = newVolume;
        totalVolume_ = totalVolume_.load() + volumePerUpdate;
        
        notifyFlowUpdate();
        
        std::cout << "[FlowMeter] Volume: " << std::fixed << std::setprecision(2) 
                  << newVolume << " L" << std::endl;
    }
    
    if (currentVolume_ >= targetVolume) {
        std::cout << "[FlowMeter] Target volume reached: " << targetVolume << " L" << std::endl;
    }
}

void ConsoleFlowMeter::notifyFlowUpdate() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (flowCallback_) {
        flowCallback_(currentVolume_, totalVolume_);
    }
}

// ConsoleEmulator implementation
ConsoleEmulator::ConsoleEmulator()
    : cardReader_(nullptr)
    , flowMeter_(nullptr)
{
}

ConsoleEmulator::~ConsoleEmulator() = default;

std::unique_ptr<peripherals::IDisplay> ConsoleEmulator::createDisplay() {
    return std::make_unique<ConsoleDisplay>();
}

std::unique_ptr<peripherals::IKeyboard> ConsoleEmulator::createKeyboard() {
    return std::make_unique<ConsoleKeyboard>();
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

void ConsoleEmulator::printWelcome() const {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    FUEL FLUX CONTROLLER                     ║\n";
    std::cout << "║                    Console Emulator                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    printHelp();
}

void ConsoleEmulator::printHelp() const {
    std::cout << "=== CONSOLE COMMANDS ===\n";
    std::cout << "card <user_id>  : Simulate card presentation\n";
    std::cout << "flow <volume>   : Simulate fuel flow\n";
    std::cout << "help           : Show this help\n";
    std::cout << "quit           : Exit application\n";
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
            } catch (const std::exception& e) {
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
