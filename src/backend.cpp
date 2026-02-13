// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "url_utils.h"
#include "backend_utils.h"
#include "logger.h"
#include "version.h"
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
#ifdef USE_CARES
#include "cares_resolver.h"
#endif

namespace fuelflux {

namespace {

// User-Agent string with version
const std::string kUserAgent = "fuelflux/" FUELFLUX_VERSION;

// Ensure curl_global_init is called exactly once at process startup
std::once_flag curl_init_flag;

void InitCurlGlobally() {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        throw std::runtime_error("Failed to initialize libcurl globally: " + 
                                 std::string(curl_easy_strerror(res)));
    }
}

// RAII wrapper for CURL handle
class CurlHandle {
public:
    CurlHandle() : curl_(curl_easy_init()) {}
    ~CurlHandle() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }
    
    // Non-copyable, non-movable
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    
    CURL* get() const { return curl_; }
    explicit operator bool() const { return curl_ != nullptr; }
    
private:
    CURL* curl_;
};

// RAII wrapper for curl_slist
class CurlSlist {
public:
    CurlSlist() : list_(nullptr) {}
    ~CurlSlist() {
        if (list_) {
            curl_slist_free_all(list_);
        }
    }
    
    // Non-copyable, non-movable
    CurlSlist(const CurlSlist&) = delete;
    CurlSlist& operator=(const CurlSlist&) = delete;
    
    void append(const char* str) {
        struct curl_slist* new_list = curl_slist_append(list_, str);
        if (!new_list) {
            throw std::bad_alloc();
        }
        list_ = new_list;
    }
    
    struct curl_slist* get() const { return list_; }
    
private:
    struct curl_slist* list_;
};

// Callback function to write received data into a string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

#ifdef TARGET_SIM800C
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


#ifdef USE_CARES
// Helper function to extract port from URL (returns 443 for https, 80 for http by default)
int ExtractPortFromUrl(const std::string& url) {
    // Check for explicit port
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return 80;
    }
    
    std::string afterScheme = url.substr(schemeEnd + 3);
    
    // Handle IPv6 addresses in brackets
    size_t hostEnd = 0;
    if (!afterScheme.empty() && afterScheme[0] == '[') {
        size_t bracketEnd = afterScheme.find(']');
        if (bracketEnd != std::string::npos) {
            hostEnd = bracketEnd + 1;
        }
    }
    
    // Find port separator after host
    size_t portPos = afterScheme.find(':', hostEnd);
    size_t pathPos = afterScheme.find('/', hostEnd);
    
    if (portPos != std::string::npos && (pathPos == std::string::npos || portPos < pathPos)) {
        // Extract port number
        std::string portStr;
        size_t portEnd = (pathPos != std::string::npos) ? pathPos : afterScheme.length();
        portStr = afterScheme.substr(portPos + 1, portEnd - portPos - 1);
        try {
            return std::stoi(portStr);
        } catch (...) {
            // Fall through to default
        }
    }
    
    // Default port based on scheme
    if (url.substr(0, 5) == "https") {
        return 443;
    }
    return 80;
}

// Reusable CaresResolver instance to avoid repeated initialization
CaresResolver& GetCaresResolver() {
    static CaresResolver resolver;
    return resolver;
}

