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
#include <ares.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace fuelflux {

namespace {

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

std::string ExtractHostFromUrl(const std::string& url) {
    std::string urlToParse = url;
    size_t schemePos = urlToParse.find("://");
    if (schemePos != std::string::npos) {
        urlToParse = urlToParse.substr(schemePos + 3);
    }
    
    // Handle IPv6 addresses in brackets
    if (!urlToParse.empty() && urlToParse[0] == '[') {
        size_t bracketEnd = urlToParse.find(']');
        if (bracketEnd != std::string::npos) {
            return urlToParse.substr(1, bracketEnd - 1);
        }
    }
    
    // Find port or path separator
    size_t portPos = urlToParse.find(':');
    size_t pathPos = urlToParse.find('/');
    
    if (portPos != std::string::npos && (pathPos == std::string::npos || portPos < pathPos)) {
        return urlToParse.substr(0, portPos);
    }
    if (pathPos != std::string::npos) {
        return urlToParse.substr(0, pathPos);
    }
    return urlToParse;
}

// Callback for c-ares DNS resolution
static void AresHostCallback(void* arg, int status, int timeouts, struct hostent* host) {
    (void)timeouts;
    std::string* result = static_cast<std::string*>(arg);
    
    if (status == ARES_SUCCESS && host && host->h_addr_list[0]) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, host->h_addr_list[0], ip, sizeof(ip));
        *result = ip;
        LOG_BCK_INFO("DNS resolved to: {}", ip);
    } else {
        LOG_BCK_ERROR("DNS resolution failed: {}", ares_strerror(status));
    }
}

// Socket callback to bind all DNS sockets to ppp0
static int AresSocketCallback(ares_socket_t sock, int type, void* data) {
    (void)type;
    (void)data;
    if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, 
                  kPppInterface, strlen(kPppInterface)) < 0) {
        LOG_BCK_ERROR("Failed to bind DNS socket to {}: {}", 
                     kPppInterface, std::strerror(errno));
        return -1;
    }
    LOG_BCK_DEBUG("Bound DNS socket to {}", kPppInterface);
    return 0;
}

// Resolve DNS using c-ares bound to ppp0
std::string ResolveDnsViaPpp0(const std::string& hostname) {
    ares_channel channel;
    struct ares_options options;
    memset(&options, 0, sizeof(options));
    
    int optmask = 0;
    
    if (ares_init_options(&channel, &options, optmask) != ARES_SUCCESS) {
        LOG_BCK_ERROR("Failed to initialize c-ares");
        return "";
    }
    
    // Set DNS servers (Yandex: 77.88.8.8, 77.88.8.1)
    struct ares_addr_node dns_servers[2];
    memset(dns_servers, 0, sizeof(dns_servers));
    
    // Primary: 77.88.8.8
    dns_servers[0].family = AF_INET;
    inet_pton(AF_INET, "77.88.8.8", &dns_servers[0].addr.addr4);
    dns_servers[0].next = &dns_servers[1];
    
    // Secondary: 77.88.8.1
    dns_servers[1].family = AF_INET;
    inet_pton(AF_INET, "77.88.8.1", &dns_servers[1].addr.addr4);
    dns_servers[1].next = nullptr;
    
    if (ares_set_servers(channel, dns_servers) != ARES_SUCCESS) {
        LOG_BCK_ERROR("Failed to set DNS servers");
        ares_destroy(channel);
        return "";
    }
    
    // Set socket callback to bind to ppp0
    ares_set_socket_callback(channel, AresSocketCallback, nullptr);
    
    // Start async query
    std::string result;
    ares_gethostbyname(channel, hostname.c_str(), AF_INET, AresHostCallback, &result);
    
    // Wait for completion (with timeout)
    int timeout_ms = 10000; // 10 seconds for GPRS
    int elapsed_ms = 0;
    
    while (elapsed_ms < timeout_ms) {
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        
        int nfds = ares_fds(channel, &read_fds, &write_fds);
        if (nfds == 0) {
            break; // Query completed
        }
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        
        int ret = select(nfds, &read_fds, &write_fds, nullptr, &tv);
        if (ret > 0) {
            ares_process(channel, &read_fds, &write_fds);
        }
        
        elapsed_ms += 100;
    }
    
    ares_destroy(channel);
    
    if (result.empty()) {
        LOG_BCK_ERROR("DNS resolution timeout for {}", hostname);
    }
    
    return result;
}
#endif

} // namespace

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid, std::shared_ptr<MessageStorage> storage)
    : BackendBase(controllerUid, std::move(storage))
    , baseAPI_(baseAPI)
{
    // Initialize libcurl globally exactly once (thread-safe)
    std::call_once(curl_init_flag, InitCurlGlobally);
    LOG_BCK_INFO("Backend initialized with base API: {} and controller UID: {}", baseAPI_, controllerUid_);
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
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "fuelflux/0.1.0");
        
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
        if (ShouldBindToPppInterface(host)) {
            LOG_BCK_DEBUG("Binding to {} for host {}", kPppInterface, host);
            curl_easy_setopt(curl.get(), CURLOPT_INTERFACE, kPppInterface);
            
            // Resolve DNS through ppp0 using c-ares
            std::string resolved_ip = ResolveDnsViaPpp0(host);
            if (!resolved_ip.empty()) {
                // Extract port from URL
                std::string port = "443"; // default HTTPS
                if (baseAPI_.find("http://") == 0) {
                    port = "80";
                }
                size_t scheme_end = baseAPI_.find("://");
                if (scheme_end != std::string::npos) {
                    size_t port_pos = baseAPI_.find(":", scheme_end + 3);
                    size_t path_pos = baseAPI_.find("/", scheme_end + 3);
                    
                    if (port_pos != std::string::npos && 
                        (path_pos == std::string::npos || port_pos < path_pos)) {
                        size_t port_end = path_pos != std::string::npos ? path_pos : baseAPI_.length();
                        port = baseAPI_.substr(port_pos + 1, port_end - port_pos - 1);
                    }
                }
                
                // Inject resolved IP into curl
                std::string resolve_entry = host + ":" + port + ":" + resolved_ip;
                CurlSlist resolve_list;
                resolve_list.append(resolve_entry.c_str());
                curl_easy_setopt(curl.get(), CURLOPT_RESOLVE, resolve_list.get());
                LOG_BCK_INFO("Using pre-resolved DNS: {}", resolve_entry);
            } else {
                LOG_BCK_WARN("DNS resolution via {} failed, falling back to system resolver", kPppInterface);
            }
        } else {
            if (host == "localhost" || host == "127.0.0.1" || host == "::1") {
                LOG_BCK_DEBUG("Skipping {} binding for localhost/local IP: {}", kPppInterface, host);
            } else {
                LOG_BCK_DEBUG("Skipping {} binding for host {} (interface unavailable)", kPppInterface, host);
            }
        }
#endif

        // Prepare headers using RAII wrapper
        CurlSlist headers;
        headers.append("Accept: */*");
        headers.append("Content-Type: application/json");
        
        if (useBearerToken && !token_.empty()) {
            std::string authHeader = "Authorization: Bearer " + token_;
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

} // namespace fuelflux
