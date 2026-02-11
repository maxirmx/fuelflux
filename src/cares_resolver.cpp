// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#ifdef USE_CARES

#include "cares_resolver.h"
#include "logger.h"
#include <ares.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <memory>

namespace fuelflux {

namespace {

// RAII wrapper for ares_channel
class AresChannel {
public:
    AresChannel() : channel_(nullptr), initialized_(false) {
        int status = ares_library_init(ARES_LIB_INIT_ALL);
        if (status != ARES_SUCCESS) {
            LOG_BCK_ERROR("Failed to initialize c-ares library: {}", ares_strerror(status));
            return;
        }
        
        struct ares_options options;
        std::memset(&options, 0, sizeof(options));
        options.timeout = 5000; // 5 seconds timeout
        options.tries = 3;      // 3 retries
        
        status = ares_init_options(&channel_, &options, ARES_OPT_TIMEOUT | ARES_OPT_TRIES);
        if (status != ARES_SUCCESS) {
            LOG_BCK_ERROR("Failed to initialize c-ares channel: {}", ares_strerror(status));
            ares_library_cleanup();
            return;
        }
        
        initialized_ = true;
    }
    
    ~AresChannel() {
        if (initialized_ && channel_) {
            ares_destroy(channel_);
            ares_library_cleanup();
        }
    }
    
    // Non-copyable, non-movable
    AresChannel(const AresChannel&) = delete;
    AresChannel& operator=(const AresChannel&) = delete;
    
    ares_channel get() const { return channel_; }
    bool isInitialized() const { return initialized_; }
    
    // Set nameserver
    bool setNameserver(const std::string& nameserver) {
        if (!initialized_ || !channel_) {
            return false;
        }
        
        int status = ares_set_servers_csv(channel_, nameserver.c_str());
        if (status != ARES_SUCCESS) {
            LOG_BCK_ERROR("Failed to set nameserver {}: {}", nameserver, ares_strerror(status));
            return false;
        }
        
        LOG_BCK_DEBUG("Set c-ares nameserver to: {}", nameserver);
        return true;
    }
    
private:
    ares_channel channel_;
    bool initialized_;
};

// Callback context for DNS resolution
struct ResolveContext {
    std::string result;
    bool done = false;
    int status = ARES_SUCCESS;
};

// Callback for ares_gethostbyname
void HostCallback(void* arg, int status, int timeouts, struct hostent* host) {
    (void)timeouts; // Unused parameter
    
    ResolveContext* ctx = static_cast<ResolveContext*>(arg);
    ctx->done = true;
    ctx->status = status;
    
    if (status != ARES_SUCCESS) {
        LOG_BCK_ERROR("DNS resolution failed: {}", ares_strerror(status));
        return;
    }
    
    if (!host || !host->h_addr_list || !host->h_addr_list[0]) {
        LOG_BCK_ERROR("DNS resolution returned no addresses");
        ctx->status = ARES_ENODATA;
        return;
    }
    
    // Convert first address to string
    char ip_str[INET6_ADDRSTRLEN];
    const char* result = nullptr;
    
    if (host->h_addrtype == AF_INET) {
        result = inet_ntop(AF_INET, host->h_addr_list[0], ip_str, sizeof(ip_str));
    } else if (host->h_addrtype == AF_INET6) {
        result = inet_ntop(AF_INET6, host->h_addr_list[0], ip_str, sizeof(ip_str));
    }
    
    if (result) {
        ctx->result = ip_str;
        LOG_BCK_DEBUG("DNS resolved to: {}", ctx->result);
    } else {
        LOG_BCK_ERROR("Failed to convert address to string");
        ctx->status = ARES_EBADRESP;
    }
}

} // namespace

CaresResolver::CaresResolver() {
}

CaresResolver::~CaresResolver() {
}

std::string CaresResolver::GetNameserverFromInterface(const std::string& interface) {
    // Read resolv.conf for the interface
    // For ppp interfaces, the nameserver is typically in /etc/ppp/resolv.conf
    // or we can read /etc/resolv.conf
    
    std::ifstream resolvConf("/etc/resolv.conf");
    if (!resolvConf.is_open()) {
        LOG_BCK_ERROR("Failed to open /etc/resolv.conf");
        return "";
    }
    
    std::string line;
    std::vector<std::string> nameservers;
    
    while (std::getline(resolvConf, line)) {
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        
        if (keyword == "nameserver") {
            std::string ns;
            iss >> ns;
            if (!ns.empty()) {
                nameservers.push_back(ns);
            }
        }
    }
    
    if (nameservers.empty()) {
        LOG_BCK_WARN("No nameservers found in /etc/resolv.conf");
        return "";
    }
    
    // Use the first nameserver
    LOG_BCK_DEBUG("Found nameserver for {}: {}", interface.empty() ? "default" : interface, nameservers[0]);
    return nameservers[0];
}

std::string CaresResolver::Resolve(const std::string& hostname, const std::string& interface) {
    LOG_BCK_DEBUG("Resolving {} using c-ares{}", hostname, 
                  interface.empty() ? "" : " on interface " + interface);
    
    // Check if hostname is already an IP address
    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, hostname.c_str(), &addr4) == 1 ||
        inet_pton(AF_INET6, hostname.c_str(), &addr6) == 1) {
        LOG_BCK_DEBUG("Hostname {} is already an IP address", hostname);
        return hostname;
    }
    
    AresChannel channel;
    if (!channel.isInitialized()) {
        LOG_BCK_ERROR("Failed to initialize c-ares channel");
        return "";
    }
    
    // Set nameserver if interface is specified
    if (!interface.empty()) {
        std::string nameserver = GetNameserverFromInterface(interface);
        if (!nameserver.empty()) {
            if (!channel.setNameserver(nameserver)) {
                LOG_BCK_ERROR("Failed to set nameserver, falling back to default");
            }
        }
    }
    
    // Perform DNS resolution
    ResolveContext ctx;
    ares_gethostbyname(channel.get(), hostname.c_str(), AF_INET, HostCallback, &ctx);
    
    // Wait for resolution to complete
    int nfds;
    fd_set read_fds, write_fds;
    struct timeval tv;
    
    while (!ctx.done) {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        
        nfds = ares_fds(channel.get(), &read_fds, &write_fds);
        if (nfds == 0) {
            // No more file descriptors to wait on
            break;
        }
        
        // Set timeout
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        struct timeval* tvp = ares_timeout(channel.get(), &tv, &tv);
        
        int result = select(nfds, &read_fds, &write_fds, nullptr, tvp);
        if (result < 0) {
            LOG_BCK_ERROR("select() failed during DNS resolution");
            break;
        }
        
        ares_process(channel.get(), &read_fds, &write_fds);
    }
    
    if (!ctx.done) {
        LOG_BCK_ERROR("DNS resolution timed out for {}", hostname);
        return "";
    }
    
    if (ctx.status != ARES_SUCCESS) {
        LOG_BCK_ERROR("DNS resolution failed for {}: {}", hostname, ares_strerror(ctx.status));
        return "";
    }
    
    return ctx.result;
}

} // namespace fuelflux

#endif // USE_CARES
