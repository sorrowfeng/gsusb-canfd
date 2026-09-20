# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Python: a bulk read that simply ran out of time is no longer reported as a
  fatal error on Windows. pyusb surfaces an idle bus as errno 10060
  (`WSAETIMEDOUT`, "Operation timed out"), which the previous check --
  `errno in (60, 110)` or a literal `"timeout"` -- did not recognise. The
  consequences were that `receive()` raised `CanFdError` instead of returning
  `None`, the `start()` receive thread stopped at the first idle moment and
  never recovered, and `gsusb-canfd monitor` exited with an error as soon as
  the bus went quiet.
- `scripts/build_windows.sh` passed POSIX paths to CMake's `-S`/`-B`/`--build`
  and to `ctest --test-dir`, so the documented Windows build failed during
  configure. The paths now go through the script's existing `to_windows`
  helper.

### Added

- Regression tests for the timeout handling (`python/tests/test_timeout.py`).
  They need neither hardware nor libusb and fail against the previous
  behaviour.
- The Python CI job now runs on Windows and macOS as well, and asserts that
  importing the package works without libusb installed.

### Documentation

- Python on Windows: the directory holding `libusb-1.0.dll` has to be on `PATH`,
  because `pyusb` resolves the DLL through `ctypes.util.find_library()`, which
  searches `PATH` only — a copy next to `python.exe` is not picked up. Importing
  the package is now documented as working with no DLL present at all.
- Corrected the note on TX echo. A loopback is only marked as one when the sender
  asked for a tag; the default send path writes `kEchoNone` (`0xFFFFFFFF`), so a
  frame from your own adapter arrives looking like ordinary received traffic and
  has to be filtered by ID. Python's `send(..., echo=True)` is the only way to opt
  in — the C++ `send()` takes no tag parameter.
- The hardware demos no longer describe themselves as "echo filtered". They filter
  by rx-id, which is what actually removes their own trigger's loopback; the banner
  and docstrings now say so.

## [0.1.1] - 2026-09-20

### Added

- Python hardware demo (`python/examples/canfd_demo.py`) mirroring the C++ one.

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
