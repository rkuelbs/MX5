# Display/Logger Software Architecture

## Design Goals

- Begin acquiring and logging data before the dash UI starts.
- Keep acquisition and logging alive if the UI exits or restarts.
- Support live Raspberry Pi operation and fake/replay development on Windows.
- Add CAN signals by updating the shared DBC rather than editing decoder code.
- Add VN300 fields, derived signals, and dash pages without changing unrelated
  modules.
- Use monotonic timestamps for ordering and a common session clock for all
  inputs.

## Process Boundary

```text
shared/miata.dbc
       |
       v
SocketCAN/VirtualCAN -> CAN endpoint -> DBC codec --+
                                         |
VN300 -> serial source -> VN parser ------+-> signal registry -> log sinks
                                         |
replay source ----------------------------+
                                         |
                                         `-> local IPC -+-> Qt dash UI
                                                        `-> service web UI
```

`vehicle_loggerd` owns hardware acquisition, decoding, signal validity, logging,
and the PDM shutdown handshake. `miata_dash` will own presentation only. The two
processes use a versioned binary Qt local-socket transport, implemented as a
Windows named pipe or a Unix-domain socket on Raspberry Pi OS. Restarting the
UI cannot interrupt logging. Access is local and restricted to the same OS
user.

`miata_service_web` is a third, independent process. It consumes the same
read-only signal IPC and serves bounded HTTP status/signal responses. It is not
in the logging or rendering path and has no CAN transmit interface. Its Linux
maintenance runner recognizes only dash restart, logger restart, and Pi reboot;
systemd/sudo policy grants those exact instrumentation-service commands.

The production IPC path is data-only. Replay commands use a separately named,
explicitly enabled local endpoint that exists only for real-time replay with
logging disabled. The dash and service web client can play, pause, seek, and
change speed; live acquisition never creates or accepts this control endpoint.

The logger sends definitions and a latest-value snapshot to each new client.
Ordinary samples are coalesced to their newest value and flushed at roughly
30 Hz. WCM event samples are never coalesced: every timestamped event triple is
delivered so the dash can reconstruct quick presses even inside one display
batch. A slow client is disconnected instead of allowing unbounded buffering;
the dash then reconnects automatically.

## Signal Contract

Every sample contains:

| Field | Purpose |
|---|---|
| Canonical name | Stable lookup key such as `ECM.rpm` |
| Value | Decoded physical value |
| Unit | Unit supplied by the DBC or source definition |
| Monotonic timestamp | Acquisition ordering and freshness |
| Source | CAN, VN300, replay, or derived |

CAN canonical names are `Sender.signal`. Message names are not included. A DBC
load fails if a sender defines the same signal name in more than one message.
Different senders may use the same signal name, such as `ECM.batt_v` and
`PDM.batt_v`.

The DBC owns CAN identifiers, byte order, scaling, offset, valid range, and
physical units. `config/dash.json` owns presentation-only properties such
as labels, decimal precision, warning thresholds, and page placement. It must
not duplicate CAN scaling.

`DashConfigStore` resolves each threshold from either a constant or another
canonical signal with optional stale fallback. The same resolved objects feed
gauge zone coloring and `WarningEvaluator`. Gauge scales remain stable when a
dynamic threshold moves. Warning activation/clear delays and hysteresis affect
alert state but never delay visualizing a changed limit.

## DBC Update Behavior

The backend loads `shared/miata.dbc` at startup. A normal DBC update therefore
does not require regenerated C++ code. DBC parser errors, duplicate canonical
names, or messages without transmitters prevent startup; parser warnings are
reported explicitly.

Production MDF4 logs embed the DBC and logging configuration as compressed
attachments and include their SHA-256 hashes in the header. Every session can
therefore be interpreted with the exact definitions used while recording.
Automatic live DBC replacement while driving is intentionally out of scope; the
service should be restarted to apply a new definition deterministically.

Qt's DBC parser and frame processor are isolated in `DbcDecoder` because those
Qt APIs are preliminary and support only part of the DBC specification. Golden
frame tests protect the public signal contract if the decoder is later changed.

## Runtime Rules

- Decode and publish every received sample regardless of its configured logging
  rate. Display and warning behavior must not depend on persistence settings.
- Persist decoded signals through named configurable rate groups. New DBC
  signals default to `off` with a startup warning until explicitly assigned.
