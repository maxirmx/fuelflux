// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "console_emulator.h"
#include "peripherals/keyboard_utils.h"
#include "logger.h"
#include "version.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>
#include <array>
#include <fmt/format.h>
#include <deque>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#endif

namespace fuelflux {

namespace {
size_t utf8CharLength(unsigned char lead) {
    if ((lead & 0x80) == 0) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

std::string utf8Truncate(const std::string& text, size_t maxChars) {
    if (maxChars == 0) {
        return {};
    }

    size_t bytePos = 0;
    size_t chars = 0;
    while (bytePos < text.size() && chars < maxChars) {
        const auto len = utf8CharLength(static_cast<unsigned char>(text[bytePos]));
        if (bytePos + len > text.size()) {
            break; // incomplete code point at the end
        }
        bytePos += len;
        ++chars;
    }
    return text.substr(0, bytePos);
}

size_t utf8Length(const std::string& text) {
    size_t length = 0;
    for (size_t i = 0; i < text.size();) {
        const auto len = utf8CharLength(static_cast<unsigned char>(text[i]));
        if (i + len > text.size()) {
            break;
        }
        i += len;
        ++length;
    }
    return length;
}

class ConsoleUi {
public:
    static ConsoleUi& instance() {
        static ConsoleUi ui;
        return ui;
    }

    void initialize() {
        std::lock_guard<std::mutex> lock(outputMutex_);
        initializeLocked();
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(outputMutex_);
        if (!initialized_) {
            return;
        }
        if (useAlternateBuffer_) {
            std::cout << "\x1b[?1049l";
        }
        std::cout << std::flush;
        initialized_ = false;
    }

    void renderDisplay(const std::array<std::string, 6>& lines) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        initializeLocked();
        for (size_t i = 0; i < lines.size(); ++i) {
            writeAt(static_cast<int>(i + 1), 1, lines[i]);
        }
        renderLogAreaLocked();
        renderInputLocked();
        std::cout << std::flush;
    }

    void logLine(const std::string& line) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        initializeLocked();
        appendLogLineLocked(line);
        renderLogAreaLocked();
        renderInputLocked();
        std::cout << std::flush;
    }

    void logBlock(const std::string& block) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        initializeLocked();
        std::istringstream stream(block);
        std::string line;
        while (std::getline(stream, line)) {
            appendLogLineLocked(line);
        }
        renderLogAreaLocked();
        renderInputLocked();
        std::cout << std::flush;
    }

    void setInputMode(bool commandMode) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        initializeLocked();
        commandMode_ = commandMode;
        if (!commandMode_) {
            inputBuffer_.clear();
        }
        renderInputLocked();
        std::cout << std::flush;
    }

    void setInputBuffer(const std::string& buffer) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        initializeLocked();
        inputBuffer_ = buffer;
        renderInputLocked();
        std::cout << std::flush;
    }

private:
    static constexpr int kDisplayHeight = 6;
    static constexpr int kLogLines = 10;
    static constexpr int kLogStartRow = kDisplayHeight + 1;
    static constexpr int kInputRow = kLogStartRow + kLogLines + 1;
    static constexpr size_t kMaxLogLines = static_cast<size_t>(kLogLines);

    std::mutex outputMutex_;
    std::deque<std::string> logLines_;
    std::string inputBuffer_;
    bool initialized_ = false;
    bool commandMode_ = true;
    bool useAlternateBuffer_ = true;

    ConsoleUi() = default;

    void initializeLocked() {
        if (initialized_) {
            return;
        }
        if (useAlternateBuffer_) {
            std::cout << "\x1b[?1049h";
        }
        std::cout << "\x1b[2J\x1b[H";
        initialized_ = true;
        renderLogAreaLocked();
        renderInputLocked();
    }

    void appendLogLineLocked(const std::string& line) {
        if (line.empty()) {
            logLines_.push_back({});
        } else {
            logLines_.push_back(line);
        }
        while (logLines_.size() > kMaxLogLines) {
            logLines_.pop_front();
        }
    }

    void renderLogAreaLocked() {
        for (int i = 0; i < kLogLines; ++i) {
            const size_t index = logLines_.size() > static_cast<size_t>(kLogLines)
                ? logLines_.size() - kLogLines + static_cast<size_t>(i)
                : static_cast<size_t>(i);
            std::string line;
            if (index < logLines_.size()) {
                line = logLines_[index];
            }
            writeAt(kLogStartRow + i, 1, line);
        }
    }

    void renderInputLocked() {
        std::string prompt = commandMode_ ? "CMD> " : "KEY MODE (Tab to command)";
        std::string line = commandMode_ ? prompt + inputBuffer_ : prompt;
        writeAt(kInputRow, 1, line);
        moveCursorToInputLocked();
    }

    void moveCursorToInputLocked() {
        std::string prompt = commandMode_ ? "CMD> " : "KEY MODE (Tab to command)";
        const size_t cursorOffset = commandMode_
            ? utf8Length(prompt) + utf8Length(inputBuffer_)
            : utf8Length(prompt);
        std::cout << "\x1b[" << kInputRow << ";" << (cursorOffset + 1) << "H";
    }

    void writeAt(int row, int col, const std::string& text) {
        std::cout << "\x1b[" << row << ";" << col << "H\x1b[2K" << text;
    }
};

