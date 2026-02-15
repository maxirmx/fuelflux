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
    std::string_view authority =
        (authorityEndPos == std::string_view::npos)
            ? urlToParse
            : urlToParse.substr(0, authorityEndPos);

    // Strip userinfo from authority
    const auto atPos = authority.rfind('@');
    if (atPos != std::string_view::npos) {
        authority.remove_prefix(atPos + 1);
    }

    // Now work with the authority view (which has userinfo stripped)
    if (!authority.empty() && authority.front() == '[') {
        const auto bracketEnd = authority.find(']');
        if (bracketEnd != std::string_view::npos) {
            return std::string(authority.substr(1, bracketEnd - 1));
        }
    }

    const auto portPos = authority.find(':');
    if (portPos != std::string_view::npos) {
        return std::string(authority.substr(0, portPos));
    }

    return std::string(authority);
}

} // namespace fuelflux
