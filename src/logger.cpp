// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "logger.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace fuelflux {

bool Logger::initialized_ = false;
std::string Logger::configPath_;

namespace {
    // Return full path to the running executable, or empty path on failure.
    static std::filesystem::path getExecutablePath() {
#ifdef _WIN32
        std::vector<char> buf(MAX_PATH);
        DWORD len = GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0 || len >= buf.size()) {
            // try with bigger buffer
            buf.resize(32768);
            len = GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (len == 0 || len >= buf.size()) return {};
        }
        return std::filesystem::path(std::string(buf.data(), len));
#else
        std::vector<char> buf(PATH_MAX);
        ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size());
        if (len <= 0 || static_cast<size_t>(len) >= buf.size()) {
            // fallback: try with bigger buffer
            buf.resize(32768);
            len = readlink("/proc/self/exe", buf.data(), buf.size());
            if (len <= 0 || static_cast<size_t>(len) >= buf.size()) return {};
        }
        return std::filesystem::path(std::string(buf.data(), static_cast<size_t>(len)));
#endif
    }
} // namespace

bool Logger::initialize(const std::string& configPath) {
    if (initialized_) {
        return true;
    }
    
    configPath_ = configPath;
    
    // Try to load from config file first
    if (loadConfig(configPath)) {
        initialized_ = true;
        spdlog::info("Logging system initialized from config file: {}", configPath);
        return true;
    }
    
    // Fallback to default configuration
    std::cout << "[Logger] Failed to load config from " << configPath 
              << ", using default configuration" << std::endl;
    return initializeDefault();
}

