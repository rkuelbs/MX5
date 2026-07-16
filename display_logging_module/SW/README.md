# Display/Logger Software

This directory contains the C++/Qt acquisition, logging, replay, and simulation
software for the Raspberry Pi display/logger.

## Implemented

- Runtime parsing and encoding with `shared/miata.dbc`.
- Canonical `Sender.signal` names with sender-level uniqueness validation.
- Live CAN through configurable Qt CAN plugins.
  - `socketcan` for the Raspberry Pi/Linux vehicle interface.
  - `virtualcan` for hardware-free Windows development.
- Monotonic receive timestamps and a latest-value signal registry.
- Streaming VN300 binary input over a configurable FTDI/serial port, including
  every Common-group field and Attitude-group `LinBodyAcc`.
- Classic-CAN candump parsing, formatting, and replay.
- Realtime, accelerated, and unthrottled replay modes.
- Compressed MDF4 decoded-signal session logs and optional diagnostic raw
  candump logs.
- Configurable storage quota, minimum-free-space and age retention with
  oldest-completed-session cleanup and active/crash-incomplete protection.
- Bounded asynchronous logging with worker-owned files and clean queue draining.
- Embedded DBC/logging configuration with SHA-256 provenance hashes.
- DBC-driven ECM simulation with hot-reloaded values and fault injection.
- Animated ECM driving-cycle and VN300 circle-driving simulators with
  per-signal diagnostic locks, fault injection, and timestamped replay.
- Mouse-driven WCM simulator with all eight wheel buttons, momentary and stuck
  states, periodic status/event messages, and counter/message-drop fault modes.
- CAN/VN300 source-health signals and per-signal freshness queries.
- Separate Qt Quick `miata_dash` process with a filterable live diagnostics
  list, local freshness indication, chart-selection state, and animated fake
  data for desktop/Design Studio development.
- Versioned binary local IPC from `vehicle_loggerd` to `miata_dash`, with
  display-rate coalescing, lossless WCM events, snapshots, and reconnect.
- Four-action WCM/keyboard input routing, page/menu navigation, and an
  acknowledgeable warning lifecycle and overlay.
- Validated `dash.json` presentation metadata, constant or signal-driven
  thresholds, warning hysteresis/delays, and reusable analog/digital gauges.
- Bounded live diagnostic history for up to four selected signals, with
  independently auto-scaled traces and selectable 5-120 second windows.
- Raspberry Pi systemd service templates that start logging before the UI,
  restart crashed processes independently, and preserve orderly shutdown.
- Independent service web process with live logger/signal/storage/config status
  and log browsing/download/deletion plus an exact allowlist for dash restart,
  logger restart, and Pi reboot.
- Two-phase DBC/logging/dash upload with complete-set validation, token-matched
  atomic activation, and a previous-file backup.
- One-command Windows stack launcher for animated ECM, VN300, WCM, logger,
  dash, and service-web integration.
- Automated tests for decoding, encoding, replay, logging, ordering, and faults.

Production artwork and additional gauge styles are not implemented yet.

## Requirements

- CMake 3.21 or newer
- Ninja or another CMake generator
- A C++20 compiler
- Qt 6.5 or newer with Core, Network, QML, Quick, Quick Controls 2, Serial Bus,
  Serial Port, and Test
- The Qt SocketCAN plugin on Linux or VirtualCAN plugin on Windows
- Git access on the first configure, unless compatible MDF dependencies are
  already installed

The build first looks for an installed `MdfLib` package. Otherwise CMake fetches
pinned revisions of the MIT-licensed `mdflib` writer and its zlib/Expat
dependencies. The revisions are fixed in
[`cmake/MdfDependencies.cmake`](cmake/MdfDependencies.cmake). Set
`-DMIATA_FETCH_MDF_DEPS=OFF` to require a preinstalled package and prohibit
dependency downloads.

## Build and Test

From `display_logging_module/SW`:

