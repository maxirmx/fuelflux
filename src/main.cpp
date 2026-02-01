// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "config.h"
#include "controller.h"
#include "backlog_worker.h"
#include "backend_factory.h"
#include "console_emulator.h"
#include "logger.h"
#include "message_storage.h"
#ifdef TARGET_REAL_DISPLAY
#include "peripherals/display.h"
#endif
#ifdef TARGET_REAL_CARD_READER
#include "peripherals/card_reader.h"
#endif
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cerrno>
#include <signal.h>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

using namespace fuelflux;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    LOG_INFO("Received signal {}, shutting down...", signal);
    g_running = false;
}

// Dispatcher: Explicit mode switching with Tab key
static void inputDispatcher(ConsoleEmulator& emulator) {
#ifndef _WIN32
    struct termios origTerm{};
    bool haveTerm = (tcgetattr(STDIN_FILENO, &origTerm) == 0);

    auto setRawMode = [&](bool raw) {
        if (!haveTerm) return;
        struct termios t = origTerm;
        if (raw) {
            t.c_lflag &= ~(ICANON | ECHO);
            t.c_cc[VMIN] = 0;
            t.c_cc[VTIME] = 0;
        } else {
            // restore canonical with echo
            t.c_lflag |= (ICANON | ECHO);
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    };
#endif

    enum class InputMode {
        Command,
        Key
    };

    InputMode currentMode = InputMode::Command;

    auto switchMode = [&](InputMode newMode) {
        if (newMode != currentMode) {
            currentMode = newMode;
            if (currentMode == InputMode::Command) {
                emulator.logLine("=== Switched to COMMAND mode ===");
#ifndef _WIN32
                setRawMode(false);
#endif
                emulator.setInputMode(true);
            } else {
                emulator.logBlock(
                    "=== Switched to KEY mode ===\n"
                    "Press individual keys (0-9, A, B, *, #)\n"
                    "Press Tab to return to command mode"
                );
#ifndef _WIN32
                setRawMode(true);
#endif
                emulator.setInputMode(false);
            }
        }
    };

    // Start in command mode
    switchMode(InputMode::Command);

    while (g_running) {
#ifndef _WIN32
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        int ret = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char c = 0;
            ssize_t r = read(STDIN_FILENO, &c, 1);
            if (r > 0) {
                // Tab key switches to command mode
                if (c == '\t') {
                    switchMode(InputMode::Command);
                    continue;
                }
                if (currentMode == InputMode::Command) {
                    bool shouldQuit = emulator.processKeyboardInput(c, SystemState::Waiting);
                    if (shouldQuit) {
                        g_running = false;
                        break;
                    }
                    if (emulator.consumeModeSwitchRequest()) {
                        switchMode(InputMode::Key);
                    }
                } else {
                    // NO mapping of Enter to 'A' - user must press 'A' explicitly
                    // Ignore Enter/Return in key mode
                    if (c == '\r' || c == '\n') {
                        emulator.logLine("[Key mode: Press 'A' to confirm, not Enter]");
                        continue;
                    }
                    // Forward raw key directly to keyboard
                    emulator.dispatchKey(c);
                }
            } else if (r == 0) {
                // EOF on stdin - graceful shutdown
                LOG_INFO("EOF on stdin, shutting down...");
                g_running = false;
                break;
            } else {
                // Read error - log and shutdown
                int err = errno;  // Save errno before any other calls
                LOG_ERROR("Error reading from stdin (errno: {}), shutting down...", err);
                g_running = false;
                break;
            }
        }
#else
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 224) {
                // extended key: ignore second code
                (void)_getch();
            } else {
                char c = static_cast<char>(ch);
                // Tab key (ASCII 9) switches to command mode
                if (c == '\t') {
                    switchMode(InputMode::Command);
                    continue;
                }
                if (currentMode == InputMode::Command) {
                    bool shouldQuit = emulator.processKeyboardInput(c, SystemState::Waiting);
                    if (shouldQuit) {
                        g_running = false;
                        break;
                    }
                    if (emulator.consumeModeSwitchRequest()) {
                        switchMode(InputMode::Key);
                    }
                } else {
                    // NO mapping of Enter to 'A' - user must press 'A' explicitly
                    // Ignore Enter/Return in key mode
                    if (c == '\r' || c == '\n') {
                        emulator.logLine("[Key mode: Press 'A' to confirm, not Enter]");
                        continue;
                    }
                    // Forward raw key directly to keyboard
                    emulator.dispatchKey(c);
                }
            }
        }
#endif
        // small sleep to avoid busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#ifndef _WIN32
    if (haveTerm) {
        tcsetattr(STDIN_FILENO, TCSANOW, &origTerm);
    }