// Helper function to perform DNS resolution and configure CURLOPT_RESOLVE
// Parameters:
//   curl - CURL handle to configure
//   resolveList - CurlSlist to append the resolve entry to
//   host - hostname to resolve
//   url - full URL (needed to extract port)
//   logPrefix - prefix for log messages (e.g., "" or "Async deauthorize: ")
void SetupDnsResolution(CURL* curl, CurlSlist& resolveList, 
                        const std::string& host, const std::string& url,
                        const std::string& logPrefix = "") {
    std::string resolvedIp = GetCaresResolver().Resolve(host, kPppInterface);
    if (resolvedIp.empty()) {
        // c-ares failed; fall back to letting libcurl do DNS, but force it to use
        // the PPP interface so that queries do not go via the default route.
        LOG_BCK_WARN("{}c-ares DNS resolution failed for {}, falling back to libcurl DNS via interface {}", 
                     logPrefix, host, kPppInterface);
        curl_easy_setopt(curl, CURLOPT_DNS_INTERFACE, kPppInterface);
    } else if (resolvedIp != host) {
        // Hostname was successfully resolved to a different IP
        int port = ExtractPortFromUrl(url);
        // Format: "hostname:port:address"
        std::string resolveEntry = host + ":" + std::to_string(port) + ":" + resolvedIp;
        resolveList.append(resolveEntry.c_str());
        curl_easy_setopt(curl, CURLOPT_RESOLVE, resolveList.get());
        LOG_BCK_DEBUG("{}Using CURLOPT_RESOLVE: {}", logPrefix, resolveEntry);
    }
    // else: resolvedIp == host means it's already an IP literal, no warning needed
}
#endif

#endif

} // namespace

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid, std::shared_ptr<MessageStorage> storage)
    : BackendBase(controllerUid, std::move(storage))
    , baseAPI_(baseAPI)
{
    // Initialize libcurl globally exactly once (thread-safe)
    std::call_once(curl_init_flag, InitCurlGlobally);
    LOG_BCK_INFO("Backend initialized (v{}) with base API: {} and controller UID: {}", FUELFLUX_VERSION, baseAPI_, controllerUid_);
}

Backend::~Backend() {
    // Do not call curl_global_cleanup() here - it's process-global and may affect other users
    // Cleanup should happen at process shutdown, not per Backend instance
}

nlohmann::json Backend::HttpRequestWrapper(const std::string& endpoint,
                                           const std::string& method,
                                           const nlohmann::json& requestBody,
                                           bool useBearerToken) {
    std::lock_guard<std::recursive_mutex> lock(requestMutex_);

    networkError_ = false;

    // Use RAII wrapper for CURL handle
    CurlHandle curl;
    if (!curl) {
        LOG_BCK_ERROR("Failed to initialize curl");
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    try {
        std::string url = baseAPI_ + endpoint;
        std::string responseBody;
        
        // Compute body string once and reuse for both logging and POST
        std::string bodyStr = requestBody.dump();
        LOG_BCK_DEBUG("Request: {} {} with body: {}", method, endpoint, bodyStr);

        // Set URL
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        
        // Set user agent
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, kUserAgent.c_str());
        
        // Set callback for response
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
        
        // Set timeouts
#ifdef TARGET_SIM800C
        // GPRS/2G connections via SIM800C have very high latency (1-3 seconds per round trip)
        // Increase timeouts significantly to accommodate slow mobile networks
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);  // 30 seconds for TCP handshake
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);          // 90 seconds total timeout
#else
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);   // 5 seconds
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 15L);          // 15 seconds total timeout
#endif

        // Enable TCP keepalive
        curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPALIVE, 1L);

#ifdef TARGET_SIM800C
        // Bind to PPP interface if available and not localhost
        std::string host = ExtractHostFromUrl(baseAPI_);
#ifdef USE_CARES
        // DNS resolution list for CURLOPT_RESOLVE (must stay in scope until curl_easy_perform)
        CurlSlist resolveList;
#endif
        if (ShouldBindToPppInterface(host)) {
            LOG_BCK_DEBUG("Binding to {} for host {}", kPppInterface, host);
            curl_easy_setopt(curl.get(), CURLOPT_INTERFACE, kPppInterface);
            
#ifdef USE_CARES
            // Use c-ares with Yandex DNS for hostname resolution
            // Bind DNS queries to ppp0 interface via ares_set_local_dev()
            // Then use CURLOPT_RESOLVE to provide the resolved IP to curl
            // This preserves the hostname in the URL for Host header and SNI
            SetupDnsResolution(curl.get(), resolveList, host, url);
#endif
        } else {
            if (IsLocalhost(host)) {
                LOG_BCK_DEBUG("Skipping {} binding for localhost: {}", kPppInterface, host);
            } else {
                LOG_BCK_DEBUG("Skipping {} binding for host {} (interface unavailable)", kPppInterface, host);
            }
        }
