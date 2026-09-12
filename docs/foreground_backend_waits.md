# Foreground backend waits and durable reporting

Set `fuelflux::timing::kForegroundBackendWaitTimeout` in
[`include/timing_config.h`](../include/timing_config.h) and rebuild. The default
is `std::chrono::seconds{5}`; a non-positive value fails compilation. This one
setting controls authorization cache fallback, the slow-connection warning,
and release of both refuel and intake transmission screens on every connection
type. HTTP connection/total deadlines and DNS deadlines remain independent.

## Authorization

Each attempt captures saved user and tank values, pending-card protection, and
writable local report-storage availability before starting its independent
backend session. The general user/tank snapshot comes from one cache generation.
The captured values stay unchanged for that attempt even if synchronization or
report delivery finishes meanwhile.

The display initially shows `Проверка...` / `Ожидайте`. An online result received
before the foreground deadline follows the existing allowance, role, and tank
selection checks. A network failure can use the captured snapshot; a definitive
authorization denial cannot.

When the foreground deadline expires, usable saved data automatically continues
through the existing cache authorization checks. Otherwise the display shows
`Медленное соединение`, `Ожидайте или`, and the keyboard profile's Cancel prompt
(`Нажмите ОТМЕНА (B)` for legacy, `Нажмите ОТМЕНА` for VID). The warning uses small
text lines so it fits the ST7565 as well as the larger ILI9488. Cancel immediately
returns to the welcome screen. Cancel before the warning retains its former
behavior.

Attempts have independent IDs and sessions. Abandonment signals transport
cancellation, including cancellable c-ares DNS waits on the GSM build. The
controller accepts an attempt's result only while that attempt is current.
A late successful unused session is closed asynchronously; it cannot update
the active session or cache.

## Reports and subsequent operations

Before enabling another operation, the controller stores the completed payload
and a protected per-card user/tank snapshot in one SQLite transaction, deducting
the refuel volume exactly once. Intake preserves the allowance. A persistence
failure shows `Ошибка записи` and enters the error procedure instead of showing
successful completion with an unrecorded transaction.

The transmission screen remains until delivery resolves or the foreground wait
expires. The existing completion screen retains the final volume and accepts a
new card or PIN. Screen release does not send the report again. The delivery
coordinator owns the reporting backend session, so the new operation cannot
reuse or close it.

A card with queued or in-flight reports immediately uses its protected snapshot
and locally reduced allowance, even when the general cache lacks its tanks.
Different cards authorize normally. Late delivery changes only its matching
report and attempt IDs. Network failures schedule retries; definitive rejections
move the unchanged payload to `dead_messages` and resolve that report. Once no
pending reports remain for the card, its next operation attempts online
authorization normally.

Authorization has a bounded worker and queue. A separate single delivery worker
coordinates both new reports and retries, with one active sender and ordered
delivery within each card. At most one additional original session is retained;
other queued reports obtain an independent session when selected. Controller
state and display transitions remain on the controller loop. Shutdown cancels
active network work and joins the owned workers. At process exit the shared
token-cleanup worker is also cancelled and joined before DNS/logging teardown.

## Persistence and compatibility

The `backlog` schema now has stable increasing report IDs, delivery attempt IDs,
queued/in-flight status, retry eligibility, and a payload interpretation flag.
Startup migrates legacy implicit rowids in order without changing payloads.
Legacy payloads retain their existing tank-mapping interpretation. New payloads
capture the backend tank ID and transaction timestamp once; retries do not
remap tanks or replace timestamps.

`card_report_state` stores the reduced allowance and saved user/tanks separately
from cache generations. Report acknowledgements and retries never deduct again.
Interrupted deliveries are recovered for retry at coordinator startup. Protected
card data and pending-card restrictions therefore survive restart.

Synchronization records resolved snapshot revisions before fetching server
data and may release protection only if those revisions remain unchanged and
the card still has no pending reports after the cache commit. A server fetch
started while reports were pending cannot restore an older allowance. A fresh
successful online authorization refreshes resolved protection and its revision,
so an older synchronization fetch cannot undo that authorization either.

Backend deduplication remains unverified. Network-failure retries are preserved.
A timeout can occur after the server accounted for a report but before its reply
arrived; stable local IDs and exclusive sending cannot prevent duplicate server
accounting in that case. Exactly-once accounting requires backend support for
an idempotency key or equivalent reconciliation protocol. The local report ID
is not sent as an undocumented backend field.

## Verification

`foreground_backend_test.cpp` exercises configurable waits, early/late results,
cache fallback, missing-cache cancellation, same/different-card continuation,
refuel/intake delivery, rejection, storage failures, and shutdown.
`report_delivery_test.cpp` covers atomic deductions, snapshot protection, stable
payloads, attempt matching, per-card ordering, migration, and restart recovery.
Backend tests include cancellation of an already-sent HTTP authorization;
c-ares cancellation tests are enabled in `USE_CARES` builds. Display tests check
the Russian warning and both Cancel labels against the small display's width.

Run `ctest --test-dir <build-directory> --output-on-failure`. The Windows console
suite and Linux container build with `TARGET_SIM800C=ON` have been exercised.
The Linux checks include cancellation while waiting for a deliberately withheld
DNS reply and while waiting for another resolver call. Physical displays and
the modem still require validation on the target hardware.