void logLine(const std::string& message) {
    ConsoleUi::instance().logLine(message);
}

void logBlock(const std::string& message) {
    ConsoleUi::instance().logBlock(message);
}
} // namespace

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
    ConsoleUi::instance().initialize();
    clear();
    LOG_PERIPH_DEBUG("Console display initialized");
    return true;
}

void ConsoleDisplay::shutdown() {
    isConnected_ = false;
    ConsoleUi::instance().shutdown();
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

    std::array<std::string, 6> lines = {
        buildBorder(false),
        fmt::format("│{}│", padLine(currentMessage_.line1, displayWidth)),
        fmt::format("│{}│", padLine(currentMessage_.line2, displayWidth)),
        fmt::format("│{}│", padLine(currentMessage_.line3, displayWidth)),
        fmt::format("│{}│", padLine(currentMessage_.line4, displayWidth)),
        buildBorder(true)
    };
    ConsoleUi::instance().renderDisplay(lines);
}

std::string ConsoleDisplay::buildBorder(bool bottom [[maybe_unused]] ) const {
    const size_t displayWidth = 40;
#ifdef _WIN32
    // Use Unicode box drawing characters now that console is UTF-8 enabled
    std::string line;
    line.reserve(displayWidth * 3);
    for (size_t i = 0; i < displayWidth; ++i) {
        line += "─";
    }
    if (!bottom) {
        return fmt::format("┌{}┐", line);
    }
    return fmt::format("└{}┘", line);
#else
    // Use ASCII borders on non-Windows platforms
    return fmt::format("+{}+", std::string(displayWidth, '-'));
#endif
}

std::string ConsoleDisplay::padLine(const std::string& line, size_t width) const {
    if (width == 0) {
        return {};
    }

    const std::string trimmed = utf8Truncate(line, width);
    const size_t charCount = utf8Length(trimmed);
    if (charCount >= width) {
        return trimmed;
    }

    const size_t padding = width - charCount;
    const size_t leftPad = padding / 2;
    const size_t rightPad = padding - leftPad;
    return fmt::format("{0}{1}{2}", std::string(leftPad, ' '), trimmed, std::string(rightPad, ' '));
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

void ConsoleKeyboard::printKeyboardHelp() const {
    logBlock(
        "=== KEYBOARD MAPPINGS ===\n"
        "0-9: Number keys\n"
        "*  : Max (maximum volume)\n"
        "#  : Clear (delete last digit)\n"
        "A  : Start/Enter (confirm input)\n"
        "B  : Stop/Cancel (cancel operation)\n"
        "\n=== IMPORTANT ===\n"
        "In KEY mode: You must press 'A' (not Enter)\n"
        "Press Tab to switch between command/key modes\n"
        "========================="
    );
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
        logLine(fmt::format("[CardReader] Reading disabled, ignoring card: {}", userId));
        return;
    }

    logLine(fmt::format("[CardReader] Card presented: {}", userId));
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
        logLine("[Pump] Started");
        notifyStateChange();
    }
}

