// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "url_utils.h"

#include <string_view>

namespace fuelflux {

std::string ExtractHostFromUrl(const std::string& url) {
    std::string_view urlToParse(url);

    const auto schemePos = urlToParse.find("://");
    if (schemePos != std::string_view::npos) {
        urlToParse.remove_prefix(schemePos + 3);
    }

    // Limit userinfo parsing to the authority section (before the first '/')
    const auto authorityEndPos = urlToParse.find('/');
    const std::string_view authority =
        (authorityEndPos == std::string_view::npos)
            ? urlToParse
            : urlToParse.substr(0, authorityEndPos);

    const auto atPos = authority.rfind('@');
    if (atPos != std::string_view::npos) {
        urlToParse.remove_prefix(atPos + 1);
    }

    if (!urlToParse.empty() && urlToParse.front() == '[') {
        const auto bracketEnd = urlToParse.find(']');
        if (bracketEnd != std::string_view::npos) {
            return std::string(urlToParse.substr(1, bracketEnd - 1));
        }
    }

    const auto portPos = urlToParse.find(':');
    const auto pathPos = urlToParse.find('/');

    if (portPos != std::string_view::npos &&
        (pathPos == std::string_view::npos || portPos < pathPos)) {
        return std::string(urlToParse.substr(0, portPos));
    }

    if (pathPos != std::string_view::npos) {
        return std::string(urlToParse.substr(0, pathPos));
    }

    return std::string(urlToParse);
}

} // namespace fuelflux