```text
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The verified Windows kit is CMake 4.4.0, Ninja, MinGW 13.1.0, and Qt 6.11.1.
Raspberry Pi deployment templates and the hardwired PDM shutdown-handshake plan
are documented in [`deploy/systemd/README.md`](deploy/systemd/README.md).

## Run the Dash

On Windows, the dash defaults to animated fake data. On Raspberry Pi/Linux, it
defaults to the logger IPC source:

```text
display_logging_module/SW/build/dash_ui/miata_dash
```

Use `--fullscreen` on the target display. Search filters canonical names, and
the Plot checkbox records selections for the planned time-series page. Open
[`dash_ui/MiataDash.qmlproject`](dash_ui/MiataDash.qmlproject) in Qt Design
Studio. See [`dash_ui/README.md`](dash_ui/README.md) for the QML/CMake workflow
and backend contract.

Select either source explicitly on any platform:

```text
miata_dash --data-source fake
miata_dash --data-source ipc
```

The logger and dash default to the local server name
`miata-vehicle-data-v1`. Override both with `--ipc-name <name>` when running
isolated development instances. The dash reconnects automatically if it starts
before the logger or if either process restarts.

## Run the Complete Windows Development Stack

After building `build-mingw`, run this once from the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\display_logging_module\SW\tools\start_dev_stack.ps1
```

This starts the animated ECM and VN300 simulators, mouse-driven WCM simulator,
production logger, IPC-backed dash, and service web page. It opens
`http://127.0.0.1:8080/` automatically. ECM and VN300 scenario JSON files remain
hot-reloadable while everything is running. Press Ctrl+C in the launcher
terminal to request a clean logger exit, finalize the MDF, and stop the child
processes.

The VN300 simulator uses a development-only local socket carrying the same
binary packets consumed by the production parser, so no virtual COM-port driver
is needed. The Pi continues to use the FTDI serial source. Useful launcher
options include `-NoLogging`, `-NoBrowser`, `-WebPort 8081`, and
`-DurationSeconds 30`. `-NoGui` is provided for automated smoke tests.

## Run the Service Web Interface

The service page is a third process and observes the same local logger IPC as
the dash. On Windows, run it from the repository root with:

```powershell
.\display_logging_module\SW\build-mingw\service_web\miata_service_web.exe `
  --listen-address 127.0.0.1 `
  --port 8080 `
  --dbc .\shared\miata.dbc `
  --logging-config .\display_logging_module\SW\config\logging.json `
  --dash-config .\display_logging_module\SW\config\dash.json `
  --log-directory .\logs
```

Then open `http://127.0.0.1:8080/`. Live values appear when the logger is
running with IPC enabled. Service actions are intentionally unavailable on
Windows. On the Pi, the supplied systemd unit serves port 80 and grants only
three exact maintenance commands. See
[`service_web/README.md`](service_web/README.md) for the API and security scope.
Configuration activation is disabled by default and enabled explicitly only by
the Pi unit; the Windows launcher cannot overwrite repository configuration
through the browser.

## Logger-to-dash IPC

`vehicle_loggerd` starts the local signal server by default. Qt implements this
as a named pipe on Windows and a Unix-domain socket on Raspberry Pi OS. Only
the local machine can connect, and the socket is restricted to the same OS
user. Run both services under the same account on the Pi.

The logger sends the canonical signal catalog when a dash connects, followed
by a current-value snapshot. It batches updates at approximately 30 Hz and
coalesces ordinary signals to their newest value. Every `WCM.event_*` sample is
retained so quick press/release pairs cannot be lost between display frames.
Acquisition and MDF logging do not depend on a connected dash. Use `--no-ipc`
to disable the server.

## Configure Gauges and Warnings

[`config/dash.json`](config/dash.json) is the shared presentation contract used
by both gauges and warning evaluation. It owns display labels, precision, gauge
range/tick spacing, and low/high caution and warning rules. It does not duplicate
DBC scaling or units.

A threshold defines exactly one source: a constant `value` or a canonical
`signal`. Signal-driven thresholds may specify a `fallback` and
`source_stale_ms`. This supports future PDM-published boost limits while using
the same resolved limit for colored gauge hashes and warning evaluation.
Threshold rules also accept `hysteresis`, `activation_delay_ms`, and
`clear_delay_ms`.

Each signal may also define a `freshness` policy. `stale_after_ms` is measured
from receipt in the dash process, so it is valid across logger restarts and
does not compare unrelated monotonic clocks. `activation_delay_ms` prevents a
brief interruption from opening an overlay, `clear_delay_ms` requires stable
recovery, and `severity` is either `warning` or `critical`. The same stale
timeout dims its gauge and drives its warning:

