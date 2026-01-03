// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace fuelflux {

/**
 * @brief Centralized logging manager for the FuelFlux application
 * 
 * This class provides a unified interface for logging throughout the application.
 * It supports multiple loggers with different configurations and automatically
 * sets up async logging for performance.
 */
class Logger {
public:
    /**
     * @brief Initialize the logging system from configuration file
     * @param configPath Path to the JSON configuration file
     * @return true if initialization was successful, false otherwise
     */
    static bool initialize(const std::string& configPath = "config/logging.json");
    
    /**
     * @brief Initialize the logging system with default configuration
     * @return true if initialization was successful, false otherwise
     */
    static bool initializeDefault();
    
    /**
     * @brief Shutdown the logging system and flush all pending logs
     */
    static void shutdown();
    
    /**
     * @brief Get a logger by name
     * @param name Logger name (e.g., "StateMachine", "Controller", etc.)
     * @return Shared pointer to the logger, or default logger if not found
     */
    static std::shared_ptr<spdlog::logger> getLogger(const std::string& name = "fuelflux");
    
    /**
     * @brief Set the global log level for all loggers
     * @param level The log level (debug, info, warn, error, critical)
     */
    static void setLevel(spdlog::level::level_enum level);
    
    /**
     * @brief Check if logging system is initialized
     * @return true if initialized, false otherwise
     */
    static bool isInitialized();

private:
    static bool initialized_;
    static std::string configPath_;
    
    /**
     * @brief Load configuration from JSON file
     * @param configPath Path to configuration file
     * @return true if loaded successfully, false otherwise
     */
    static bool loadConfig(const std::string& configPath);
   
};

// Convenience macros for different logger categories
#define LOG_STATE_MACHINE() fuelflux::Logger::getLogger("StateMachine")
#define LOG_CONTROLLER() fuelflux::Logger::getLogger("Controller") 
#define LOG_PERIPHERALS() fuelflux::Logger::getLogger("Peripherals")
#define LOG_BACKEND() fuelflux::Logger::getLogger("Backend")
#define LOG_MAIN() fuelflux::Logger::getLogger("fuelflux")

// Convenience logging macros with automatic logger selection
#define LOG_SM_DEBUG(...) LOG_STATE_MACHINE()->debug(__VA_ARGS__)
#define LOG_SM_INFO(...) LOG_STATE_MACHINE()->info(__VA_ARGS__)
#define LOG_SM_WARN(...) LOG_STATE_MACHINE()->warn(__VA_ARGS__)
#define LOG_SM_ERROR(...) LOG_STATE_MACHINE()->error(__VA_ARGS__)

#define LOG_CTRL_DEBUG(...) LOG_CONTROLLER()->debug(__VA_ARGS__)
#define LOG_CTRL_INFO(...) LOG_CONTROLLER()->info(__VA_ARGS__)
#define LOG_CTRL_WARN(...) LOG_CONTROLLER()->warn(__VA_ARGS__)
#define LOG_CTRL_ERROR(...) LOG_CONTROLLER()->error(__VA_ARGS__)

#define LOG_PERIPH_DEBUG(...) LOG_PERIPHERALS()->debug(__VA_ARGS__)
#define LOG_PERIPH_INFO(...) LOG_PERIPHERALS()->info(__VA_ARGS__)
#define LOG_PERIPH_WARN(...) LOG_PERIPHERALS()->warn(__VA_ARGS__)
#define LOG_PERIPH_ERROR(...) LOG_PERIPHERALS()->error(__VA_ARGS__)

#define LOG_BCK_DEBUG(...) LOG_BACKEND()->debug(__VA_ARGS__)
#define LOG_BCK_INFO(...) LOG_BACKEND()->info(__VA_ARGS__)
#define LOG_BCK_WARN(...) LOG_BACKEND()->warn(__VA_ARGS__)
#define LOG_BCK_ERROR(...) LOG_BACKEND()->error(__VA_ARGS__)

#define LOG_DEBUG(...) LOG_MAIN()->debug(__VA_ARGS__)
#define LOG_INFO(...) LOG_MAIN()->info(__VA_ARGS__)
#define LOG_WARN(...) LOG_MAIN()->warn(__VA_ARGS__)
#define LOG_ERROR(...) LOG_MAIN()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) LOG_MAIN()->critical(__VA_ARGS__)

} // namespace fuelflux