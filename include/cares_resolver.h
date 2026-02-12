// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#ifdef USE_CARES

#include <string>
#include <mutex>

namespace fuelflux {

// Yandex DNS servers for reliable resolution over mobile networks
constexpr const char* kYandexDns1 = "77.88.8.8";
constexpr const char* kYandexDns2 = "77.88.8.1";

// Initialize c-ares library system-wide (must be called before using CaresResolver)
// Returns true on success, false on failure
// Thread-safe: can be called multiple times; initialization happens only once unless it fails,
// in which case it can be retried on subsequent calls
bool InitializeCaresLibrary();

// Cleanup c-ares library (should be called at process shutdown)
// Thread-safe
void CleanupCaresLibrary();

// c-ares DNS resolver for use with SIM800C mobile connections
// Uses Yandex DNS servers for reliable resolution over PPP interface
// Thread-safe: concurrent calls to Resolve() are serialized via internal mutex
// NOTE: InitializeCaresLibrary() must be called successfully before using this class
class CaresResolver {
public:
    CaresResolver();
    ~CaresResolver();

    // Resolve hostname to IP address using Yandex DNS
    // Returns empty string on failure
    // Parameters:
    //   hostname - hostname to resolve (e.g., "example.com")
    //   interface - network interface to bind for DNS queries (e.g., "ppp0")
    //               empty string means use system default
    // Thread-safe: multiple threads can call this method concurrently
    std::string Resolve(const std::string& hostname, const std::string& interface = "");

private:
    // Mutex for thread-safe DNS resolution
    // Protects concurrent channel operations
    mutable std::mutex resolve_mutex_;
};

} // namespace fuelflux

#endif // USE_CARES