void ConsolePump::stop() {
    bool wasRunning = isRunning_.exchange(false);
    if (wasRunning) {
        logLine("[Pump] Stopped");
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
    , flowRate_(0.05) 
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

    // Use atomic compare_exchange to ensure only one thread prints the message
     bool expected = false;
    // if (isMeasuring_.compare_exchange_strong(expected, true)) {
        // logLine(fmt::format("[FlowMeter] Started measurement at {:.2f} L/s", flowRate_));
    // }
    isMeasuring_.compare_exchange_strong(expected, true);
}

void ConsoleFlowMeter::stopMeasurement() {
    bool wasMeasuring = isMeasuring_.exchange(false);
    if (wasMeasuring) {
        shouldStop_ = true;

        // Only join if we're not being called from the simulation thread itself
        if (simulationThread_.joinable()) {
            // Check if we're on the simulation thread
            if (std::this_thread::get_id() != simulationThread_.get_id()) {
                simulationThread_.join();
            } else {
                // If we're on the simulation thread, detach to avoid leaving a joinable thread
                simulationThread_.detach();
            }
        }

        shouldStop_ = false;
        // logLine("[FlowMeter] Stopped measurement");
    }
}

void ConsoleFlowMeter::resetCounter() {
    {
        std::lock_guard<std::mutex> lock(volumeMutex_);
        currentVolume_ = 0.0;
    }
    // logLine("[FlowMeter] Counter reset");
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
    // Stop any existing simulation thread first
    shouldStop_ = true;
    if (simulationThread_.joinable()) {
        simulationThread_.join();
    }

    // Reset the stop flag and start new simulation
    shouldStop_ = false;

    // Ensure measurement is active using atomic store
    isMeasuring_.store(true);

    // Create and start the simulation thread
    simulationThread_ = std::thread(&ConsoleFlowMeter::simulationThreadFunction, this, targetVolume);

    // logLine(fmt::format("[FlowMeter] Simulation thread started for target: {:.2f} L", targetVolume));
}

void ConsoleFlowMeter::simulationThreadFunction(Volume targetVolume) {
    const auto updateInterval = std::chrono::milliseconds(100);
    const Volume volumePerUpdate = flowRate_ * 0.1; // 100ms updates = 0.1 seconds

    //logLine(fmt::format("[FlowMeter] Simulation thread running - Target: {:.2f} L at {:.2f} L/s",
    //                    targetVolume, flowRate_));
    //logLine(fmt::format("[FlowMeter] Volume per update: {:.2f} L", volumePerUpdate));

    int updateCount = 0;

    while (!shouldStop_) {
        // Check if measurement is still active
        if (!isMeasuring_) {
            // logLine("[FlowMeter] Measurement stopped, exiting simulation thread");
            break;
        }

        Volume currentVol;
        {
            std::lock_guard<std::mutex> lock(volumeMutex_);
            currentVol = currentVolume_;
        }

        // Check if we've reached the target
        if (currentVol >= targetVolume) {
            // logLine(fmt::format("[FlowMeter] Target volume reached: {:.2f} L after {} updates",
            //                    currentVol, updateCount));
            break;
        }

        // Sleep before updating
        std::this_thread::sleep_for(updateInterval);

        // Check flags again after sleep
        if (!isMeasuring_ || shouldStop_) {
            // logLine(fmt::format("[FlowMeter] Stopping: isMeasuring={}, shouldStop={}",
            //                    isMeasuring_.load(), shouldStop_.load()));
            break;
        }
         
        // Update volume
        Volume newVolume;
        {
            std::lock_guard<std::mutex> lock(volumeMutex_);
            newVolume = std::min(currentVolume_ + volumePerUpdate, targetVolume);
            currentVolume_ = newVolume;
            totalVolume_ += volumePerUpdate;
        }

        // Notify callback - but don't call stopMeasurement from within callback!
        notifyFlowUpdate();
        
        updateCount++;
        
        // Print progress every update
        // logLine(fmt::format("[FlowMeter] Volume: {:.2f} L / {:.2f} L ({})",
        //                    newVolume, targetVolume, updateCount));
    }

    // logLine("[FlowMeter] Simulation thread exiting");
}

void ConsoleFlowMeter::notifyFlowUpdate() {
    Volume current;
    {
        std::lock_guard<std::mutex> lock(volumeMutex_);
        current = currentVolume_;
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
            if (cmd == "exit") {
                processCommand(cmd);
                commandBuffer_.clear();
                ConsoleUi::instance().setInputBuffer(commandBuffer_);
                return true;
            } else if (!cmd.empty()) {
                processCommand(cmd);
            }
            commandBuffer_.clear();
            ConsoleUi::instance().setInputBuffer(commandBuffer_);
            return false;
        } else if (c == 127 || c == 8) {
            if (!commandBuffer_.empty()) {
                // Remove the last full UTF-8 code point, not just the last byte
                std::size_t i = commandBuffer_.size();
                // Move back over any UTF-8 continuation bytes (10xxxxxx)
                while (i > 0) {
                    --i;
                    unsigned char byte = static_cast<unsigned char>(commandBuffer_[i]);
                    if ((byte & 0xC0) != 0x80) {
                        // Found the leading byte of the last UTF-8 character
                        break;
                    }
                }
                commandBuffer_.erase(i);
            }
            ConsoleUi::instance().setInputBuffer(commandBuffer_);
            return false;
        } else {
            commandBuffer_.push_back(c);
            ConsoleUi::instance().setInputBuffer(commandBuffer_);
            return false;
        }
    } else {
        // key mode: forward raw key to keyboard
        if (keyboard_) keyboard_->injectKey(c);
        return false;
    }
}

