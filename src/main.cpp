#include "controller.h"
#include "console_emulator.h"
#include "logger.h"
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <signal.h>
#include <chrono>
#include <cctype>
#include <sstream>
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

// Helper: trim leading/trailing spaces
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Dispatcher: Explicit mode switching with Tab key
void inputDispatcher(ConsoleEmulator& emulator, Controller& controller) {
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
    
    auto printPrompt = [&]() {
        if (currentMode == InputMode::Command) {
            std::cout << "\n[CMD] fuelflux> " << std::flush;
        } else {
            std::cout << "\n[KEY MODE] Press Tab to return to command mode\n" << std::flush;
        }
    };

    auto switchMode = [&](InputMode newMode) {
        if (newMode != currentMode) {
            currentMode = newMode;
            std::cout << "\n";
            if (currentMode == InputMode::Command) {
                std::cout << "=== Switched to COMMAND mode ===\n";
#ifndef _WIN32
                setRawMode(false);
#endif
            } else {
                std::cout << "=== Switched to KEY mode ===\n";
                std::cout << "Press individual keys (0-9, A, B, *, #)\n";
                std::cout << "Press Tab to return to command mode\n";
#ifndef _WIN32
                setRawMode(true);
#endif
            }
            printPrompt();
        }
    };

    // Start in command mode
    switchMode(InputMode::Command);

    while (g_running) {
        SystemState state = controller.getStateMachine().getCurrentState();

        if (currentMode == InputMode::Command) {
            // Command mode: accept commands with Enter to execute
#ifndef _WIN32
            setRawMode(false); // ensure canonical mode
#endif
            std::string line;
            if (!std::getline(std::cin, line)) {
                // EOF or error
                g_running = false;
                break;
            }
            line = trim(line);
            
            // Check for Tab key (though unlikely in line mode)
            if (!line.empty() && line[0] == '\t') {
                switchMode(InputMode::Key);
                continue;
            }
            
            if (line.empty()) {
                printPrompt();
                continue;
            }
            
            // Parse command
            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;
            
            if (cmd == "quit" || cmd == "exit") {
                g_running = false;
                break;
            } else if (cmd == "help") {
                emulator.printHelp();
            } else if (cmd == "key" || cmd == "keymode") {
                switchMode(InputMode::Key);
                continue;
            } else if (cmd == "card") {
                std::string userId;
                iss >> userId;
                if (!userId.empty()) {
                    emulator.processCommand(line);
                } else {
                    std::cout << "Usage: card <user_id>\n";
                }
            } else {
                std::cout << "Unknown command: " << cmd << "\n";
                std::cout << "Type 'help' for available commands\n";
                std::cout << "Type 'keymode' to switch to key mode\n";
            }
            printPrompt();
            
        } else {
            // Key mode: raw character input, NO Enter mapping
#ifndef _WIN32
            setRawMode(true);
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
                    // NO mapping of Enter to 'A' - user must press 'A' explicitly
                    // Ignore Enter/Return in key mode
                    if (c == '\r' || c == '\n') {
                        std::cout << "\n[Key mode: Press 'A' to confirm, not Enter]\n" << std::flush;
                        continue;
                    }
                    // Forward raw key directly to keyboard (bypass processKeyboardInput)
                    emulator.dispatchKey(c);
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
                    // NO mapping of Enter to 'A' - user must press 'A' explicitly
                    // Ignore Enter/Return in key mode
                    if (c == '\r' || c == '\n') {
                        std::cout << "\n[Key mode: Press 'A' to confirm, not Enter]\n" << std::flush;
                        continue;
                    }
                    // Forward raw key directly to keyboard (bypass processKeyboardInput)
                    emulator.dispatchKey(c);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
            // small sleep to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

#ifndef _WIN32
    if (haveTerm) {
        tcsetattr(STDIN_FILENO, TCSANOW, &origTerm);
    }
#endif
}

int main(int argc [[maybe_unused]], char* argv[] [[maybe_unused]] ) {
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
    std::string controllerId = "CTRL-001";
    if (const char* envId = std::getenv("FUELFLUX_CONTROLLER_ID")) {
        controllerId = envId;
    }
    
    try {
        // Create console emulator
        ConsoleEmulator emulator;
        emulator.printWelcome();
        
        // Create controller
        Controller controller(controllerId);
        
        // Create and setup peripherals
        controller.setDisplay(emulator.createDisplay());
        controller.setKeyboard(emulator.createKeyboard());
        controller.setCardReader(emulator.createCardReader());
        controller.setPump(emulator.createPump());
        controller.setFlowMeter(emulator.createFlowMeter());
        
        // Initialize controller
        if (!controller.initialize()) {
            LOG_ERROR("Failed to initialize controller");
            return 1;
        }
        
        LOG_INFO("Controller initialized successfully");
        std::cout << "[Main] Type 'help' for available commands" << std::endl;
        
        // Start input dispatcher thread (handles both command and key modes)
        std::thread inputThread(inputDispatcher, std::ref(emulator), std::ref(controller));
        
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
