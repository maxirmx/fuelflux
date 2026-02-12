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
#include <condition_variable>
#include <chrono>
#include <cerrno>
#include <sys/select.h>
#include <sys/time.h>

namespace fuelflux {

namespace {

// Global c-ares library initialization state
enum class InitState {
    Uninitialized,
    Initializing,
    Initialized,
    Failed
};

std::atomic<InitState> g_cares_init_state{InitState::Uninitialized};
std::mutex g_cares_init_mutex;
std::condition_variable g_cares_init_cv;

// RAII wrapper for ares_channel
class AresChannel {
public:
    AresChannel(const std::string& interface) : channel_(nullptr), initialized_(false) {
        // Check if library is initialized
        if (g_cares_init_state.load(std::memory_order_acquire) != InitState::Initialized) {
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
            LOG_BCK_ERROR("Failed to set DNS servers '{}': {} (error code: {})", 
                          dnsServers, ares_strerror(status), status);
            ares_destroy(channel_);
            channel_ = nullptr;
            return;
        }
        
        LOG_BCK_DEBUG("c-ares channel initialized with Yandex DNS: {}", dnsServers);
        
        // Bind DNS queries to specific interface (e.g., ppp0)
        // Note: ares_set_local_dev() returns void; there is no error checking available
        if (!interface.empty()) {
            ares_set_local_dev(channel_, interface.c_str());
            LOG_BCK_DEBUG("c-ares bound to interface: {}", interface);
        }
        
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
    // Fast path: already initialized successfully
    InitState state = g_cares_init_state.load(std::memory_order_acquire);
    if (state == InitState::Initialized) {
        return true;
    }
    
    // Slow path: need to initialize (or retry after failure)
    std::unique_lock<std::mutex> lock(g_cares_init_mutex);
    
    // Double-check after acquiring lock
    state = g_cares_init_state.load(std::memory_order_acquire);
    if (state == InitState::Initialized) {
        return true;
    }
    
    // If another thread is currently initializing, wait for it to complete
    if (state == InitState::Initializing) {
        LOG_BCK_DEBUG("c-ares library initialization in progress, waiting...");
        g_cares_init_cv.wait(lock, []() {
            InitState s = g_cares_init_state.load(std::memory_order_acquire);
            return s != InitState::Initializing;
        });
        // Re-check state after waiting
        state = g_cares_init_state.load(std::memory_order_acquire);
        return state == InitState::Initialized;
    }
    
    // Mark as initializing
    g_cares_init_state.store(InitState::Initializing, std::memory_order_release);
    
    // Attempt initialization
    int status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status == ARES_SUCCESS) {
        g_cares_init_state.store(InitState::Initialized, std::memory_order_release);
        lock.unlock();
        g_cares_init_cv.notify_all();
        LOG_BCK_INFO("c-ares library initialized successfully");
        return true;
    } else {
        g_cares_init_state.store(InitState::Failed, std::memory_order_release);
        lock.unlock();
        g_cares_init_cv.notify_all();
        LOG_BCK_ERROR("Failed to initialize c-ares library: {} (error code: {})", 
                      ares_strerror(status), status);
        return false;
    }
}

void CleanupCaresLibrary() {
    // Use atomic exchange to ensure cleanup runs exactly once, even if called
    // concurrently from multiple threads.
    InitState expected = InitState::Initialized;
    if (g_cares_init_state.compare_exchange_strong(expected, InitState::Uninitialized,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
        ares_library_cleanup();
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
    if (g_cares_init_state.load(std::memory_order_acquire) != InitState::Initialized) {
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
    
    // Wait for resolution to complete with overall timeout protection
    // c-ares has its own retry logic (10s timeout * 3 retries = 30s max)
    // Add extra margin for processing delays
    constexpr int kMaxResolutionTimeSeconds = 45;
    auto startTime = std::chrono::steady_clock::now();
    
    int nfds;
    fd_set read_fds, write_fds;
    struct timeval tv, max_tv;
    int loopCount = 0;
    
    while (!ctx.done) {
        // Check overall timeout to prevent infinite hangs
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime);
        if (elapsed.count() >= kMaxResolutionTimeSeconds) {
            LOG_BCK_ERROR("DNS resolution overall timeout ({}s) exceeded for {}", 
                          kMaxResolutionTimeSeconds, hostname);
            break;
        }
        
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        
        nfds = ares_fds(channel.get(), &read_fds, &write_fds);
        if (nfds == 0) {
            // No more file descriptors to wait on - c-ares may have given up
            LOG_BCK_DEBUG("DNS resolution: no file descriptors (loop {})", loopCount);
            break;
        }
        
        // Set select timeout (max 2 seconds per iteration)
        max_tv.tv_sec = 2;
        max_tv.tv_usec = 0;
        struct timeval* tvp = ares_timeout(channel.get(), &max_tv, &tv);
        
        int result = select(nfds, &read_fds, &write_fds, nullptr, tvp);
        ++loopCount;
        
        if (result < 0) {
            int err = errno;
            if (err == EINTR) {
                // Interrupted by signal, retry
                LOG_BCK_DEBUG("DNS resolution: select() interrupted, retrying (loop {})", loopCount);
                continue;
            }
            LOG_BCK_ERROR("select() failed during DNS resolution: {} (errno: {})", 
                          std::strerror(err), err);
            break;
        } else if (result == 0) {
            // Timeout - let ares_process handle internal timeout logic
            LOG_BCK_DEBUG("DNS resolution: select() timeout, processing (loop {})", loopCount);
        }
        
        ares_process(channel.get(), &read_fds, &write_fds);
    }
    
    if (!ctx.done) {
        LOG_BCK_ERROR("DNS resolution incomplete for {} after {} iterations", hostname, loopCount);
        return "";
    }
    
    if (ctx.status != ARES_SUCCESS) {
        LOG_BCK_ERROR("DNS resolution failed for {}: {}", hostname, ares_strerror(ctx.status));
        return "";
    }
    
    LOG_BCK_DEBUG("Resolved {} -> {} via Yandex DNS{}", 
                  hostname, ctx.result,
                  interface.empty() ? "" : " on " + interface);
    return ctx.result;
}

} // namespace fuelflux

#endif // USE_CARES