#endif

        // Prepare headers using RAII wrapper
        CurlSlist headers;
        headers.append("Accept: */*");
        headers.append("Content-Type: application/json");
        
        if (useBearerToken) {
            std::string token = GetToken();
            if (!token.empty()) {
                std::string authHeader = "Authorization: Bearer " + token;
                headers.append(authHeader.c_str());
            }
        }
        
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        // Set method and body
        if (method == "POST") {
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, bodyStr.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
        } else if (method == "GET") {
            curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
        } else {
            LOG_BCK_ERROR("Unsupported HTTP method: {}", method);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        // Perform request
        CURLcode res = curl_easy_perform(curl.get());

        if (res != CURLE_OK) {
            std::string errorMsg = "HTTP request failed: ";
            errorMsg += curl_easy_strerror(res);
            LOG_BCK_ERROR("{}", errorMsg);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        // Get HTTP status code
        long httpCode = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
        
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

// Overload that accepts explicit token for async deauthorization
nlohmann::json Backend::HttpRequestWrapper(const std::string& endpoint,
                                           const std::string& method,
                                           const nlohmann::json& requestBody,
                                           const std::string& bearerToken) {
    std::lock_guard<std::recursive_mutex> lock(requestMutex_);

    networkError_ = false;

    // Use RAII wrapper for CURL handle
    CurlHandle curl;
    if (!curl) {
        LOG_BCK_ERROR("Failed to initialize curl");
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    try {
        std::string url = baseAPI_ + endpoint;
        std::string responseBody;
        
        // Compute body string once and reuse for both logging and POST
        std::string bodyStr = requestBody.dump();
        LOG_BCK_DEBUG("Request: {} {} with body: {}", method, endpoint, bodyStr);

        // Set URL
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        
        // Set user agent
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, kUserAgent.c_str());
        
        // Set callback for response
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
        
        // Set timeouts
#ifdef TARGET_SIM800C
        // GPRS/2G connections via SIM800C have very high latency (1-3 seconds per round trip)
        // Increase timeouts significantly to accommodate slow mobile networks
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);  // 30 seconds for TCP handshake
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);          // 90 seconds total timeout
#else
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);   // 5 seconds
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 15L);          // 15 seconds total timeout
#endif

        // Enable TCP keepalive
        curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPALIVE, 1L);

#ifdef TARGET_SIM800C
        // Bind to PPP interface if available and not localhost
        std::string host = ExtractHostFromUrl(baseAPI_);
#ifdef USE_CARES
        // DNS resolution list for CURLOPT_RESOLVE (must stay in scope until curl_easy_perform)
        CurlSlist resolveList;
#endif
        if (ShouldBindToPppInterface(host)) {
            LOG_BCK_DEBUG("Binding to {} for host {}", kPppInterface, host);
            curl_easy_setopt(curl.get(), CURLOPT_INTERFACE, kPppInterface);
            
#ifdef USE_CARES
            // Use c-ares with Yandex DNS for hostname resolution
            // Bind DNS queries to ppp0 interface via ares_set_local_dev()
            // Then use CURLOPT_RESOLVE to provide the resolved IP to curl
            // This preserves the hostname in the URL for Host header and SNI
            SetupDnsResolution(curl.get(), resolveList, host, url);
#endif
        } else {
            if (IsLocalhost(host)) {
                LOG_BCK_DEBUG("Skipping {} binding for localhost: {}", kPppInterface, host);
            } else {
                LOG_BCK_DEBUG("Skipping {} binding for host {} (interface unavailable)", kPppInterface, host);
            }
        }
