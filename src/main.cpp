#include "controller.h"
#include "console_emulator.h"
#include "cloud_service.h"
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace fuelflux;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

// Command processing thread
void commandProcessingThread(ConsoleEmulator& emulator) {
    std::string command;
    
    while (g_running) {
        std::cout << "\nfuelflux> ";
        if (!std::getline(std::cin, command)) {
            // EOF or input error
            g_running = false;
            break;
        }
        
        if (command == "quit" || command == "exit") {
            g_running = false;
            break;
        }
        
        if (!command.empty()) {
            emulator.processCommand(command);
        }
    }
}

int main(int argc, char* argv[]) {
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "Starting FuelFlux Controller..." << std::endl;
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
        
        // Create and setup cloud service
        auto cloudService = std::make_unique<MockCloudService>();
        controller.setCloudService(std::move(cloudService));
        
        // Initialize controller
        if (!controller.initialize()) {
            std::cerr << "Failed to initialize controller" << std::endl;
            return 1;
        }
        
        std::cout << "\n[Main] Controller initialized successfully" << std::endl;
        std::cout << "[Main] Type 'help' for available commands" << std::endl;
        
        // Start command processing thread
        std::thread cmdThread(commandProcessingThread, std::ref(emulator));
        
        // Start controller main loop in a separate thread
        std::thread controllerThread([&controller]() {
            controller.run();
        });
        
        // Main thread waits for shutdown signal
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "\n[Main] Shutting down..." << std::endl;
        
        // Shutdown controller
        controller.shutdown();
        
        // Wait for threads to finish
        if (cmdThread.joinable()) {
            cmdThread.join();
        }
        
        if (controllerThread.joinable()) {
            controllerThread.join();
        }
        
        std::cout << "[Main] Shutdown complete" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    
    return 0;
}