```json
"freshness": {
  "stale_after_ms": 500,
  "activation_delay_ms": 250,
  "clear_delay_ms": 100,
  "severity": "critical"
}
```

The default configuration applies different timeouts to fast RPM/AFR data,
slower coolant/battery/fuel data, and logger-published CAN/VN300 health. A
fresh `LOGGER.can_healthy` or `LOGGER.vn300_healthy` value of zero raises a
separate source-health warning; loss of those health updates raises a stale
warning.

Warnings for a source that is intentionally unpowered can be gated by a
periodic control signal:

```json
"enabled_when": {
  "signal": "PDM.ecm_powered",
  "equals": 1,
  "fallback": false,
  "source_stale_ms": 500
}
```

`PDM.ecm_powered` is only an illustrative name and is not added to the DBC by
this example. Once the PDM power-status names are finalized, add the condition
to ECM and WCM presentations. A missing/stale gate uses `fallback`; `false`
prevents nuisance downstream warnings while the independently monitored PDM
messages still identify loss of the always-powered authority. When a gate
changes from disabled to enabled, the full signal stale timeout starts again,
so an ECM/WCM warning is not raised at the ignition edge before that module has
had time to boot and transmit.

The embedded default is used automatically. Pass `--dash-config <path>` to load
an external file without rebuilding the dash. Invalid JSON, gauge ranges,
threshold sources, or timing values prevent dash startup with an explicit
error. The initial configured coolant caution/warning values are examples to be
reviewed against the completed vehicle.

A future PDM-controlled boost gauge can reference controller-published limits:

```json
"high_warning": {
  "signal": "PDM.boost_warning_limit",
  "fallback": 15,
  "source_stale_ms": 500,
  "hysteresis": 1
}
```

Those placeholder PDM signals must be added to the shared DBC when the boost
status protocol is defined; the dash should display the controller's actual
active limit rather than infer it from a selector position.

## Replay a CAN Log

Run from the repository root:

```text
display_logging_module/SW/build/src/vehicle_loggerd \
  --dbc shared/miata.dbc \
  --replay display_logging_module/SW/tests/data/ecm_sample.log \
  --replay-fast \
  --log-directory logs/replay_test
```

Omit `--replay-fast` for timestamp-paced playback. Use `--replay-speed 2.0`
for twice-real-time playback. Replays terminate automatically after the final
frame. `--no-log` disables output logs. Raw CAN is not recorded by default; add
`--raw-can-log` only when a bus-level diagnostic capture is needed.

For interactive CAN replay, start all three processes with the same explicit
control name:

```powershell
.\display_logging_module\SW\build-mingw\src\vehicle_loggerd.exe --dbc .\shared\miata.dbc --replay .\capture.log --no-log --replay-control-name miata-replay-dev
.\display_logging_module\SW\build-mingw\dash_ui\miata_dash.exe --data-source ipc --replay-control-name miata-replay-dev
.\display_logging_module\SW\build-mingw\service_web\miata_service_web.exe --listen-address 127.0.0.1 --replay-control-name miata-replay-dev
```

The diagnostics page and service web page then provide play, pause, seek, and
speed controls. Controlled replay remains alive at end-of-file so it can be
restarted or sought. The separate control socket is rejected for live input,
fast replay, and replay logging; `--no-log` prevents a backward seek from
creating non-monotonic MDF timestamps. Production signal IPC remains data-only.

Accepted input lines use classic candump syntax:

```text
(0.050000000) can0 5F0#0000000000000BB8
```

CAN FD and RTR records are intentionally rejected until their logging and DBC
requirements are defined.

## Configure Decoded Logging Rates

[`config/logging.json`](config/logging.json) assigns every canonical decoded
signal to a named rate group. The supplied groups are `native`, `fast` (20 Hz),
`medium` (10 Hz), `slow` (1 Hz), and `off`. Group names and rates are ordinary
configuration and can be changed without rebuilding the logger.

