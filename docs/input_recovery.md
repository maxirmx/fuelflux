# Input recovery and controller ownership

The controller loop owns the session, transitions, and dispensing decisions.
Keyboard/card callbacks carry copied values; measurement callbacks capture the
measurement generation. Internal transition events drain before the next external
message. `getStatus()` publishes a coherent copy after processing; reference-based
reads of live session fields are not a cross-thread interface.

`handleKeyPress`, `handleCardPresented`, and flow/pump ingress are asynchronous.
`synchronize()` is an explicit completion barrier for already-queued controller
messages, not for subsequently arriving backend results. Before `run()` starts it
can drive queued messages and worker completions synchronously for tests. Do not
call it from an event action or worker. Tests should wait for a published state or
use injected transports and completion gates, rather than sleep after callbacks.

## Workers and stopping

The backend worker owns authorization, reporting, persistence, and deauthorization
sequencing. The display worker owns runtime rendering/reset; pending frames are
coalesced. A separate flow-control worker arms, stops, and joins measurement
without blocking the controller loop. Arming acquires the GPIO line and subscribes
to edge events before returning success. The controller enables the pump only
after it receives a successful arming result for the current measurement
generation. Worker results and runtime flow faults contain copied data and
generations; stale results cannot change or stop a newer session.

STOP, target crossing, no flow, and input faults enter `RefuelingStopping`. The
controller commands pump-off first and checks its result. Final measurement then
precedes `RefuelDataTransmission`. Duplicate stop requests do not duplicate reports.
A transaction must be delivered or retained in the existing backlog/dead-message
storage before completion. Persistence or final-measurement failure inhibits new
sessions and retains controller data for investigation. A failed pump-off is never
represented as completed dispensing.

## Input recovery

Keyboard and NFC workers own their device handles throughout open/poll/close.
An I/O failure marks the device unhealthy and starts retries at 1, 2, 4, 8, 16,
and then 30 seconds. Retry waits are interruptible during shutdown. Keyboard
reconnect resets debounce and requires release before accepting a key.

NFC negative results are errors; an empty successful poll is normal. Card delivery
is enabled only in states that invite a card, including authorization failure
screens. Communication checks continue while delivery is disabled. NFC uses one
short polling cycle and an explicit finite command timeout.

The controller checks successful-I/O timestamps, not time since the last key or
card. Five seconds without completed I/O marks an otherwise connected input
worker stalled. Timing constants are in `include/timing_config.h`. A detected
failure of either configured input stops dispensing, preserves its final volume,
and blocks new sessions. After recovery and transaction finalization, the system
returns to Waiting and requires a fresh session. It never resumes the pump.

No global I2C lock or shared-bus reset is used. A blocked kernel call cannot be
made interruptible by C++: retain its worker/handle until it exits. Do not detach
or close/reopen its handle concurrently. Shutdown may wait for outstanding device
or backend I/O; the former two-second cleanup timeout was not a safe lifetime bound.

## Field diagnosis

Before restarting an affected controller, collect the service journal and kernel
messages covering the incident. For example, on the controller:

```sh
journalctl -u fuelflux --since '30 minutes ago' --no-pager
journalctl -k --since '30 minutes ago' --no-pager
pid=$(systemctl show -p MainPID --value fuelflux)
ps -L -p "$pid" -o pid,tid,stat,wchan:32,comm
```

Record the displayed state, device health/reconnect messages, and whether the
pump was running. Queue-age/depth and slow-handler logs distinguish event-loop
congestion from device failure. Do not infer a deadlock from an idle screen or a
period with no user input.

If thread stacks are required, capture them during a maintenance window with
`gdb -p "$pid" -batch -ex 'thread apply all bt'`: attaching a debugger briefly
pauses the process. Do not attach while dispensing. Redact credentials and tokens
before sharing existing historical logs. New controller diagnostics omit input
payloads and backend request bodies.

## Release validation

Automated tests exercise burst ordering, stale observations, delayed backend and
display workers, I/O errors, retries, and final-volume reporting after input faults.
Build and run both console tests and Linux VID/LEGACY configurations. Run the
controller/recovery tests under ThreadSanitizer on a supported Linux host; a
sanitizer startup failure is not a successful race check.

Hardware checks still required before deployment:

1. Disconnect/reconnect each input device in Waiting, volume entry, and dispensing.
2. Inject shared-I2C faults and stalled I/O. Verify the other workers remain alive,
   pump-off is issued, and no session resumes automatically.
3. Measure target/STOP/fault-to-pump-off latency under maximum flow, queued input,
   slow backend responses, and slow display operations. Confirm acceptable limits
   on the actual controller; the software makes no hard real-time guarantee.
4. Verify the final metered volume and one transaction through repeated stop/fault
   notifications and during shutdown.
5. Release to one monitored controller for 24 hours. Expand only after reviewing
   reconnects, queue delay, transaction counts, and unexplained input loss.

This change addresses established ownership and silent-error defects. The exact
cause of the original field incident remains unconfirmed without field evidence.

## Startup, aborted starts, and shutdown barriers

Input workers publish an initializing state before their first open/poll. New
sessions remain inhibited until all configured inputs are ready. Initialization
becomes a fault on an explicit I/O error or expiration of the stall deadline.
Startup progress messages render synchronously before the display worker starts.

