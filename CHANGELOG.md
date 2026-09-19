# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Python CLI now auto-discovers any gs_usb adapter by default (like the C++
  CLI) and gained a `--channel` option, matching the documented behaviour.

## [0.1.0] - 2026-09-20

### Added

- C++17 library (`canfd::CanFdBus`) for gs_usb CAN and CAN FD adapters over
  libusb, with no kernel driver required.
- Stable C ABI (`include/canfd/canfd.h`) for ctypes/cffi, Rust FFI, C#, ...
- Pure-Python implementation with the same API and protocol
  (`python/`, package `gsusb_canfd`) plus a CLI.
- `canfd` command line tool (`list` / `send` / `monitor`), a hardware demo and an
  interactive terminal.
- Adapter auto-discovery and multi-channel selection.
- CAN and CAN FD (ISO), standard and extended IDs, RTR, BRS, hardware
  timestamps, listen-only, loopback and one-shot modes.
- Unit tests for bit timing, DLC mapping and frame codec in both languages.
- GitHub Actions CI (macOS, Linux, Windows and Python) and a PyPI publishing
  workflow using Trusted Publishing.
