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
- Bounded asynchronous logging with worker-owned files and clean queue draining.
- Embedded DBC/logging configuration with SHA-256 provenance hashes.
- DBC-driven ECM simulation with hot-reloaded values and fault injection.
- Animated ECM driving-cycle and VN300 circle-driving simulators with
  per-signal diagnostic locks, fault injection, and timestamped replay.
- CAN/VN300 source-health signals and per-signal freshness queries.
- Separate Qt Quick `miata_dash` process with a filterable live diagnostics
  list, local freshness indication, chart-selection state, and animated fake
  data for desktop/Design Studio development.
- Automated tests for decoding, encoding, replay, logging, ordering, and faults.

Logger-to-dash IPC and the production graphical gauge pages are not implemented
yet. The diagnostics dash and its source-neutral backend model are implemented.

## Requirements

- CMake 3.21 or newer
- Ninja or another CMake generator
- A C++20 compiler
- Qt 6.5 or newer with Core, Serial Bus, Serial Port, and Test
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

## Run the Diagnostics Dash

The dash starts with animated fake data and lists every signal configured in
`config/logging.json`:

```text
display_logging_module/SW/build/dash_ui/miata_dash
```

Use `--fullscreen` on the target display. Search filters canonical names, and
the Plot checkbox records selections for the planned time-series page. Open
[`dash_ui/MiataDash.qmlproject`](dash_ui/MiataDash.qmlproject) in Qt Design
Studio. See [`dash_ui/README.md`](dash_ui/README.md) for the QML/CMake workflow
and backend contract.

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

## Source Health and Freshness

The registry retains each sample's receive timestamp and exposes age/freshness
queries independently of logging rate. `vehicle_loggerd` also publishes these
canonical diagnostic signals every 100 ms:

- `LOGGER.can_enabled`, `LOGGER.can_healthy`, and `LOGGER.can_rx_age`
- `LOGGER.can_decode_errors`
- `LOGGER.vn300_enabled`, `LOGGER.vn300_healthy`, and `LOGGER.vn300_rx_age`
- `LOGGER.vn300_crc_errors` and `LOGGER.vn300_format_errors`

The default stale timeout is 500 ms for each source. Override it with
`--can-stale-ms` or `--vn300-stale-ms`. A disabled source is considered healthy
and distinguished by its `enabled` signal; an enabled source is healthy only
when connected and receiving valid data within its timeout.

## Remaining Production Logging Work

Before track use, the MDF writer and queue sizing must be benchmarked at maximum
CAN and VN300 rates, including deliberately slow storage and clean PDM-requested
shutdown. Abrupt-power-loss recovery, session rotation, storage quotas, and
oldest-first retention are not implemented yet. MDF finalization currently
depends on the normal PDM shutdown handshake or orderly process exit.

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