void ConsoleEmulator::printWelcome() const {
    // Box width is 64 characters (62 inside the borders)
    constexpr int kBoxInnerWidth = 62;
    
    // Calculate centered version text
    std::string versionText = "Version " + std::string(FUELFLUX_VERSION);
    int padding = (kBoxInnerWidth - static_cast<int>(versionText.length())) / 2;
    std::string paddedVersion = std::string(padding, ' ') + versionText + 
                                std::string(kBoxInnerWidth - padding - versionText.length(), ' ');
    
    std::string welcomeMsg = 
        "╔══════════════════════════════════════════════════════════════╗\n"
        "║                    FUEL FLUX CONTROLLER                      ║\n"
        "║                    Console Emulator                          ║\n"
        "║" + paddedVersion + "║\n"
        "╚══════════════════════════════════════════════════════════════╝\n";
    logBlock(welcomeMsg);
    printHelp();
}

void ConsoleEmulator::printHelp() const {
    std::string help = "=== CONSOLE COMMANDS ===\n";
#ifndef TARGET_REAL_CARD_READER
    help += "card <user_id>  : Simulate card presentation\n";
#endif
    help += "keymode         : Switch to key input mode\n";
    help += "help            : Show this help\n";
    help += "exit            : Exit application\n";
    help += "========================\n";
    help += "  Command mode  : Type full commands, press Enter\n";
    help += "  Key mode      : Press individual keys (A, B, 0-9, *, #)\n";
    help += "  Tab key       : Switch between command and key input modes\n";
    help += "========================\n";
    logBlock(help);
}

void ConsoleEmulator::processCommand(const std::string& command) {
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "key" || cmd == "keymode") {
        requestKeyModeSwitch_.store(true);
        return;
    }
    if (cmd == "card") {
        std::string userId;
        iss >> userId;
        if (!userId.empty()) {
            simulateCard(userId);
        } else {
            logBlock("Usage: card <user_id>\nExample: card 2222-2222-2222-2222");
        }
    } else if (cmd == "help") {
        printHelp();
    } else if (!cmd.empty()) {
        logLine(fmt::format("Unknown command: {}", cmd));
        printAvailableCommands();
    }
}

void ConsoleEmulator::simulateCard(const UserId& userId) {
    if (cardReader_) {
        cardReader_->simulateCardPresented(userId);
    } else {
        logLine("Card reader not available");
    }
}

void ConsoleEmulator::printAvailableCommands() const {
#ifndef TARGET_REAL_CARD_READER
    logLine("Available commands: card, flow, keymode, help, exit");
#else
    logLine("Available commands: flow, keymode, help, exit");
#endif
    logLine("Type 'help' for detailed information");
}

void ConsoleEmulator::setInputMode(bool commandMode) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    ConsoleUi::instance().setInputMode(commandMode);
    if (commandMode) {
        ConsoleUi::instance().setInputBuffer(commandBuffer_);
    } else {
        commandBuffer_.clear();
    }
}

void ConsoleEmulator::logLine(const std::string& message) const {
    ConsoleUi::instance().logLine(message);
}

void ConsoleEmulator::logBlock(const std::string& message) const {
    ConsoleUi::instance().logBlock(message);
}

bool ConsoleEmulator::consumeModeSwitchRequest() {
    return requestKeyModeSwitch_.exchange(false);
}

} // namespace fuelflux
