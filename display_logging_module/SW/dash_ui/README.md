# Qt Quick Dash UI

`miata_dash` is a separate Qt Quick process. Restarting or changing it does not
interrupt acquisition and MDF logging in `vehicle_loggerd`.

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

## Backend Contract

`SignalDataSource` is the input boundary. It publishes:

1. A batch of canonical signal definitions.
2. Batches of `SignalSample` updates.
3. Connection state and source errors.

`FakeSignalSource` implements that contract now. It loads every canonical name
from the embedded `config/logging.json` and generates animated values. The
future logger IPC client will implement the same contract.

`SignalListModel` exposes QML roles for canonical name, raw/formatted value,
unit, source, local receive age, stale state, and chart selection. Freshness is
measured from local dash receipt time rather than comparing process-local
monotonic clocks. `SignalFilterModel` supplies case-insensitive name filtering.

The Plot checkboxes only retain selection today. A later chart page can query
the selected canonical names and subscribe them to bounded time-series buffers.
