// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

/**
 * System Configuration
 *
 * This file contains all application-level timeout values, intervals, and
 * numeric constants. Centralising them here makes tuning and review straightforward.
 */

#include <chrono>

namespace fuelflux::config {

// Controller event-loop and thread-management timings
namespace controller {
    /// How long the event-loop condition variable waits before re-checking isRunning_.
    constexpr auto EVENT_LOOP_CV_TIMEOUT        = std::chrono::milliseconds(100);
    /// Sleep duration in the event-loop idle branch (no pending events).
    constexpr auto EVENT_LOOP_IDLE_SLEEP        = std::chrono::milliseconds(10);
    /// Maximum time allowed for the event-loop thread to exit on shutdown.
    constexpr auto THREAD_SHUTDOWN_TIMEOUT      = std::chrono::milliseconds(2000);
    /// Polling interval while waiting for the thread to acknowledge shutdown.
    constexpr auto THREAD_SHUTDOWN_POLL_INTERVAL = std::chrono::milliseconds(10);
    /// Minimum interval between consecutive display refresh calls in handleFlowUpdate.
    constexpr auto DISPLAY_UPDATE_MIN_INTERVAL  = std::chrono::seconds(1);
    /// Sleep interval inside the no-flow monitor thread.
    constexpr auto NO_FLOW_MONITOR_SLEEP        = std::chrono::milliseconds(200);
    /// Default timeout before cancelling a refuel session with no flow detected.
    constexpr auto NO_FLOW_CANCEL_TIMEOUT       = std::chrono::seconds(30);
} // namespace controller

// State-machine inactivity timeout
namespace state_machine {
    /// Duration of user inactivity that triggers an automatic session timeout.
    constexpr auto INACTIVITY_TIMEOUT           = std::chrono::seconds(30);
    /// How often the timeout-check thread wakes to evaluate inactivity.
    constexpr auto TIMEOUT_CHECK_INTERVAL       = std::chrono::seconds(1);
} // namespace state_machine

// Application startup, retry, and recovery
namespace application {
    /// Maximum number of consecutive start-up failures before entering permanent failure state.
    constexpr int MAX_RETRIES                   = 10;
    /// If at least this much time passes between failures the retry counter is reset.
    constexpr auto RETRY_RESET_INTERVAL         = std::chrono::hours(1);
    /// Delay before attempting to restart the controller loop after a failure.
    constexpr auto RECOVERY_RETRY_DELAY         = std::chrono::seconds(1);
    /// Sleep duration per iteration when the application is in permanent failure state.
    constexpr auto PERMANENT_FAILURE_SLEEP      = std::chrono::hours(24);
    /// Main-thread polling interval while waiting for the shutdown signal.
    constexpr auto MAIN_LOOP_SLEEP              = std::chrono::milliseconds(100);
    /// Interval at which the BacklogWorker processes unsent transactions.
    constexpr auto BACKLOG_WORKER_INTERVAL      = std::chrono::seconds(30);
} // namespace application

// Cache manager scheduling
namespace cache_manager {
    /// Hour of the day (UTC) at which the daily cache refresh is scheduled.
    constexpr int DAILY_UPDATE_HOUR             = 2;   // 2 AM
    /// How long to wait before retrying a failed cache population.
    constexpr int RETRY_INTERVAL_MINUTES        = 60;  // 1 hour
    /// Number of users fetched per backend request during cache population.
    constexpr int FETCH_BATCH_SIZE              = 100;
} // namespace cache_manager

// NFC card-reader polling
namespace card_reader {
    /// Delay between successive NFC poll attempts.
    constexpr auto POLL_DELAY                   = std::chrono::milliseconds(150);
    /// Cooldown after a successful card read before polling resumes.
    constexpr auto READ_COOLDOWN                = std::chrono::milliseconds(500);
} // namespace card_reader

} // namespace fuelflux::config
