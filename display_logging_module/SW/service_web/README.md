# Service web interface

`miata_service_web` is a small, independent Qt process for servicing the
instrumentation Pi from a browser. It consumes the logger's existing local IPC
stream; it is not in the acquisition, logging, or dash rendering path.

The embedded page provides:

- logger connection state and most-recent data age;
- filterable canonical signal values, units, sources, and local freshness;
- log-volume free space;
- completed/active log sessions with streamed downloads and safe deletion;
- SHA-256 identifiers for the active DBC, logging config, and dash config;
- two-phase validation and atomic activation for those configuration files;
- fixed actions to restart the dash, restart the logger, or reboot the Pi.
- optional development replay status, play/pause, seek, and speed controls.

The HTTP implementation accepts one bounded HTTP/1.1 request per connection.
Headers are limited to 16 KiB, request bodies to 1 MiB, active clients to 16,
and incomplete requests to five seconds. It deliberately has no file browser,
shell, arbitrary process execution, CAN transmission, or vehicle-control
endpoint.

## Run during Windows development

From the repository root after building:

```powershell
.\display_logging_module\SW\build-mingw\service_web\miata_service_web.exe `
  --listen-address 127.0.0.1 `
  --port 8080 `
  --dbc .\shared\miata.dbc `
  --logging-config .\display_logging_module\SW\config\logging.json `
  --dash-config .\display_logging_module\SW\config\dash.json `
  --log-directory .\logs
```

Add `--replay-control-name miata-replay-dev` when the logger was launched with
that same replay-only control name. It is intentionally absent from the Pi
production service configuration.

Open `http://127.0.0.1:8080/`. Start `vehicle_loggerd` with its normal IPC name
to populate live signals. The maintenance buttons are disabled on Windows.

## HTTP endpoints

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Embedded service page |
| `GET` | `/api/status` | Process, logger, storage, config, and action status |
| `GET` | `/api/signals` | Latest received signal values |
| `GET` | `/api/logs` | Session catalog and managed storage totals |
| `GET` | `/api/log-file/<name>` | Stream one MDF4 or raw CAN file |
| `POST` | `/api/log-session/<id>/delete` | Delete one completed session |
| `GET` | `/api/config` | Update availability and staged-candidate status |
| `GET` | `/api/replay` | Optional controlled-replay status |
| `POST` | `/api/replay/play` | Resume or restart controlled replay |
| `POST` | `/api/replay/pause` | Pause controlled replay |
| `POST` | `/api/replay/seek` | Seek using JSON `position_ms` |
| `POST` | `/api/replay/speed` | Set JSON playback `factor` |
| `POST` | `/api/config/<type>/stage` | Upload and validate `dbc`, `logging`, or `dash` |
| `POST` | `/api/config/<type>/activate` | Activate the token-matched candidate atomically |
| `POST` | `/api/action/restart-dash` | Restart the Qt dash service |
| `POST` | `/api/action/restart-logger` | Restart the logger service |
| `POST` | `/api/action/reboot` | Reboot the instrumentation Pi |

Deletion and action requests require `Content-Type: application/json` and
`X-Miata-Service: 1`. This prevents ordinary cross-origin form submission but
is not authentication. The initial Pi deployment is intended only for a
trusted local service network. See [`../deploy/systemd/README.md`](../deploy/systemd/README.md)
for installation and the exact sudo allowlist.

Configuration updates are disabled unless the process is launched with
`--enable-config-updates`; the Windows development launcher intentionally omits
it. DBC candidates are limited to 1 MiB and JSON candidates to 256 KiB. Staging
validates the candidate with both companion files, including DBC canonical
names, logging assignments and storage policy, dash structure, threshold
sources, and warning-enable sources. Activation requires the staged token and
repeats validation so a changed companion file cannot invalidate the original
result. The old target is retained as `<filename>.previous`. Activation never
restarts a process automatically.

Only timestamp-shaped `vehicle_*` names directly inside the configured log
directory are accepted. Symlinks and path traversal are rejected. A session
with a `.active` marker cannot be deleted; this protects both the current log
and an incomplete file left by an abrupt shutdown. Downloads are streamed in
64 KiB chunks with bounded socket buffering rather than loaded into memory.
