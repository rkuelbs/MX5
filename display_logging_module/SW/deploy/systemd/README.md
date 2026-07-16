# Raspberry Pi service and shutdown integration

These service templates establish the intended process order:

1. `vehicle-loggerd` starts and begins acquisition/logging.
2. `miata-dash` starts after the logger.
3. `miata-service-web` starts independently after the instrumentation processes.
4. A dash or web crash does not interrupt logging.
5. During system shutdown, systemd stops the dash before the logger because the
   dash declares `After=vehicle-loggerd.service`.
6. `SIGTERM` gives the logger up to 30 seconds to drain its bounded queue and
   finalize the MDF file.
7. Each process reports systemd readiness only after it is genuinely usable:
   the logger after acquisition setup, and the dash after its first rendered
   frame.

The paths assume an application installed under `/opt/miata`, configuration in
`/etc/miata`, logs under `/var/lib/miata/logs`, and a dedicated unprivileged
`miata` user. Adjust packaging if those locations change.

## Installation outline

```text
sudo install -d -o miata -g miata /var/lib/miata/logs
sudo install -d -o root -g miata -m 0770 /etc/miata
sudo install -m 0644 display-logger.env.example /etc/miata/display-logger.env
sudo install -m 0644 ../../../../shared/miata.dbc /etc/miata/miata.dbc
sudo install -m 0644 ../../config/logging.json ../../config/dash.json /etc/miata/
sudo install -m 0644 vehicle-loggerd.service miata-dash.service miata-service-web.service /etc/systemd/system/
sudo visudo -cf miata-service-web.sudoers
sudo install -o root -g root -m 0440 miata-service-web.sudoers /etc/sudoers.d/miata-service-web
sudo systemctl daemon-reload
sudo systemctl enable vehicle-loggerd.service miata-dash.service miata-service-web.service
```

Replace `VN300_PORT` with its stable `/dev/serial/by-id/...` path. Configure
`can0` separately through the Raspberry Pi OS networking mechanism and verify
the selected bitrate before enabling these units.

Neither vehicle service depends on `network-online.target`, NetworkManager, or
Wi-Fi. SSH and Raspberry Pi Connect may become available later without delaying
logging or the first dash frame. The CAN interface is not ordinary IP
networking: it must still exist before `vehicle_loggerd` can become ready. Until
a dedicated CAN setup unit is defined for the selected adapter, systemd will
restart the logger if it starts before `can0` exists.

Editable configuration and the DBC live in `/etc/miata`; executables remain in
`/opt/miata`. Validate replacement files before restarting their service.
The logger creates `/run/miata/vehicle-data-v1` as the explicit shared local
socket. Using `/run/miata` avoids each unit's private `/tmp` namespace while
retaining `PrivateTmp=true` hardening.

## Service web interface

The embedded page is available at `http://<pi-address>/` with the example
environment file. It displays logger IPC state, current signals, data age, free
log storage, active configuration hashes, and the result of the last service
action. It can restart the dash, restart the logger, or reboot the Pi. These
actions affect instrumentation availability only; the service has no CAN
transmit or vehicle-output interface.

The Pi unit explicitly enables two-phase configuration updates. An uploaded
DBC, logging JSON, or dash JSON is first staged in `/etc/miata`, then the full
three-file set is validated with the runtime parsers. Activation requires the
returned token, revalidates the set, writes a `<filename>.previous` backup, and
atomically replaces the selected file. Restart the dash after changing
`dash.json`; restart the logger after changing the DBC or `logging.json`.
The `root:miata` ownership and 0770 directory mode above are required because
atomic replacement creates and renames a temporary file beside the target.

The web process runs as the unprivileged `miata` account. Its ordinary process
receives only the ambient capability needed to bind port 80, and the sudoers
fragment allows three exact `systemctl` commands. It does not accept arbitrary
command names or arguments. The unit does not apply a capability bounding set,
because the separately invoked setuid `sudo` helper needs to establish root
identity before executing those allowlisted commands.
The unit intentionally does not set `NoNewPrivileges=true`, because that would
prevent the narrowly allowlisted `sudo` operations from gaining root identity.

This first version uses plain HTTP and has no authentication. Anyone who can
reach the port can replace a valid instrumentation configuration, interrupt
the dash/logger, or reboot the Pi. Use it only on a
trusted diagnostic Ethernet/Wi-Fi network, or set `SERVICE_WEB_LISTEN_ADDRESS`
to a specific service-interface address. Add authentication and TLS before
exposing it to an untrusted or generally shared network.

The web process merely observes logger IPC. If it is unavailable, the logger
and dash continue normally; if the logger is unavailable, the page remains up
and reports the disconnection. Software upload is not part of this version. It
should later use signature validation, staging, rollback, and atomic activation
rather than writing live executables in place.

## PDM hardwired shutdown handshake

Use Raspberry Pi kernel overlays rather than making the Qt UI responsible for
power control:

- `gpio-shutdown` receives the protected PDM `rpi_shutdown_req` signal and asks
  Linux to shut down cleanly.
- `gpio-poweroff` drives the protected `rpi_halt`/power-off-ready signal only
  after Linux reaches the halted state.
- The PDM keeps Pi power present until power-off-ready is asserted or its
  documented shutdown timeout expires.

GPIO numbers, active polarity, pulls, voltage translation, and pulse/level
behavior are intentionally not specified here. Finalize them from the PDM and
Pi schematic, then add reviewed `dtoverlay=gpio-shutdown,...` and
`dtoverlay=gpio-poweroff,...` entries to the Pi boot configuration. Verify that
boot and shutdown levels cannot falsely indicate ready before deploying them.

This GPIO path is independent of CAN. A future PDM CAN shutdown-status message
may improve the UI and logs, but it must not be the only shutdown request or
power-off-ready mechanism.

## Readiness, watchdog, and boot timing

All three binaries implement the systemd notification protocol directly. The units
use `Type=notify` and a five-second `WatchdogSec`; each Qt event loop sends its
heartbeat at half the requested interval. A blocked event loop is therefore
restarted in addition to ordinary crash recovery.

The journal contains machine-readable milestone labels:

```text
BOOT_MILESTONE logger_process_started_ms
BOOT_MILESTONE logger_ready_ms
BOOT_MILESTONE dash_process_started_ms
BOOT_MILESTONE dash_first_frame_ms
```

The values are milliseconds since the corresponding process began. Combine
them with systemd's monotonic journal timestamps to measure the entire boot:

```text
systemd-analyze time
systemd-analyze critical-chain miata-dash.service
journalctl -b -o short-monotonic -u vehicle-loggerd -u miata-dash -u miata-service-web
```