#endif
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Initialize logging system first (before any logging calls)
    if (!Logger::initialize()) {
        std::cerr << "Failed to initialize logging system" << std::endl;
        return 1;
    }
    
    LOG_INFO("Starting FuelFlux Controller...");
    
    // On Windows, set console to UTF-8 and enable virtual terminal processing
#ifdef _WIN32
    // Set console code page to UTF-8 for proper Unicode output
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // Enable VT processing to allow ANSI escape sequences
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
    
    // Get controller ID from environment or use default
    std::string controllerId = CONTROLLER_UID;
    if (const char* envId = std::getenv("FUELFLUX_CONTROLLER_ID")) {
        controllerId = envId;
    }
    
    try {
        // Create console emulator
        ConsoleEmulator emulator;
        emulator.printWelcome();
        
        auto storage = std::make_shared<MessageStorage>(STORAGE_DB_PATH);
        auto backend = CreateBackend(storage);
        auto backlogBackend = CreateBackendShared();
        BacklogWorker backlogWorker(storage, backlogBackend, std::chrono::seconds(30));
        backlogWorker.Start();

        // Create controller
        Controller controller(controllerId, std::move(backend));
        
        // Create and setup peripherals
#ifdef TARGET_REAL_DISPLAY
        // Use real hardware display with configuration from environment variables
        LOG_INFO("Using real hardware display (NHD-C12864A1Z-FSW-FBW-HTT)");

        // Get hardware configuration from environment or use defaults
        std::string spiDevice = "/dev/spidev1.0";
        std::string gpioChip = "/dev/gpiochip0";
        int dcPin = 262;
        int rstPin = 226;
        std::string fontPath = "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf";

        if (const char* env = std::getenv("FUELFLUX_SPI_DEVICE")) spiDevice = env;
        if (const char* env = std::getenv("FUELFLUX_GPIO_CHIP")) gpioChip = env;
        if (const char* env = std::getenv("FUELFLUX_DC_PIN")) dcPin = std::atoi(env);
        if (const char* env = std::getenv("FUELFLUX_RST_PIN")) rstPin = std::atoi(env);
        if (const char* env = std::getenv("FUELFLUX_FONT_PATH")) fontPath = env;

        LOG_INFO("Display hardware configuration:");
        LOG_INFO("  SPI Device: {}", spiDevice);
        LOG_INFO("  GPIO Chip: {}", gpioChip);
        LOG_INFO("  D/C Pin: {}", dcPin);
        LOG_INFO("  RST Pin: {}", rstPin);
        LOG_INFO("  Font Path: {}", fontPath);

        auto display = std::make_unique<peripherals::RealDisplay>(
            spiDevice, gpioChip, dcPin, rstPin, fontPath
        );
        controller.setDisplay(std::move(display));
#else
        controller.setDisplay(emulator.createDisplay());
#endif

        controller.setKeyboard(emulator.createKeyboard());

#ifdef TARGET_REAL_CARD_READER
        LOG_INFO("Using real NFC card reader (libnfc)");
        auto cardReader = std::make_unique<peripherals::HardwareCardReader>();
        controller.setCardReader(std::move(cardReader));
#else
        controller.setCardReader(emulator.createCardReader());
#endif

        controller.setPump(emulator.createPump());
        controller.setFlowMeter(emulator.createFlowMeter());
        
        // Initialize controller
        if (!controller.initialize()) {
            LOG_ERROR("Failed to initialize controller");
            return 1;
        }
        
        LOG_INFO("Controller initialized successfully");
        emulator.logLine("[Main] Type 'help' for available commands");
        
        // Start input dispatcher thread (handles both command and key modes)
        std::thread inputThread(inputDispatcher, std::ref(emulator));
        
        // Start controller main loop in a separate thread
        std::thread controllerThread([&controller]() {
            controller.run();
        });
        
        // Main thread waits for shutdown signal
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO("Shutting down...");
        
        // Shutdown controller
        controller.shutdown();

        backlogWorker.Stop();
        
        // Wait for threads to finish
        if (inputThread.joinable()) {
            inputThread.join();
        }
        
        if (controllerThread.joinable()) {
            controllerThread.join();
        }
        
        LOG_INFO("Shutdown complete");
        
        // Shutdown logging system last
        Logger::shutdown();
        
    } catch (const std::exception& e) {
        LOG_CRITICAL("Error: {}", e.what());
        Logger::shutdown();
        return 1;
    } catch (...) {
        LOG_CRITICAL("Unknown error occurred");
        Logger::shutdown();
        return 1;
    }
    
    return 0;
}
