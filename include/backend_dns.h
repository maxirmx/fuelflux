// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>

namespace fuelflux {
namespace dns {

// Resolve DNS using c-ares
// When TARGET_SIM800C is enabled, binds to ppp0 interface
// This function is exposed for testing purposes
// Parameters:
//   hostname - hostname to resolve (e.g., "example.com")
// Returns:
//   IP address string on success (e.g., "93.184.216.34")
//   Empty string on failure
std::string ResolveDnsViaPpp0(const std::string& hostname);

} // namespace dns
} // namespace fuelflux
