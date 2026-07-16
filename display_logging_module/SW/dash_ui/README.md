# Qt Quick Dash UI

`miata_dash` is a separate Qt Quick process. Restarting or changing it does not
interrupt acquisition and MDF logging in `vehicle_loggerd`.

During development, pass `--replay-control-name <name>` alongside IPC data to
show play/pause, scrub, and speed controls on the diagnostics page. The logger
must be running a `--no-log` replay with the same control name; the controls are
never present for live acquisition.

## Open in Qt Design Studio

Open [`MiataDash.qmlproject`](MiataDash.qmlproject). This project metadata is
only for Design Studio's QML and asset views; the production application is
built by the repository CMake project.

The current diagnostics page is intentionally code-oriented because its
filtering, reusable delegates, selection, and scrolling behavior are dynamic.
Future graphical pages should normally separate visually editable
`PageNameForm.ui.qml` files from `PageName.qml` data bindings and behavior.

Use standard Qt Quick types in production pages. Place artwork in `assets/`
and add each asset to the `RESOURCES` section of `dash_ui/CMakeLists.txt`.

## Build and Run

From `display_logging_module/SW`:

```text
cmake --build build --target miata_dash --parallel
build/dash_ui/miata_dash
```

The Windows MinGW build used in this repository produces:

```text
build-mingw/dash_ui/miata_dash.exe
```

Pass `--fullscreen` for the target-display presentation. Windowed mode is the
default for desktop development.

Pass `--demo-warning` to present a simulated critical warning at startup. This
exercises the modal warning and acknowledgement flow without vehicle data.

On Windows the default data source is `fake`; on Raspberry Pi/Linux it is
`ipc`. Override it on either platform with `--data-source fake` or
`--data-source ipc`. `--ipc-name <name>` selects a non-default local server.
Use `--dash-config <path>` to replace the embedded presentation configuration.

## Operator input

The normal driving UI uses four abstract actions. WCM CAN input and keyboard
input both enter the same `DashInputController`, so pages do not depend on a
specific physical input device.

| WCM button | Keyboard | Normal operation | Menu open |
|---|---|---|---|
| Up | Up arrow | Previous page | Previous item |
| Down | Down arrow | Next page | Next item |
| Ack/Enter | Enter | Acknowledge warning | Select item |
| Menu | M or Escape | Open menu | Close menu |

`DashInputController` consumes atomic `WCM.event_*` triples for low-latency
edges and uses `WCM.inputs` as authoritative state and missed-event recovery.
The first status message establishes a baseline without creating button
actions. Event and status transitions update one shared pressed-state table, so
receiving both for one edge cannot generate duplicate actions. Buttons 4-7 are
not routed to dash actions because they are consumed by the PDM.
Multiple event frames in one IPC batch are grouped by acquisition timestamp and
processed in order, preserving quick press/release pairs.

Keyboard/mouse controls remain available for advanced diagnostics. Key events
not in the four-action set continue to normal QML controls, including the
diagnostics filter field.

`WarningManager` owns active and acknowledged warning state. An unacknowledged
warning is the highest-priority interaction layer: Up/Down cycle pending
warnings, Ack acknowledges the current warning, and Menu cannot bypass it.
Acknowledgement removes the modal presentation but does not clear the active
fault. A smaller active-warning indicator remains until its source clears it.

## Reusable gauges and thresholds

`AnalogGauge` and `DigitalGauge` require only a canonical `signalName` plus the
application's signal and presentation providers. They obtain the live value,
unit, freshness, display label, precision, scale, and thresholds automatically.
The current Drive page demonstrates RPM and coolant analog gauges plus battery,
AFR, and fuel digital gauges.

Gauge hash colors and `WarningEvaluator` consume the same resolved threshold
objects. Constant and signal-driven limits therefore cannot disagree visually
with warning behavior. Dynamic limit changes recolor hashes immediately;
activation delays and hysteresis apply to the measured value crossing the limit,
not to moving the displayed limit. Ordinary value updates do not recreate QML
objects, and static tick elements only rebind when their thresholds change.

Optional per-signal `freshness` rules in `config/dash.json` control both gauge
stale appearance and warning generation. Stale warnings have independent
activation and recovery delays. Acknowledgement silences the current occurrence
without clearing it; after fresh data clears the fault, a later timeout is a
new occurrence and opens the warning overlay again.

The components expose `backgroundSource` and `needleSource` for later artwork.
Without assets, `AnalogGauge` uses a lightweight generated dial and needle for
development. Needle artwork should point upward with its pivot at the bottom
center; `needleLength` and `needleWidth` define its rendered box before the
gauge rotates its parent item around the dial center.

## Backend Contract

`SignalDataSource` is the input boundary. It publishes:

1. A batch of canonical signal definitions.
2. Batches of `SignalSample` updates.
3. Connection state and source errors.

`FakeSignalSource` implements that contract for standalone development. It loads every canonical name
from the embedded `config/logging.json` and generates animated values. The
reconnecting `IpcSignalSource` implements the same contract for live logger
data. Definitions and a latest-value snapshot are sent at connection time, so
restarting the dash does not affect acquisition and does not require waiting
for every slow signal to update.

WCM event fields are intentionally not emitted in periodic fake-data batches;
event signals represent individual CAN frames and must not be replayed as
continuous state.

`SignalListModel` exposes QML roles for canonical name, raw/formatted value,
unit, source, local receive age, stale state, and chart selection. Freshness is
measured from local dash receipt time rather than comparing process-local
monotonic clocks. `SignalFilterModel` supplies case-insensitive name filtering.

The Plot checkboxes select up to four signals for the diagnostics **Live plot**
view. History is kept in bounded in-memory buffers and never changes logger
rates or MDF output. Traces are independently auto-scaled so signals with
different units can be compared by shape; the legend retains each canonical
name, unit, and current value. Available windows are 5, 10, 30, 60, and 120
seconds.
