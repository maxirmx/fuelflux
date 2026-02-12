// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#ifdef USE_CARES

#include <string>
#include <vector>

namespace fuelflux {

// c-ares DNS resolver for use with sim800 binding
// Resolves hostnames to IP addresses using c-ares library
class CaresResolver {
public:
    CaresResolver();
    ~CaresResolver();

    // Resolve hostname to IP address
    // Returns empty string on failure
    // Parameters:
    //   hostname - hostname to resolve (e.g., "example.com")
    //   interface - network interface to use for DNS resolution (e.g., "ppp0")
    //               empty string means use default interface
    std::string Resolve(const std::string& hostname, const std::string& interface = "");

private:
    // Get nameserver from interface (e.g., ppp0)
    // Returns empty string if not found
    std::string GetNameserverFromInterface(const std::string& interface);
};

} // namespace fuelflux

#endif // USE_CARES