Health is checked before entering Refueling. A rejected entry cannot report a
cleared session. Flow-meter arming runs on the flow-control worker, and the pump
remains off until the current generation reports that observation is ready. An
arming failure enters the equipment-fault path without producing a zero-volume
transaction. A runtime GPIO monitoring failure publishes a generation-tagged
fault, immediately enters the idempotent pump-stop path, retains the last measured
volume, and inhibits new sessions until flow-meter recovery. Recovery never
resumes the interrupted dispensing session. A failed pump start still confirms
pump-off and finalizes the measurement: zero delivery aborts without a refuel
report, while measured delivery is retained through the normal reporting path.
Backend exceptions leave local storage fallback reachable for both refueling and
intake. An unknown backend
outcome uses the existing backlog retry semantics; this does not introduce backend
idempotency or eliminate delivery ambiguity.

The event-loop exit path joins workers and shuts down peripherals even when
shutdown originates on the owner. Barrier admission and loop termination share
the lifecycle lock. Queued barriers behind shutdown complete against the final
snapshot; trailing commands are canceled. Calls during cleanup or after shutdown
return without posting a barrier. A barrier is not a worker-cleanup completion
signal: use shutdown/run completion for dependency lifetime coordination.

## Required-device recovery and command completion

A required peripheral's startup failure leaves the controller in Error. Healthy
peripherals and the keyboard/card recovery workers remain alive. STOP in Error
retries failed pump/flow initialization on the flow-control worker and schedules
display reset on the display worker. New sessions stay inhibited until all required
initializations succeed and both configured inputs are healthy. Shutdown waits for
an in-progress recovery before destroying its dependencies.

Input delivery refreshes aggregate health before applying the message, so the
first healthy key/card need not wait for the periodic health check. This does not
bypass another device's fault or unfinished transaction finalization.

An expired or already-pending inactivity timeout rejects entry into Refueling.
Exceptions during measurement/pump startup command pump-off and finalize the
measurement, including when an adapter throws after enabling output. Zero-volume
aborts do not create a refuel report; measured delivery is retained. Pump-off
exceptions keep the retry path active.

The off-owner simulation API waits for the queued command's actual result. It
returns false when unsupported, inhibited, or canceled by shutdown. Only the
caller waits; the controller event loop does not wait on a completion future.

## Shutdown and durable receipts

Both pre-loop and running-loop shutdown keep processing stop retries until the
output is confirmed off and active measurement finalization finishes. Logical
pump-off failures remain authoritative even when an adapter reports false after
throwing. An unexpected idle output is stopped without manufacturing a sale.
Input/ordinary command admission closes when shutdown is consumed; all ingress
closes atomically with the final queue drain. Late reset calls return false.

Every completed refueling/intake operation first receives a durable local receipt
in the message-storage database. If the backend executor rejects the report, a
reserved persistence worker journals it and routes it to backlog without touching
the busy backend. Only one report can be outstanding, and this worker remains
alive until its completion has been processed. Complete storage failure remains
a fault with data held in memory; software cannot guarantee persistence on failed
media or recovery after power loss when no durable write succeeded.

Allowance deduction and an applied-receipt ID commit together in the user-cache
database. A separate receipt marker can therefore be recovered after a crash
without repeating the deduction. Receipt data remains durable even if backlog
insertion fails. Failed accounting inhibits sessions; startup reconciles unfinished
receipts before starting cache synchronization or admitting users. Preserve both
databases when upgrading/restarting. Completed receipts and applied IDs remain as
an audit trail; storage retention/archival must preserve IDs needed by unfinished
receipts. Do not roll back to a binary that ignores unfinished receipts without
first reconciling them.

Local backlog promotion is atomic and idempotent. Remote delivery still uses the
existing wire format and retry semantics: a crash after the server accepts a report
but before local acknowledgement can require retry with an unknown outcome. This
change does not provide server-side exactly-once delivery.

Online intake and refueling clean up backend authorization before completing.
The next authorization also checks for an uncleared session. Rendering exceptions
mark the display unready and require successful display reset before new sessions.


Controller reports use the backend's unpersisted report methods: only receipt
promotion inserts backlog/dead-message rows. Legacy callers retain their existing
backend persistence behavior. This removes the crash window between two separate
local persistence paths; remote acknowledgement ambiguity remains unchanged.

Controller cleanup uses completion-aware deauthorization on its backend worker.
A failed request retains the old token so the next authorization must retry cleanup
first. Session-end cleanup and local-only report cleanup have a coalesced, reserved
FIFO queue slot and count as pending work through shutdown. The controller loop
never waits on HTTP; network timeouts still bound each worker attempt. Legacy
backend users retain the existing asynchronous Deauthorize API.

The asynchronous deauthorization contract is token-scoped. Every successful
authorization returns a token that identifies that specific server-side session,
and `/api/pump/deauthorize` may invalidate only the session named by its bearer
token. It must not invalidate a newer session merely because both sessions use the
same controller UID. `Deauthorize()` captures the current token, clears only the
corresponding local session, and sends that captured token without reading or
modifying any later session state. Callers may therefore authorize a new session
without waiting for the earlier network cleanup. A backend adapter or server that
cannot guarantee token-scoped invalidation must not use this asynchronous contract;
it must serialize cleanup with the next authorization or provide a blocking cleanup
operation instead.

Cleanup requests carry session generations; coalesced requests cannot clear a
newer backend session. Executor shutdown changes its wait predicate under the
queue mutex before notifying workers, preventing a missed shutdown notification.

During cache population, successful local debits copy the active row to standby
and mark that UID locally changed. Later rows from the overlapping server snapshot
cannot overwrite it. Unrelated rows still refresh, and the next population starts
with a fresh change set. Historical receipt IDs are not reapplied to server values.
