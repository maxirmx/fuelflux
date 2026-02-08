// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "backend_utils.h"
#include "logger.h"
#include <curl/curl.h>
#include <mutex>
#include <sstream>
#include <stdexcept>
#ifdef TARGET_SIM800C
#include <ifaddrs.h>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cerrno>
#endif

namespace fuelflux {

#ifdef TARGET_SIM800C
namespace {
constexpr const char kPppInterface[] = "ppp0";

bool IsPppInterfaceAvailable() {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) {
        LOG_BCK_ERROR("Failed to get network interfaces (errno: {}): {}", errno, std::strerror(errno));
        return false;
    }

    bool found = false;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name && std::strcmp(ifa->ifa_name, kPppInterface) == 0) {
            found = true;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return found;
}

bool IsLocalhost(const std::string& host) {
    // Check case-insensitive "localhost"
    std::string lower_host = host;
    std::transform(lower_host.begin(), lower_host.end(), lower_host.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower_host == "localhost") {
        return true;
    }

    // Check IPv4 loopback range (127.0.0.0/8)
    // Any address starting with "127." is in the loopback range
    if (host.length() >= 4 && host.substr(0, 4) == "127.") {
        return true;
    }

    // Check IPv6 loopback - compact form
    if (host == "::1") {
        return true;
    }

    // Check IPv6 loopback - full forms
    // Handle both "0:0:0:0:0:0:0:1" and "0000:0000:0000:0000:0000:0000:0000:0001"
    // or any variation with leading zeros
    // Skip if already handled by compact form or doesn't contain colons
    if (host.find(':') != std::string::npos && host != "::1") {
        // Split by colons and check if we have 8 segments where first 7 are zero and last is 1
        std::vector<std::string> segments;
        size_t start = 0;
        size_t end = host.find(':');
        
        while (end != std::string::npos) {
            segments.push_back(host.substr(start, end - start));
            start = end + 1;
            end = host.find(':', start);
        }
        segments.push_back(host.substr(start));
        
        // Must have exactly 8 segments for full IPv6 address
        if (segments.size() == 8) {
            bool is_loopback = true;
            for (size_t i = 0; i < 7; ++i) {
                // Empty segments or segments longer than 4 chars are invalid
                if (segments[i].empty() || segments[i].length() > 4) {
                    is_loopback = false;
                    break;
                }
                // Check if segment contains valid hex and is all zeros
                bool all_zeros = true;
                for (char c : segments[i]) {
                    if (!std::isxdigit(static_cast<unsigned char>(c))) {
                        is_loopback = false;
                        all_zeros = false;
                        break;
                    }
                    if (c != '0') {
                        all_zeros = false;
                    }
                }
                if (!is_loopback || !all_zeros) {
                    break;
                }
            }
            
            // Check last segment is 1
            if (is_loopback && !segments[7].empty() && segments[7].length() <= 4) {
                // Validate all characters are hex digits
                bool valid_hex = true;
                for (char c : segments[7]) {
                    if (!std::isxdigit(static_cast<unsigned char>(c))) {
                        valid_hex = false;
                        break;
                    }
                }
                
                if (valid_hex) {
                    try {
                        // All chars are hex, so stoul will parse the entire string
                        unsigned long last_val = std::stoul(segments[7], nullptr, 16);
                        if (last_val != 1) {
                            is_loopback = false;
                        }
                    } catch (...) {
                        is_loopback = false;
                    }
                } else {
                    is_loopback = false;
                }
            } else {
                is_loopback = false;
            }
            
            if (is_loopback) {
                return true;
            }
        }
    }

    return false;
}

bool ShouldBindToPppInterface(const std::string& host) {
    if (IsLocalhost(host)) {
        return false;
    }
    return IsPppInterfaceAvailable();
}
} // namespace
#endif