```json
{
  "storage": {
    "minimum_free_bytes": 2147483648,
    "maximum_total_bytes": 21474836480,
    "maximum_age_days": 30,
    "cleanup_interval_seconds": 60
  },
  "default_group": "off",
  "rate_groups": {
    "native": { "mode": "native" },
    "fast": { "rate_hz": 20 },
    "slow": { "rate_hz": 1 },
    "off": { "mode": "off" }
  },
  "signals": {
    "ECM.rpm": "native",
    "ECM.clt": "slow"
  }
}
```

The decoder and latest-value registry always process every incoming sample.
The rate policy applies only at the persistence boundary. A periodic group
records the first received sample whose timestamp is at least one configured
interval after the preceding recorded sample; it does not interpolate or
manufacture samples when the source is slower than the configured rate.

Unknown canonical names and undefined groups prevent startup, catching DBC or
configuration spelling mistakes. Signals omitted from `signals` use
`default_group` and produce a startup warning. The default is deliberately
`off`, so a newly added DBC signal cannot silently increase log size. Use
`--logging-config <path>` to select another configuration.

The `storage` policy is enforced before a session starts and at its configured
interval. Zero disables the total-size or age limit; minimum free space may
also be zero. Cleanup groups the `.mf4` and optional `.can.log` files by their
UTC session stem and removes the oldest completed session first. An adjacent
`.active` marker protects the current file. If a crash leaves that marker, the
session is retained as incomplete rather than silently deleted. If completed
logs cannot satisfy the limits, logging closes and stops instead of consuming
the remaining filesystem; acquisition, IPC, and the dash continue.

## VN300 Binary Input

Enable the serial source by supplying the FTDI port name. It is disabled when
`--vn300-port` is omitted:

```text
display_logging_module/SW/build/src/vehicle_loggerd \
  --plugin socketcan --can-interface can0 \
  --vn300-port /dev/ttyUSB0 --vn300-baud 921600
```

On Windows the port name is typically `COM3` or similar. Serial framing is
8 data bits, no parity, one stop bit, and no flow control. The logger does not
write VN300 configuration registers; configure Binary Output Register 75, 76,
or 77 separately for the desired rate and fields.

The parser derives each packet layout from its group/type masks, so any subset
of the Common group is accepted without rebuilding. It additionally accepts
Attitude `LinBodyAcc` (type bit 6). Other selected Attitude fields are safely
skipped, while unsupported groups or type bits are rejected. CRC failures are
discarded and the stream resynchronizes at the next `0xFA` sync byte.

For all 15 Common fields plus Attitude `LinBodyAcc`, the group/type selection is
`11,7FFF,0040`. A 50 Hz output on UART 1 (rate divisor 8 from the VN300's
400 Hz IMU rate) is therefore configured in the form:

```text
$VNWRG,75,1,8,11,7FFF,0040*XX
```

Replace `XX` with the command checksum produced by the configuration tool. The
complete packet is about 218 bytes. At 50 Hz it uses roughly 109 kbit/s with
8N1 framing, leaving ample margin at 921600 baud. Lower rates or fewer selected
fields reduce this further. Disable other asynchronous output on that port.
The service reports cumulative CRC and unsupported-format counts when they
change, which will expose an overloaded or noisy link.

VN300 `uint64` time fields are exposed as seconds rather than raw nanoseconds.
This retains useful precision in the MDF double-valued physical channels and
makes the values directly usable in analysis tools. The source ICD is
[`../vn300-icd-v1_1_0_1-(icd30004-r1).pdf`](../vn300-icd-v1_1_0_1-(icd30004-r1).pdf).

## Logging Worker

Acquisition never performs file writes. It places selected decoded samples and
raw frames only when `--raw-can-log` is enabled onto a bounded queue. A dedicated
worker owns both session files, periodically flushes the optional raw text log,
and drains and finalizes MDF4 before shutdown.

The default queue capacity is 65,536 records. Override it with
`--log-queue-capacity <records>` after measuring peak VN300 and CAN load. If the
queue fills, new records are dropped rather than blocking acquisition; the
service reports the dropped count once per second. Disk-write failures are also
reported by the service health timer.

Decoded data is written directly as compressed MDF4. Each canonical signal has
its own channel group and time master, preserving the timestamps selected by
its configured rate without repeating slow signals in faster records. Signal
channels contain decoded physical values and retain their DBC units. The exact
DBC and logging configuration are embedded as compressed MDF attachments;
SHA-256 hashes are also written into the MDF header description.

