# Display and Logging Module

## Overview

The display and logging module is a high-performance instrumentation and data
logging system for a street/track car. It operates the main LCD dashboard,
records vehicle and navigation data, and will eventually support features such
as GNSS lap timing.

The logger backend starts before and operates independently of the dash UI. A
UI failure must not interrupt data acquisition or logging. This module does not
make vehicle-control decisions or directly command vehicle loads.

## Hardware

- Raspberry Pi 5 with 8 GB RAM
- Raspberry Pi OS Trixie Lite
- CAN adapter supported by Linux SocketCAN
- FTDI USB-to-RS-232 cable connected to a VectorNav VN300 INS
- 1920 x 720 LCD connected over HDMI

The specific CAN adapter, its Linux driver, and the VN300 serial configuration
still need to be selected and documented.

## Software Direction

- C++20 and Qt 6
- Qt Serial Bus for SocketCAN access and DBC-backed CAN decoding
- Qt Serial Port for VN300 acquisition
- Qt Quick/QML for modular, reusable dash screens backed by C++ models
- Fake and replay data sources for development on Windows
- Runtime loading of the vehicle-wide `../shared/miata.dbc`
- Candump-compatible CAN capture and deterministic replay
- DBC-driven ECM simulation over Qt VirtualCAN for hardware-free development

Canonical signal names use `Sender.signal`, for example `ECM.rpm`. CAN message
names are intentionally excluded, and DBC signal names must be unique within a
sender namespace.

See [`SW/architecture.md`](SW/architecture.md) for the initial architecture and
[`SW/README.md`](SW/README.md) for build, replay, and simulator instructions.
