// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>

namespace fuelflux {

// Extract hostname from URL, handling:
// - optional scheme
// - optional userinfo
// - IPv6 addresses in brackets
// - optional port and path
std::string ExtractHostFromUrl(const std::string& url);

} // namespace fuelflux
