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
#include <cstring>
#include <atomic>
#include <mutex>

namespace fuelflux {

namespace {

// Global c-ares library initialization state
std::atomic<bool> g_cares_initialized{false};
std::once_flag g_cares_init_flag;
bool g_cares_init_success = false;

// RAII wrapper for ares_channel
class AresChannel {
public:
    AresChannel(const std::string& interface) : channel_(nullptr), initialized_(false) {
        // Check if library is initialized
        if (!g_cares_initialized.load()) {
            LOG_BCK_ERROR("c-ares library not initialized. Call InitializeCaresLibrary() first.");
            return;
        }
        
        struct ares_options options;
        std::memset(&options, 0, sizeof(options));
        options.timeout = 10000; // 10 seconds timeout for slow mobile networks
        options.tries = 3;       // 3 retries
        
        int status = ares_init_options(&channel_, &options, ARES_OPT_TIMEOUT | ARES_OPT_TRIES);
        if (status != ARES_SUCCESS) {
            LOG_BCK_ERROR("Failed to initialize c-ares channel: {}", ares_strerror(status));
            return;
        }
        
        // Set Yandex DNS servers
        std::string dnsServers = std::string(kYandexDns1) + "," + kYandexDns2;
        status = ares_set_servers_csv(channel_, dnsServers.c_str());
        if (status != ARES_SUCCESS) {
            LOG_BCK_ERROR("Failed to set Yandex DNS servers: {}", ares_strerror(status));
            ares_destroy(channel_);
            return;
        }
        
        // Bind DNS queries to specific interface (e.g., ppp0)
        if (!interface.empty()) {
            ares_set_local_dev(channel_, interface.c_str());
            LOG_BCK_DEBUG("c-ares bound to interface: {}", interface);
        }
        
        LOG_BCK_DEBUG("c-ares channel initialized with Yandex DNS: {}{}", 
                      dnsServers, 
                      interface.empty() ? "" : " on " + interface);
        initialized_ = true;
    }
    
    ~AresChannel() {
        if (initialized_ && channel_) {
            ares_destroy(channel_);
        }
    }
    
    // Non-copyable, non-movable
    AresChannel(const AresChannel&) = delete;
    AresChannel& operator=(const AresChannel&) = delete;
    
    ares_channel get() const { return channel_; }
    bool isInitialized() const { return initialized_; }
    
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

bool InitializeCaresLibrary() {
    // Use call_once to ensure initialization happens exactly once
    std::call_once(g_cares_init_flag, []() {
        int status = ares_library_init(ARES_LIB_INIT_ALL);
        if (status == ARES_SUCCESS) {
            g_cares_init_success = true;
            g_cares_initialized.store(true, std::memory_order_release);
            LOG_BCK_INFO("c-ares library initialized successfully");
        } else {
            g_cares_init_success = false;
            LOG_BCK_ERROR("Failed to initialize c-ares library: {}", ares_strerror(status));
        }
    });
    
    return g_cares_init_success;
}

void CleanupCaresLibrary() {
    // Only cleanup if initialization succeeded
    // NOTE: This should only be called at process shutdown, after all DNS resolvers
    // have been destroyed and are no longer in use.
    if (g_cares_initialized.load(std::memory_order_acquire)) {
        ares_library_cleanup();
        g_cares_initialized.store(false, std::memory_order_release);
        LOG_BCK_INFO("c-ares library cleaned up");
    }
}

CaresResolver::CaresResolver() {
}

CaresResolver::~CaresResolver() {
}

std::string CaresResolver::Resolve(const std::string& hostname, const std::string& interface) {
    // Serialize concurrent calls to prevent issues with channel operations
    std::lock_guard<std::mutex> lock(resolve_mutex_);
    
    // Check if library is initialized
    // NOTE: This check is for detecting programming errors (using resolver before init).
    // The library should only be cleaned up at process shutdown, after all resolvers
    // have stopped being used. If cleanup happens during resolution, it indicates
    // incorrect lifecycle management by the caller.
    if (!g_cares_initialized.load(std::memory_order_acquire)) {
        LOG_BCK_ERROR("c-ares library not initialized. Call InitializeCaresLibrary() first.");
        return "";
    }
    
    LOG_BCK_DEBUG("Resolving {} using c-ares with Yandex DNS{}", 
                  hostname,
                  interface.empty() ? "" : " via " + interface);
    
    // Check if hostname is already an IP address
    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, hostname.c_str(), &addr4) == 1 ||
        inet_pton(AF_INET6, hostname.c_str(), &addr6) == 1) {
        LOG_BCK_DEBUG("Hostname {} is already an IP address", hostname);
        return hostname;
    }
    
    AresChannel channel(interface);
    if (!channel.isInitialized()) {
        LOG_BCK_ERROR("Failed to initialize c-ares channel");
        return "";
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
        tv.tv_sec = 2;
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
    
    LOG_BCK_INFO("Resolved {} -> {} via Yandex DNS{}", 
                 hostname, ctx.result,
                 interface.empty() ? "" : " on " + interface);
    return ctx.result;
}

} // namespace fuelflux

#endif // USE_CARES
