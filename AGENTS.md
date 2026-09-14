# Repository-wide architecture rules

These rules apply to all changes. Existing code is not evidence that an exception
is safe. Keep timing values in `include/timing_config.h` and operating details in
`docs/state_machine.md` and `docs/input_recovery.md`.

- The controller event loop owns session data, state-machine transitions, and
  dispensing decisions. Synchronous setup is allowed before the loop starts.
- Cross-thread producers publish typed messages containing copied data. Workers
  must not mutate controller session state or execute controller business logic.
- Drain internally generated transitions iteratively before accepting the next
  external message. Do not recursively transition from callbacks.
- Tag asynchronous results with the applicable session, operation, or device
  generation. Reject stale observations and completions.
- Keep handlers short: network, persistence, display rendering, and device
  recovery belong in workers. Do not wait for workers in event handlers.
- Synchronize queues, lifecycle, and published snapshots. Never invoke callbacks
  or join workers while holding locks they need. Do not add broad controller locks
  to compensate for violations of ownership.
- STOP, target reached, no flow, and input failure use one idempotent stop path.
  Confirm pump-off and capture final flow before reporting or clearing a session.
  Do not represent failed pump-off as successful completion.
- An input fault stops active dispensing and inhibits new sessions. Recovery is
  automatic; dispensing must never resume automatically.
- Do not detach device workers, self-join, close/reopen a handle concurrently, or
  destroy dependencies before their workers have exited.
- Tests for concurrency changes must exercise ordering, delayed work, stale
  results, failure/recovery, and shutdown. Distinguish device silence from stalled
  processing in diagnostics; never log PINs or card credentials.
- GPS, heater/temperature, and backlog workers remain independent and must not
  mutate controller session state.

Hardware fault injection, shared-I2C validation, and stop-latency measurements are
release requirements. Software cannot guarantee recovery from uninterruptible
kernel I/O; never work around it with unsafe worker detachment.