namespace {
// Callback for writing response data from libcurl
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
} // namespace

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid, std::shared_ptr<MessageStorage> storage)
    : BackendBase(controllerUid, std::move(storage))
    , baseAPI_(baseAPI)
{
    // libcurl handles SSL automatically if built with SSL support
    LOG_BCK_INFO("Backend initialized with base API: {} and controller UID: {}", baseAPI_, controllerUid_);
}

Backend::~Backend() = default;

nlohmann::json Backend::HttpRequestWrapper(const std::string& endpoint,
                                           const std::string& method,
                                           const nlohmann::json& requestBody,
                                           bool useBearerToken) {
    std::lock_guard<std::recursive_mutex> lock(requestMutex_);

    networkError_ = false;

    try {
        // Build full URL
        std::string url = baseAPI_ + endpoint;
        
        LOG_BCK_DEBUG("Request: {} {} with body: {}", method, url, requestBody.dump());

        // Initialize curl
        CURL* curl = curl_easy_init();
        if (!curl) {
            LOG_BCK_ERROR("Failed to initialize libcurl");
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        // Response data
        std::string responseBody;
        long httpCode = 0;

        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Set timeouts
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // Set write callback
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

        // Prepare headers
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: fuelflux/0.1.0");
        headers = curl_slist_append(headers, "Accept: */*");
        
        if (useBearerToken && !token_.empty()) {
            std::string authHeader = "Authorization: Bearer " + token_;
            headers = curl_slist_append(headers, authHeader.c_str());
        }

        // Prepare request body for POST
        std::string bodyStr;
        if (method == "POST") {
            bodyStr = requestBody.dump();
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, bodyStr.size());
        } else if (method == "GET") {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        } else {
            LOG_BCK_ERROR("Unsupported HTTP method: {}", method);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

#ifdef TARGET_SIM800C
        // Parse host from URL for interface binding check
        std::string host;
        size_t schemePos = url.find("://");
        if (schemePos != std::string::npos) {
            std::string afterScheme = url.substr(schemePos + 3);
            size_t portOrPathPos = afterScheme.find_first_of(":/");
            if (portOrPathPos != std::string::npos) {
                host = afterScheme.substr(0, portOrPathPos);
            } else {
                host = afterScheme;
            }
        }

        if (ShouldBindToPppInterface(host)) {
            LOG_BCK_DEBUG("Binding to {} for host {}", kPppInterface, host);
            curl_easy_setopt(curl, CURLOPT_INTERFACE, kPppInterface);
        } else {
            if (IsLocalhost(host)) {
                LOG_BCK_DEBUG("Skipping {} binding for localhost/local IP: {}", kPppInterface, host);
            } else {
                LOG_BCK_DEBUG("Skipping {} binding for host {} (interface unavailable)", kPppInterface, host);
            }
        }
#endif

        // Perform request
        CURLcode res = curl_easy_perform(curl);

        // Get HTTP response code
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // Check if request succeeded
        if (res != CURLE_OK) {
            std::string errorMsg = "HTTP request failed: ";
            errorMsg += curl_easy_strerror(res);
            LOG_BCK_ERROR("{}", errorMsg);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        LOG_BCK_DEBUG("Response status: {} body: {}", httpCode, responseBody);

        // Check HTTP status code
        if (httpCode >= 200 && httpCode < 300) {
            // Success response
            nlohmann::json responseJson;

            // Handle empty or "null" response
            if (responseBody.empty() || responseBody == "null") {
                responseJson = nlohmann::json(nullptr);
            }
            else {
                try {
                    responseJson = nlohmann::json::parse(responseBody);
                }
                catch (const std::exception& e) {
                    LOG_BCK_ERROR("Failed to parse response JSON: {}", e.what());
                    networkError_ = true;
                    return BuildWrapperErrorResponse();
                }
            }

            return responseJson;
        }
        else {
            // HTTP error response
            std::ostringstream oss;
            oss << "System error, code " << httpCode;
            LOG_BCK_ERROR("{}", oss.str());
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }
    }
    catch (const std::exception& e) {
        LOG_BCK_ERROR("HTTP request exception: {}", e.what());
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }
}

} // namespace fuelflux
