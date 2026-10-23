// Copyright (C) 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "runtime_config.h"
#include "config.h"

#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace fuelflux {
namespace {
std::string Trim(const std::string& value) {
    const auto nonSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::find_if(value.begin(), value.end(), nonSpace);
    const auto last = std::find_if(value.rbegin(), value.rend(), nonSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::optional<std::string> UrlPart(CURLU* url, CURLUPart part) {
    char* text = nullptr;
    if (curl_url_get(url, part, &text, 0) != CURLUE_OK) return std::nullopt;
    std::unique_ptr<char, decltype(&curl_free)> owned(text, curl_free);
    return std::string(text);
}

std::optional<std::string> NormalizeUrl(const std::string& value) {
    const auto authorityStart = value.find("://");
    if (authorityStart == std::string::npos || value.find_first_of("?#\\") != std::string::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch <= 32 || ch == 127; })) {
        return std::nullopt;
    }
    const auto authority = value.substr(authorityStart + 3, value.find('/', authorityStart + 3) - (authorityStart + 3));
    if (authority.empty() || authority.back() == ':' || authority.find('@') != std::string::npos) return std::nullopt;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && (i + 2 >= value.size() ||
            !std::isxdigit(static_cast<unsigned char>(value[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(value[i + 2])))) return std::nullopt;
    }
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, value.c_str(), CURLU_DISALLOW_USER) != CURLUE_OK) {
        return std::nullopt;
    }
    const auto scheme = UrlPart(url.get(), CURLUPART_SCHEME);
    const auto host = UrlPart(url.get(), CURLUPART_HOST);
    const auto port = UrlPart(url.get(), CURLUPART_PORT);
    if (!scheme || (*scheme != "http" && *scheme != "https") || !host || host->empty() ||
        (port && *port == "0") || UrlPart(url.get(), CURLUPART_USER) ||
        UrlPart(url.get(), CURLUPART_PASSWORD) || UrlPart(url.get(), CURLUPART_OPTIONS)) return std::nullopt;
    auto normalized = UrlPart(url.get(), CURLUPART_URL);
    if (!normalized) return std::nullopt;
    while (!normalized->empty() && normalized->back() == '/') normalized->pop_back();
    return normalized;
}
} // namespace

void InitializeCurlNetworking() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK) throw std::runtime_error("Failed to initialize libcurl globally");
    });
}

RuntimeConfig LoadRuntimeConfig(EnvironmentReader read, ConfigurationWarning warn) {
    InitializeCurlNetworking();
    if (!read) read = [](const char* key) -> std::optional<std::string> {
        const auto value = std::getenv(key);
        return value ? std::optional<std::string>(value) : std::nullopt;
    };
    const auto id = Trim(read("FUELFLUX_CONTROLLER_ID").value_or(""));
    const auto url = Trim(read("FUELFLUX_BACKEND_URL").value_or(""));
    auto backendUrl = url.empty() ? std::optional<std::string>(BACKEND_API_URL) : NormalizeUrl(url);
    if (!backendUrl) {
        // Never include a rejected URL: it may contain credentials.
        if (warn) warn("Invalid FUELFLUX_BACKEND_URL; using compiled default " + BACKEND_API_URL);
        backendUrl = BACKEND_API_URL;
    }
    return {id.empty() ? CONTROLLER_UID : id, *backendUrl};
}
} // namespace fuelflux