bool Logger::initializeDefault() {
    if (initialized_) {
        return true;
    }
    
    try {
        // Create logs directory if it doesn't exist
        std::filesystem::create_directories(LOG_DIR);
        
        // Initialize async logging
        spdlog::init_thread_pool(8192, 1);
        
        // Create sinks
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            LOG_DIR + "/fuelflux.log", 1024 * 1024 * 10, 5);
        file_sink->set_level(spdlog::level::debug);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v");
        
        auto error_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            LOG_DIR + "/fuelflux_error.log", 1024 * 1024 * 5, 3);
        error_sink->set_level(spdlog::level::warn);
        error_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v");
        
        std::vector<spdlog::sink_ptr> sinks{file_sink, error_sink};
        
        // Create loggers
        auto main_logger = std::make_shared<spdlog::async_logger>(
                "fuelflux", sinks.begin(), sinks.end(), 
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        main_logger->set_level(spdlog::level::debug);
        spdlog::register_logger(main_logger);
        
        auto sm_logger = std::make_shared<spdlog::async_logger>(
            "StateMachine", sinks.begin(), sinks.end(),
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        sm_logger->set_level(spdlog::level::info);
        spdlog::register_logger(sm_logger);
        
        auto ctrl_logger = std::make_shared<spdlog::async_logger>(
            "Controller", sinks.begin(), sinks.end(),
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        ctrl_logger->set_level(spdlog::level::info);
        spdlog::register_logger(ctrl_logger);
        
        auto periph_logger = std::make_shared<spdlog::async_logger>(
            "Peripherals", sinks.begin(), sinks.end(),
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        periph_logger->set_level(spdlog::level::debug);
        spdlog::register_logger(periph_logger);
        
        auto cloud_logger = std::make_shared<spdlog::async_logger>(
            "CloudService", sinks.begin(), sinks.end(),
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        cloud_logger->set_level(spdlog::level::info);
        spdlog::register_logger(cloud_logger);
        
        // Set default logger
        spdlog::set_default_logger(main_logger);
        spdlog::set_level(spdlog::level::debug);
        
        initialized_ = true;
        spdlog::info("Logging system initialized with default configuration");
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Logger] Failed to initialize logging system: " << e.what() << std::endl;
        return false;
    }
}

void Logger::shutdown() {
    if (initialized_) {
        spdlog::info("Shutting down logging system");
        spdlog::shutdown();
        initialized_ = false;
    }
}

std::shared_ptr<spdlog::logger> Logger::getLogger(const std::string& name) {
    auto logger = spdlog::get(name);
    if (!logger) {
        // Return default logger if specific logger not found
        logger = spdlog::default_logger();
        if (!logger) {
            // Create a simple console logger as last resort
            logger = spdlog::stdout_color_mt(name);
        }
    }
    return logger;
}

void Logger::setLevel(spdlog::level::level_enum level) {
    spdlog::set_level(level);
    
    // Also set level for all registered loggers
    spdlog::apply_all([level](std::shared_ptr<spdlog::logger> logger) {
        logger->set_level(level);
    });
}

bool Logger::isInitialized() {
    return initialized_;
}

bool Logger::loadConfig(const std::string& configPath) {
    try {
        std::filesystem::path cfgPath(configPath);
        std::ifstream configFile;
        std::filesystem::path usedPath;

        // Try opening exactly as given first
        configFile.open(cfgPath);
        if (configFile.is_open()) {
            usedPath = cfgPath;
        } else {
            // If not absolute, try relative to executable location
            std::filesystem::path exePath = getExecutablePath();
            if (!exePath.empty()) {
                std::filesystem::path exeDir = exePath.parent_path();
                std::filesystem::path candidate = exeDir / cfgPath;
                configFile.open(candidate);
                if (configFile.is_open()) {
                    usedPath = candidate;
                } else {
                    // Also try exeDir/config/<cfg>
                    candidate = exeDir / "config" / cfgPath;
                    configFile.open(candidate);
                    if (configFile.is_open()) {
                        usedPath = candidate;
                    }
                }
            }
        }

        if (!configFile.is_open()) {
            // Could not find the config file in any of the attempted locations
            return false;
        }

        nlohmann::json config;
        configFile >> config;

        // Create logs directory if it doesn't exist
        std::filesystem::create_directories("logs");

        // Parse async configuration
        bool asyncEnabled = config.value("async", nlohmann::json{}).value("enabled", true);
        if (asyncEnabled) {
            size_t queueSize = config["async"].value("queue_size", 8192);
            size_t threadCount = config["async"].value("thread_count", 1);
            spdlog::init_thread_pool(queueSize, threadCount);
        }

        // Create sinks
        std::vector<spdlog::sink_ptr> sinks;

        for (const auto& sinkConfig : config["sinks"]) {
            std::string type = sinkConfig["type"];
            std::string name = sinkConfig["name"];

            spdlog::sink_ptr sink;

            if (type == "stdout_color") {
                sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            } else if (type == "rotating_file") {
                std::string filename = sinkConfig["filename"];
                std::string maxSizeStr = sinkConfig.value("max_size", "10MB");
                int maxFiles = sinkConfig.value("max_files", 5);

                // Parse size string (e.g., "10MB" -> bytes)
                size_t maxSize = 1024 * 1024 * 10; // Default 10MB
                if (maxSizeStr.size() >= 2 && maxSizeStr.substr(maxSizeStr.size() - 2) == "MB") {
                    int sizeMB = std::stoi(maxSizeStr.substr(0, maxSizeStr.length() - 2));
                    maxSize = sizeMB * 1024 * 1024;
                } else if (maxSizeStr.size() >= 2 && maxSizeStr.substr(maxSizeStr.size() - 2) == "KB") {
                    int sizeKB = std::stoi(maxSizeStr.substr(0, maxSizeStr.length() - 2));
                    maxSize = sizeKB * 1024;
                }

                sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    filename, maxSize, maxFiles);
            }

            if (sink) {
                // Set sink properties
                std::string levelStr = sinkConfig.value("level", "info");
                sink->set_level(spdlog::level::from_str(levelStr));

                if (sinkConfig.contains("pattern")) {
                    sink->set_pattern(sinkConfig["pattern"]);
                }

                sinks.push_back(sink);
            }
        }

        // Create loggers
        for (const auto& loggerConfig : config["loggers"]) {
            std::string loggerName = loggerConfig["name"];
            std::string levelStr = loggerConfig.value("level", "info");

            std::shared_ptr<spdlog::logger> logger;

            if (asyncEnabled) {
                logger = std::make_shared<spdlog::async_logger>(
                    loggerName, sinks.begin(), sinks.end(),
                    spdlog::thread_pool(), spdlog::async_overflow_policy::block);
            } else {
                logger = std::make_shared<spdlog::logger>(
                    loggerName, sinks.begin(), sinks.end());
            }

            logger->set_level(spdlog::level::from_str(levelStr));
            spdlog::register_logger(logger);
        }

        // Set global level and default logger
        if (config.contains("global_level")) {
            spdlog::set_level(spdlog::level::from_str(config["global_level"]));
        }

        // Set main logger as default
        auto mainLogger = spdlog::get("fuelflux");
        if (mainLogger) {
            spdlog::set_default_logger(mainLogger);
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "[Logger] Error loading config: " << e.what() << std::endl;
        return false;
    }
}

} // namespace fuelflux