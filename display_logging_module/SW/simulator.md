# Data Source Simulators

## ECM CAN Simulator

`ecm_can_simulator` generates every message transmitted by `ECM` in the shared
DBC. Message identifiers, payload layout, byte order, scaling, and range checks
come directly from the DBC.

The default scenario is [`config/ecm_simulator.json`](config/ecm_simulator.json).
The file is watched for changes, including the atomic replace behavior used by
many editors.

## Scenario Fields

| Field | Meaning |
|---|---|
| `rate_hz` | Broadcast rate for each ECM message, from 1 through 1000 Hz |
| `automatic_values` | Generate a repeatable driving cycle for gauge-relevant values |
| `signals` | Lock individual canonical signals to diagnostic values |
| `faults.paused` | Stop all ECM transmission while true |
| `faults.drop_frames` | CAN identifiers that should not be transmitted |
| `faults.dlc_overrides` | Force a message payload to an invalid or shortened DLC |

Every entry in `signals` is a per-signal lock and wins over automatic behavior.
For example, adding `"ECM.rpm": 6500` holds RPM while TPS, MAP, temperatures,
fuel level, AFR, pulse width, and other modeled values continue moving. Set
`automatic_values` to `false` to hold the entire scenario at its defaults and
explicit signal values.

Examples:

```json
{
  "rate_hz": 20,
  "automatic_values": false,
  "signals": {
    "ECM.rpm": 6500,
    "ECM.tps": 100,
    "ECM.map": 180,
    "ECM.clt": 230,
    "ECM.status1": 8
  },
  "faults": {
    "paused": false,
    "drop_frames": ["0x5F0"],
    "dlc_overrides": {
      "0x5F2": 4
    }
  }
}
```

This scenario holds engine values manually, sets an ECM status bitfield, drops
the message containing `ECM.rpm`, and shortens realtime group 2 from eight to
four bytes. The logger will retain the malformed raw frame while rejecting its
decoded signals.

Unknown signals, invalid CAN identifiers, invalid DLC values, and malformed
JSON are rejected. The last valid scenario continues running after a failed
reload.

## Record Without a Virtual Bus

The simulator can generate a replay file without publishing to CAN:

```text
ecm_can_simulator --no-bus --record ecm_run.can.log \
  --dbc shared/miata.dbc --scenario path/to/scenario.json
```

The process currently runs until stopped. Its candump output can then be passed
to `vehicle_loggerd --replay`.

## VN300 Simulator

`vn300_simulator` emits the same 218-byte binary packet expected from a VN300
configured for every Common field plus Attitude `LinBodyAcc`. Its default
scenario is [`config/vn300_simulator.json`](config/vn300_simulator.json).

The automatic model drives around a configurable circle. Sensor time advances,
latitude and longitude trace the circle, NED velocity follows its tangent, yaw
follows heading, and angular rate and lateral acceleration agree with the
configured speed and radius. Attitude, quaternion, delta values, temperature,
pressure, altitude, and the remaining Common fields also receive valid changing
or status values.

| Field | Meaning |
|---|---|
| `rate_hz` | Packet rate from 1 through 400 Hz |
| `circle.radius_m` | Simulated path radius |
| `circle.speed_mps` | Constant vehicle speed around the path |
| `circle.origin_latitude_deg` | Circle center latitude |
| `circle.origin_longitude_deg` | Circle center longitude |
| `circle.altitude_m` | Nominal ellipsoid altitude |
| `signals` | Per-signal diagnostic locks such as `VN300.yaw` |
| `faults.paused` | Stop packet generation |
| `faults.corrupt_crc_every` | Corrupt every Nth packet; zero disables |
| `faults.drop_every` | Drop every Nth packet; zero disables |

Generate a replay capture without serial hardware:

```text
vn300_simulator \
  --scenario display_logging_module/SW/config/vn300_simulator.json \
  --record vn300_circle.vnlog
```

Stop it after the desired duration, then replay through the production parser
and MDF logger:

```text
vehicle_loggerd \
  --no-can \
  --vn300-replay vn300_circle.vnlog \
  --replay-fast
```

For live integration, connect two virtual null-modem ports and give one to each
process:

```text
vn300_simulator --serial-port COM10 --baud 921600
vehicle_loggerd --vn300-port COM11 --vn300-baud 921600
```

On Linux, PTYs or a virtual serial pair can be used in the same way. The
scenario file is hot-reloaded. Every entry under `signals` locks only that
canonical value while the rest of the circular model keeps moving.

VN-only replay currently requires `--no-can`. CAN and VN300 captures need a
merged replay scheduler before two independent recorded timelines can be safely
interleaved into one globally ordered MDF session.

For driver-free desktop development, `--local-server <name>` publishes the
identical binary packets over a same-user local socket and the logger consumes
them with `--vn300-local <name>`. This transport is used by
`tools/start_dev_stack.ps1`; it changes only transport, not packet generation
or parsing, and is not used on the vehicle.

## WCM Button Simulator

`wcm_simulator` is a small Qt Quick control panel for testing steering-wheel
interaction with a mouse. It encodes the production `WCM_STATUS` and
`WCM_EVENT` layouts directly from `shared/miata.dbc`.

| Button ID | Input | Consumer |
|---:|---|---|
| 0 | Down | Dash |
| 1 | Up | Dash |
| 2 | Menu | Dash |
| 3 | Ack / Enter | Dash |
| 4 | Left turn | PDM |
| 5 | Right turn | PDM |
| 6 | Wiper | PDM |
| 7 | Flash | PDM |

A mouse press emits a rising-edge event and an immediate status frame. Holding
the mouse keeps the corresponding bit in the periodic `WCM.inputs` mask. Mouse
release emits a falling-edge event with `WCM.event_length` in milliseconds and
then an updated status frame. The **Hold / stuck** checkbox latches an input for
diagnostic testing.

Start the logger, dash, and simulator in separate PowerShell terminals from the
repository root:

```powershell
.\display_logging_module\SW\build-mingw\src\vehicle_loggerd.exe `
  --plugin virtualcan --can-interface can0 --dbc shared\miata.dbc
```

```powershell
.\display_logging_module\SW\build-mingw\dash_ui\miata_dash.exe `
  --data-source ipc
```

```powershell
.\display_logging_module\SW\build-mingw\wcm_simulator_ui\wcm_simulator.exe `
  --plugin virtualcan --interface can0 --dbc shared\miata.dbc
```

The controls can pause all transmission, independently drop status or event
frames, freeze the status counter, advance it by more than one count to create
counter gaps, and set the boost-encoder byte. `--status-rate` changes the
periodic rate from its 20 Hz default. `--no-bus --record <path>` creates a
candump replay file without a CAN interface.