- Store each selected signal in an MDF4 channel group with its own time master.
  This preserves asynchronous sample times and avoids repeating slow channels
  at the rate of unrelated fast channels.
- Keep continuous raw CAN logging disabled by default. It is an explicit
  diagnostic option and, later, a candidate for a bounded triggered buffer.
- Never block the Qt acquisition event loop on disk I/O or UI work. Selected
  records enter a bounded queue; the logging worker owns, flushes, and closes
  the files.
- Timestamp live CAN and VN300 receipt from the same process-owned monotonic
  clock. Replay and live serial input cannot be mixed until a timeline mapping
  is explicitly defined.
- Drop new log records and report the count if the queue is exhausted. Do not
  allow storage latency to grow memory without limit or stall acquisition.
- Batch UI updates at the display rate instead of forwarding every high-rate
  sample directly into QML.
- Mark signals stale after a configurable source-specific timeout.
- Gate downstream-source freshness and value warnings with periodic PDM power
  state. PDM health itself remains ungated because PDM messages are expected
  whenever the dash is powered.
- Publish logger-owned source health as canonical `LOGGER.*` signals so the UI
  and MDF logs observe the same connection, age, and error state.
- Recover from CAN and serial disconnects without terminating the logger.
- Close and sync logs before asserting the PDM power-off-ready signal.
- Mark open sessions explicitly, retain active/crash-incomplete files, and
  remove only oldest completed sessions when configured age, quota, or free
  space limits are exceeded.

## Dash interaction architecture

The dash converts every operator input into one of four presentation-only
actions: navigate up, navigate down, activate, or menu/back. Keyboard input and
WCM CAN input share the same `DashInputController`; QML pages never bind
directly to CAN signal names.

WCM edge messages provide responsive actions and the periodic `WCM.inputs`
bitmask provides authoritative state and recovery if an edge message is lost.
Both update one deduplicating button-state table. The first periodic state
establishes a baseline and cannot create a synthetic press during dash startup.
Event fields are processed atomically using their shared acquisition timestamp.

Actions are routed in priority order:

```text
unacknowledged warning -> open menu -> current page/global page navigation
```

Acknowledging a warning dismisses its modal presentation but does not clear the
underlying active warning. Severity escalation re-arms an acknowledged warning;
it clears only after the configured hysteresis and clear delay are satisfied.
The diagnostics page retains keyboard and mouse
affordances for filtering, selection, and future charts.

## Planned Stages

1. Completed: DBC codec, live CAN input, and signal registry.
2. Completed: replay, raw/decoded log sink interfaces, and ECM simulation.
3. Completed: decoded-signal rate groups and a bounded logging worker.
4. Implemented: compressed MDF4 output and embedded configuration provenance;
   sustained-throughput and power-loss testing remain.
5. Completed: VN300 Common-group and `LinBodyAcc` binary packet input with
   streaming resynchronization and CRC validation; reconnect behavior remains.
6. Completed: animated ECM/VN300 simulation, independent replay, source health,
   and registry freshness; combined CAN/VN replay merging remains.
7. Completed: the independent Qt Quick process, diagnostics signal model,
   filtering, selection, freshness display, animated fake provider, WCM input
   adapter, keyboard mapping, page/menu navigation, and warning lifecycle are
   connected through versioned, reconnecting local logger IPC.
8. In progress: validated presentation/threshold/freshness configuration, the
   first Drive page, reusable analog/digital gauges, and stale/source-health
   warnings, bounded diagnostic charting, and replay controls are complete; add
   production artwork and additional gauge types.
9. In progress: systemd startup/restart ordering, readiness, event-loop
   watchdog notification, boot milestones, and the kernel GPIO shutdown
   handshake are scaffolded; finalize CAN/GPIO setup and electrical polarity,
   then perform boot-time and power-cut testing on the Pi.
10. Implemented: a standalone embedded service web page reports logger IPC,
    current signals, storage, configuration hashes, and a safe log catalog with
    streamed downloads/deletion, and exposes allowlisted instrumentation
    restart/reboot actions. Complete-set configuration staging, validation,
    token-matched atomic activation, and previous-file backups are implemented;
    authentication, TLS, and signed atomic software deployment remain future
    service work.
11. Completed: configurable oldest-first completed-session retention, storage
    health signals, active/incomplete session markers, and a one-command Windows
    ECM/VN300/WCM/logger/dash/web integration stack.