The development `SessionLogSink` can still write CSV for focused unit tests,
but `vehicle_loggerd` no longer creates decoded CSV logs.

## Run the ECM Simulator on Windows

Start the logger in one terminal:

```text
display_logging_module/SW/build/src/vehicle_loggerd \
  --plugin virtualcan \
  --can-interface can0 \
  --dbc shared/miata.dbc
```

Start the simulator in another terminal:

```text
display_logging_module/SW/build/src/ecm_can_simulator \
  --plugin virtualcan \
  --interface can0 \
  --dbc shared/miata.dbc \
  --scenario display_logging_module/SW/config/ecm_simulator.json
```

VirtualCAN uses a local TCP bus, so no CAN adapter is required. Edit and save
the scenario while the simulator is running; changes are applied automatically.
See [simulator.md](simulator.md) for the scenario and fault controls.

## Run the WCM Simulator on Windows

With `vehicle_loggerd` and `miata_dash --data-source ipc` already running on
the same VirtualCAN channel, start the graphical wheel-button simulator:

```powershell
.\display_logging_module\SW\build-mingw\wcm_simulator_ui\wcm_simulator.exe `
  --plugin virtualcan `
  --interface can0 `
  --dbc shared\miata.dbc
```

Press and hold a button card for a normal momentary input. The simulator sends
the rising event immediately, keeps the bit set in periodic `WCM.inputs`, then
sends the falling event with measured press length when released. Use
**Hold / stuck** and the fault controls for diagnostics. Buttons 0-3 drive dash
navigation; buttons 4-7 are transmitted for future PDM testing but are not
treated as dash actions.

## Source Health and Freshness

The registry retains each sample's receive timestamp and exposes age/freshness
queries independently of logging rate. `vehicle_loggerd` also publishes these
canonical diagnostic signals every 100 ms:

- `LOGGER.can_enabled`, `LOGGER.can_healthy`, and `LOGGER.can_rx_age`
- `LOGGER.can_decode_errors`
- `LOGGER.vn300_enabled`, `LOGGER.vn300_healthy`, and `LOGGER.vn300_rx_age`
- `LOGGER.vn300_crc_errors` and `LOGGER.vn300_format_errors`
- `LOGGER.storage_healthy`, `LOGGER.storage_free_bytes`, and
  `LOGGER.storage_used_bytes`
- `LOGGER.storage_session_count`, `LOGGER.storage_incomplete_sessions`,
  `LOGGER.storage_cleanup_errors`, and `LOGGER.logging_active`

The default stale timeout is 500 ms for each source. Override it with
`--can-stale-ms` or `--vn300-stale-ms`. A disabled source is considered healthy
and distinguished by its `enabled` signal; an enabled source is healthy only
when connected and receiving valid data within its timeout.

## Remaining Production Logging Work

Before track use, the MDF writer and queue sizing must be benchmarked at maximum
CAN and VN300 rates, including deliberately slow storage and clean PDM-requested
shutdown. Storage quotas and oldest-first retention are implemented, but must
still be qualified against the target filesystem. Abrupt-power-loss MDF
recovery and time/size-based rotation within one long-running logger process
remain future work. MDF finalization depends on the normal PDM shutdown
handshake or another orderly process exit; an abrupt loss leaves the session's
`.active` marker so it is protected and visible for diagnosis.

## Benchmark the Logging Path

`logger_benchmark` generates deterministic decoded samples through the same
bounded worker and compressed MDF4 sink used by `vehicle_loggerd`.

```text
display_logging_module/SW/build/src/logger_benchmark \
  --dbc shared/miata.dbc \
  --logging-config display_logging_module/SW/config/logging.json \
  --records 100000 \
  --aggregate-rate 10000 \
  --queue-capacity 65536
```

Without `--realtime`, this measures maximum producer/worker throughput and is
expected to expose queue overflow if the producer outruns the writer. Add
`--realtime` to test a sustained target rate. The report separates producer
time from queue-drain/finalization time and includes dropped records and final
MDF4 size. Raspberry Pi/storage qualification results should eventually be
recorded alongside the hardware test plan. See
[`benchmark.md`](benchmark.md) for the current Windows baseline and the Pi test
matrix.
