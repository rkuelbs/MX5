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
                                         `-> local IPC -> Qt dash UI
```

`vehicle_loggerd` owns hardware acquisition, decoding, signal validity, logging,
and the PDM shutdown handshake. `miata_dash` will own presentation only. The two
processes will use a local IPC transport so restarting the UI cannot interrupt
logging. The IPC choice will be made after the signal update and startup
requirements are measured; it must also work on Windows.

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
physical units. Dash configuration will own presentation-only properties such
as labels, decimal precision, warning thresholds, and page placement. It must
not duplicate CAN scaling.

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
- Publish logger-owned source health as canonical `LOGGER.*` signals so the UI
  and MDF logs observe the same connection, age, and error state.
- Recover from CAN and serial disconnects without terminating the logger.
- Close and sync logs before asserting the PDM power-off-ready signal.

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
7. Add local IPC and a minimal `ECM.rpm` Qt Quick screen.
8. Add fake/replay controls and a modular dash component library.
9. Add service startup, watchdog, and PDM shutdown handshake integration.
