// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "config.h"
#include "controller.h"
#include "backlog_worker.h"
#include "backend.h"
#include "console_emulator.h"
#include "logger.h"
#include "message_storage.h"
#ifdef USE_CARES
#include "cares_resolver.h"
#endif
#include "version.h"
#include "peripherals/peripheral_interface.h"
#include "peripherals/display.h"
#ifdef TARGET_REAL_CARD_READER
#include "peripherals/card_reader.h"
#endif
#ifdef TARGET_REAL_PUMP
#include "peripherals/pump.h"
#endif
#ifdef TARGET_REAL_KEYBOARD
#include "peripherals/keyboard.h"
#endif
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <signal.h>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif

using namespace fuelflux;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};
std::atomic<bool> g_loggerReady{false};

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    if (g_loggerReady) {
        LOG_INFO("Received signal {}, shutting down...", signal);
    } else {
        std::cerr << "Received signal " << signal << ", shutting down..." << std::endl;
    }
    g_running = false;
}

// Display failure message on a display interface
static void displayFailureMessage(peripherals::IDisplay* display) {
    if (display) {
        try {
            DisplayMessage msg;
            msg.line1 = "";
            msg.line2 = "Отказ";
            msg.line3 = "";
            msg.line4 = "";
            display->showMessage(msg);
        } catch (...) {
            // Ignore display errors during failure state
        }
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Initialize logging system first (before any logging calls)
    bool loggerReady = Logger::initialize();
    g_loggerReady = loggerReady;
    if (!loggerReady) {
        std::cerr << "Failed to initialize logging system" << std::endl;
    }
    
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
    
    // Retry limit to prevent infinite restart on persistent errors
    const int MAX_RETRIES = 10;
    int retryCount = 0;
    auto lastRetryTime = std::chrono::steady_clock::now();
#ifdef USE_CARES
    bool caresInitialized = false;
#endif
    
    while (g_running) {
        // Display pointer for failure message - created once per iteration, reused on failure
        std::unique_ptr<peripherals::IDisplay> display;
        
        try {
            // Reset retry count if enough time has passed since last failure (1 hour)
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastRetry = std::chrono::duration_cast<std::chrono::hours>(now - lastRetryTime);
            if (timeSinceLastRetry.count() >= 1) {
                retryCount = 0;
            }
            
#ifdef USE_CARES
            // Initialize c-ares library if not already initialized
            if (!caresInitialized) {
                if (!InitializeCaresLibrary()) {
                    if (loggerReady) {
                        LOG_ERROR("Failed to initialize c-ares library, will retry");
                    } else {
                        std::cerr << "Failed to initialize c-ares library, will retry" << std::endl;
                    }
                    throw std::runtime_error("c-ares initialization failed");
                }
                caresInitialized = true;
            }
#endif
            
            LOG_INFO("Starting FuelFlux Controller v{}...", FUELFLUX_VERSION);
            // Create console emulator
            ConsoleEmulator emulator;
            
            // Create display early so it can be used for failure message if needed
#ifdef TARGET_REAL_DISPLAY
            display = std::make_unique<peripherals::Display>();
#else
            display = emulator.createDisplay();
#endif
            display->initialize();
            emulator.printWelcome();

            DisplayMessage msg;
            msg.line1 = "Подготовка";
            msg.line2 = "";
            msg.line3 = "Дисплей";
            msg.line4 = "FuelFlux вер. " + std::string(FUELFLUX_VERSION);
            display->showMessage(msg);

            
            // ----- Backlog -----
            msg.line3 = "Очередь";
            display->showMessage(msg);

            auto storage = std::make_shared<MessageStorage>(STORAGE_DB_PATH);
            auto backend = Controller::CreateDefaultBackend(storage);
            auto backlogBackend = Controller::CreateDefaultBackendShared(controllerId, nullptr);
            BacklogWorker backlogWorker(storage, backlogBackend, std::chrono::seconds(30));
            backlogWorker.Start();


            // ----- Контроллер -----
            msg.line3 = "Контроллер";
            display->showMessage(msg);

            Controller controller(controllerId, backend);
            controller.setDisplay(std::move(display));


            // ----- Клавиатура -----
            msg.line3 = "Клавиатура";
            controller.showMessage(msg);
#ifdef TARGET_REAL_KEYBOARD
            controller.setKeyboard(std::make_unique<peripherals::HardwareKeyboard>());
#else
            controller.setKeyboard(emulator.createKeyboard());
#endif

            // ----- Кардридер -----
            msg.line3 = "Кардридер";
            controller.showMessage(msg);

#ifdef TARGET_REAL_CARD_READER
            auto cardReader = std::make_unique<peripherals::HardwareCardReader>();
            controller.setCardReader(std::move(cardReader));
#else
            controller.setCardReader(emulator.createCardReader());
#endif

            // ----- Насос/счётчик -----
            msg.line3 = "Насос\\счётчик";
            controller.showMessage(msg);
#ifdef TARGET_REAL_PUMP
            std::string pumpChip = peripherals::pump_defaults::GPIO_CHIP;
            int pumpLine = peripherals::pump_defaults::RELAY_PIN;
            bool pumpActiveLow = peripherals::pump_defaults::ACTIVE_LOW;

            if (const char* env = std::getenv("FUELFLUX_PUMP_GPIO_CHIP")) {
                if (*env != '\0') {
                    pumpChip = env;
                } else {
                    LOG_WARN("FUELFLUX_PUMP_GPIO_CHIP is set but empty; using default {}", pumpChip);
                }
            }
            if (const char* env = std::getenv("FUELFLUX_PUMP_RELAY_PIN")) {
                if (*env != '\0') {
                    try {
                        int parsed = std::stoi(env);
                        if (parsed >= 0) {
                            pumpLine = parsed;
                        } else {
                            LOG_WARN("Ignoring FUELFLUX_PUMP_RELAY_PIN='{}': negative values are invalid; using default {}", env, pumpLine);
                        }
                    } catch (const std::exception& ex) {
                        LOG_WARN("Failed to parse FUELFLUX_PUMP_RELAY_PIN='{}': {}; using default {}", env, ex.what(), pumpLine);
                    }
                } else {
                    LOG_WARN("FUELFLUX_PUMP_RELAY_PIN is set but empty; using default {}", pumpLine);
                }
            }
            if (const char* env = std::getenv("FUELFLUX_PUMP_ACTIVE_LOW")) {
                if (*env != '\0') {
                    std::string value = env;
                    for (auto& ch : value) {
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    }
                    pumpActiveLow = (value == "1" || value == "true" || value == "yes" || value == "on");
                } else {
                    LOG_WARN("FUELFLUX_PUMP_ACTIVE_LOW is set but empty; using default {}", pumpActiveLow);
                }
            }

            LOG_INFO("Pump relay configuration:");
            LOG_INFO("  GPIO Chip: {}", pumpChip);
            LOG_INFO("  Relay Line: {}", pumpLine);
            LOG_INFO("  Active Low: {}", pumpActiveLow);

            controller.setPump(std::make_unique<peripherals::HardwarePump>(
                pumpChip, pumpLine, pumpActiveLow));
#else
            controller.setPump(emulator.createPump());
#endif
            controller.setFlowMeter(emulator.createFlowMeter());
            
            // Initialize controller
            msg.line1 = "Запуск";
            msg.line3 = "Контроллер";
            controller.showMessage(msg);

            bool initOk = controller.initialize();
            if (!initOk) {
                LOG_ERROR("Failed to initialize controller; entering error state; awaiting reinitialization");
                emulator.logLine("[Main] Controller failed to initialize");
            } else {
                LOG_INFO("Controller initialized successfully");
            }
            
            // Set cache manager and user cache for console commands
            if (controller.getCacheManager()) {
                emulator.setCacheManager(controller.getCacheManager());
            }
            if (controller.getUserCache()) {
                emulator.setUserCache(controller.getUserCache());
            }
            
            // Start input dispatcher thread (handles both command and key modes)
            emulator.startInputDispatcher(g_running);
            
            // Start controller main loop in a separate thread
            controller.updateDisplay();
            std::thread controllerThread([&controller]() {
                controller.run();
            });
            
            // Ensure threads are cleaned up even if exceptions occur
            try {
                // Main thread waits for shutdown signal
                while (g_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                LOG_INFO("Shutting down...");
                
                // Shutdown controller
                controller.shutdown();
                backlogWorker.Stop();
            } catch (...) {
                // Ensure controller is stopped even if exception occurs
                controller.shutdown();
                backlogWorker.Stop();
                throw; // Re-throw to be caught by outer handler
            }
            
            // Wait for threads to finish
            emulator.stopInputDispatcher();
            
            if (controllerThread.joinable()) {
                controllerThread.join();
            }
            
            LOG_INFO("Shutdown complete");
        } catch (const std::exception& e) {
            LOG_CRITICAL("Error: {}", e.what());
            retryCount++;
            lastRetryTime = std::chrono::steady_clock::now();
        } catch (...) {
            LOG_CRITICAL("Unknown error occurred");
            retryCount++;
            lastRetryTime = std::chrono::steady_clock::now();
        }

        if (g_running) {
            if (retryCount >= MAX_RETRIES) {
                LOG_CRITICAL("Maximum retry limit ({}) reached, entering permanent failure state", MAX_RETRIES);
                
                // Display failure message if display was created
                displayFailureMessage(display.get());
                
#ifdef USE_CARES
                if (caresInitialized) {
                    CleanupCaresLibrary();
                    caresInitialized = false;
                }
#endif
                
                if (loggerReady) {
                    Logger::shutdown();
                    g_loggerReady = false;
                    loggerReady = false;
                }
                
                // Sleep forever - embedded application should not exit
                while (true) {
                    std::this_thread::sleep_for(std::chrono::hours(24));
                }
            }
            LOG_WARN("Recovering after error (attempt {}/{}); restarting controller loop", retryCount, MAX_RETRIES);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
#ifdef USE_CARES
    if (caresInitialized) {
        CleanupCaresLibrary();
    }
#endif
    
    if (loggerReady) {
        Logger::shutdown();
    }
    return 0;
}
