// Copyright (C) 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <functional>
#include <optional>
#include <string>

namespace fuelflux {

// A copied startup snapshot. Never re-read the environment during recovery/retries.
struct RuntimeConfig {
    const std::string controllerId;
    const std::string backendUrl;
};

using EnvironmentReader = std::function<std::optional<std::string>(const char*)>;
using ConfigurationWarning = std::function<void(const std::string&)>;

RuntimeConfig LoadRuntimeConfig(EnvironmentReader read = {}, ConfigurationWarning warn = {});

// Shared by URL validation and backend construction; safe before worker startup.
void InitializeCurlNetworking();

} // namespace fuelflux