#endif

        // Prepare headers using RAII wrapper with explicit token
        CurlSlist headers;
        headers.append("Accept: */*");
        headers.append("Content-Type: application/json");
        
        if (!bearerToken.empty()) {
            std::string authHeader = "Authorization: Bearer " + bearerToken;
            headers.append(authHeader.c_str());
        }
        
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        // Set method and body
        if (method == "POST") {
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, bodyStr.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
        } else if (method == "GET") {
            curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
        } else {
            LOG_BCK_ERROR("Unsupported HTTP method: {}", method);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        // Perform request
        CURLcode res = curl_easy_perform(curl.get());

        if (res != CURLE_OK) {
            std::string errorMsg = "HTTP request failed: ";
            errorMsg += curl_easy_strerror(res);
            LOG_BCK_ERROR("{}", errorMsg);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        // Get HTTP status code
        long httpCode = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
        
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

// Static helper for async deauthorize - doesn't use mutex or modify state
void Backend::SendAsyncDeauthorize(const std::string& baseAPI, const std::string& token) {
    // Use RAII wrapper for CURL handle
    CurlHandle curl;
    if (!curl) {
        LOG_BCK_WARN("Async deauthorize: Failed to initialize curl");
        return;
    }

    try {
        std::string url = baseAPI + "/api/pump/deauthorize";
        std::string responseBody;
        std::string bodyStr = "{}";
        
        LOG_BCK_DEBUG("Async deauthorize: POST /api/pump/deauthorize");

        // Set URL
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        
        // Set user agent
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, kUserAgent.c_str());
        
        // Set callback for response
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseBody);
        
        // Set timeouts
#ifdef TARGET_SIM800C
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);
#else
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 15L);
#endif

        // Enable TCP keepalive
        curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPALIVE, 1L);

#ifdef TARGET_SIM800C
        // Bind to PPP interface if available and not localhost
        std::string host = ExtractHostFromUrl(baseAPI);
#ifdef USE_CARES
        // DNS resolution list for CURLOPT_RESOLVE (must stay in scope until curl_easy_perform)
        CurlSlist resolveList;
#endif
        if (ShouldBindToPppInterface(host)) {
            LOG_BCK_DEBUG("Async deauthorize: Binding to {} for host {}", kPppInterface, host);
            curl_easy_setopt(curl.get(), CURLOPT_INTERFACE, kPppInterface);
            
#ifdef USE_CARES
            // Use c-ares with Yandex DNS for hostname resolution via ppp0
            SetupDnsResolution(curl.get(), resolveList, host, url, "Async deauthorize: ");
#endif
        }
#endif

        // Prepare headers with token
        CurlSlist headers;
        headers.append("Accept: */*");
        headers.append("Content-Type: application/json");
        
        if (!token.empty()) {
            std::string authHeader = "Authorization: Bearer " + token;
            headers.append(authHeader.c_str());
        }
        
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        // Set POST method and body
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, bodyStr.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));

        // Perform request
        CURLcode res = curl_easy_perform(curl.get());

        if (res != CURLE_OK) {
            LOG_BCK_WARN("Async deauthorize request failed (ignored): {}", curl_easy_strerror(res));
        } else {
            long httpCode = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
            LOG_BCK_DEBUG("Async deauthorize response status: {}", httpCode);
            if (httpCode >= 200 && httpCode < 300) {
                LOG_BCK_INFO("Async deauthorization completed successfully");
            } else {
                LOG_BCK_WARN("Async deauthorize returned HTTP {}", httpCode);
            }
        }
    }
    catch (const std::exception& e) {
        LOG_BCK_WARN("Async deauthorize exception (ignored): {}", e.what());
    }
}

// Send async deauthorize request without mutex
void Backend::SendAsyncDeauthorizeRequest(const std::string& token) {
    SendAsyncDeauthorize(baseAPI_, token);
}

} // namespace fuelflux
