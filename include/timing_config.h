// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

/**
 * Timing Configuration
 *
 * This file centralizes all timeout values and timing constants used across
 * the application. Keeping them here makes it easy to tune behaviour without
 * hunting through multiple source files.
 */

#include <chrono>

namespace fuelflux::timing {

// ─── State machine ────────────────────────────────────────────────────────────

// Inactivity timeout: how long to stay in a non-Waiting state before
// automatically returning to Waiting (seconds).
constexpr std::chrono::seconds kInactivityTimeout{30};

// State machine timeout thread: how often to check for inactivity (seconds).
constexpr std::chrono::seconds kTimeoutThreadPollInterval{1};

// ─── Controller ───────────────────────────────────────────────────────────────

// Default no-flow cancel timeout: pump stops if no flow pulses are received
// for this long during refuelling (seconds).
constexpr std::chrono::seconds kNoFlowCancelTimeout{30};

// Event loop: how long to block on the condition variable when the queue is
// empty (allows timely shutdown detection).
constexpr std::chrono::milliseconds kEventLoopWaitInterval{100};

// Event loop: sleep duration when the queue is empty and the wait expired
// without an event (avoids busy-spinning).
constexpr std::chrono::milliseconds kEventLoopIdleSleep{10};

// Shutdown: maximum time to wait for the event-loop thread to exit.
constexpr std::chrono::milliseconds kShutdownDeadline{2000};

// No-flow monitor thread: polling interval.
constexpr std::chrono::milliseconds kNoFlowMonitorInterval{200};

// ─── Backlog worker ───────────────────────────────────────────────────────────

// How often the backlog worker retries failed messages (seconds).
constexpr std::chrono::seconds kBacklogWorkerInterval{30};

// ─── Main application loop ────────────────────────────────────────────────────

// Main thread: sleep interval while waiting for shutdown signal.
constexpr std::chrono::milliseconds kMainLoopWaitInterval{100};

// Recovery: delay before restarting the controller after an error.
constexpr std::chrono::seconds kRecoveryRestartDelay{1};

// Permanent failure: sleep interval once the retry limit is exceeded.
constexpr std::chrono::hours kPermanentFailureSleepInterval{24};

// Maximum number of controller restart attempts before giving up.
constexpr int kMaxRetries{3};

// ─── HTTP / network ───────────────────────────────────────────────────────────

// TCP connection timeout for normal (Ethernet / Wi-Fi) networks (seconds).
constexpr long kHttpConnectTimeoutSec{5};

// Total HTTP request timeout for normal networks (seconds).
constexpr long kHttpTotalTimeoutSec{15};

// TCP connection timeout for SIM800C GPRS/2G networks (seconds).
// GPRS round-trip latency can be 1–3 seconds, so a much larger value is needed.
constexpr long kHttpConnectTimeoutSim800cSec{30};

// Total HTTP request timeout for SIM800C networks (seconds).
constexpr long kHttpTotalTimeoutSim800cSec{90};

// ─── DNS / c-ares ─────────────────────────────────────────────────────────────

// c-ares channel: per-attempt timeout in milliseconds (passed to ares_options).
constexpr int kDnsChannelTimeoutMs{10000};

// c-ares channel: number of retries per query (passed to ares_options).
constexpr int kDnsChannelRetries{3};

// Overall DNS resolution timeout guard (seconds). Prevents infinite hangs if
// c-ares keeps retrying. Should be > kDnsChannelTimeoutMs * kDnsChannelRetries.
constexpr int kDnsResolutionOverallTimeoutSec{45};

// Maximum time per select() iteration during DNS resolution (seconds).
constexpr int kDnsSelectTimeoutSec{2};

// TTL for the cached resolved IP of the backend API hostname.
constexpr std::chrono::hours kBackendApiDnsCacheTtl{24};

// ─── Cache manager ────────────────────────────────────────────────────────────

// Hour of day (local time) at which the daily cache population is scheduled.
constexpr int kCacheDailyUpdateHour{2};  // 2 AM

// Retry interval after a failed cache population attempt (minutes).
constexpr int kCacheRetryIntervalMinutes{60};  // 1 hour

// Number of user records fetched per API request during cache population.
constexpr int kCacheFetchBatchSize{100};

// ─── Authorisation (debug/test) ───────────────────────────────────────────────

// Extra delay injected during authorisation when ENABLE_AUTH_DELAY is defined.
constexpr std::chrono::seconds kAuthDelay{3};

// ─── Console emulator ─────────────────────────────────────────────────────────

// Console input dispatcher: sleep interval between keyboard input checks.
constexpr std::chrono::milliseconds kConsoleInputPollInterval{10};

// ─── Flow meter ───────────────────────────────────────────────────────────────

// Simulation thread: tick interval (controls simulation accuracy vs CPU usage).
constexpr std::chrono::milliseconds kFlowMeterSimTickInterval{100};

// Simulation / hardware monitor: callback interval (how often to invoke the
// volume-updated callback; smaller values increase CPU load).
constexpr std::chrono::milliseconds kFlowMeterCallbackInterval{100};

} // namespace fuelflux::timing
