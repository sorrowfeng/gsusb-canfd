# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.3] - 2026-09-20

### Added

- CMake install/export support: `cmake --install` produces a relocatable
  package and downstream projects can use `find_package(gsusb-canfd)` +
  `canfd::canfd`. The generated `version.hpp` is installed alongside the public
  headers, and the package config recreates the libusb dependency.
- `BusConfig::drop_echo` (C++/Python) and `CanFdBusConfig::drop_echo` (C ABI):
  drop echo-tagged loopbacks from the receive path. Defaults to `false`; pair it
  with `send(frame, echo=true)` to keep your own TX out of the RX stream.
- `AdapterInfo::displayName()` / `uniqueName()` (and the Python equivalents,
  plus `display_name`/`unique_name` in the C ABI) for a device label that
  includes the USB ids and, for `uniqueName()`, the serial number.
- `CanFdBus::open(const AdapterInfo&)` / `CanFdBus.open(adapter)` to reopen the
  exact adapter returned by a scan (serial first, USB bus/address otherwise);
  `DeviceSelector` gained optional `bus`/`address` and its `index` semantics are
  now documented.
- Typed errors: `canfd::NotFoundError`, `TimeoutError`, `BusError`,
  `ArgumentError` (all deriving from `CanFdError`) with `CanFdError::code()`
  returning `canfd::ErrorCode`. Python mirrors them, and the C ABI gained
  `canfd_last_error_code()` with `CANFD_ERRC_*` constants.

### Changed

- `CanFrame::timestamp` (C++) and `CanFrame.timestamp` (Python) are now always
  meaningful: when the adapter does not provide a usable hardware timestamp
  (not requested, absent from the frame, or left at zero by the firmware) it
  falls back to the host monotonic clock in seconds instead of staying at 0 or
  `None`.
- `add_subdirectory(gsusb-canfd)` no longer pollutes the parent project: the
  `CANFD_BUILD_TESTS`/`CANFD_BUILD_TOOLS`/`CANFD_BUILD_EXAMPLES` options default
  to ON only when this is the top-level project, tests are only registered at
  the top level, and the library type now follows the new `CANFD_BUILD_SHARED`
  option (defaulting to `BUILD_SHARED_LIBS`) instead of reading
  `BUILD_SHARED_LIBS` directly. Standalone builds are unchanged.

### Fixed

- Python: a missing USB backend (pyusb's `NoBackendError`, e.g. libusb-1.0 not
  installed) is now reported as a `BusError` instead of leaking pyusb's
  exception out of `scan_adapters()` / `open()`.
- CMake: `-DLIBUSB_ROOT=<dir>` now works with the official libusb Windows
  release archive, which nests the header as `include/libusb/libusb.h` and ships
  import libraries under `MS64/static`, `MinGW64/static`, ... Previously the
  search suffixes only covered `include/libusb-1.0` and `lib/`, so following the
  library's own error message failed at configure time on Windows.

## [0.1.2] - 2026-09-20

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
- `CanFdBus::send(frame, echo)` and its C ABI counterpart
  `canfd_send_echo(handle, frame, echo)`: the two entry points that ask the
  adapter to echo a transmitted frame back marked. `send(frame)` / `canfd_send()`
  are unchanged and still send untagged, so a loopback stays indistinguishable
  from received traffic unless the caller opts in.
- Echo round-trip checks in `tests/test_frame.cpp` and `tests/test_c_api.cpp`, and
  `python/tests/test_cli_echo.py`, which drives `monitor` against a fake bus and
  needs no hardware. The CLI ones fail against the previous wiring.

### Changed

- The three "show echoes" switches now tag what they transmit:
  `canfd monitor --show-echo`, `gsusb-canfd monitor --show-echo`, and
  `canfd_term`'s `echo on` / `--echo`. Every send used to write `kEchoNone`, so no
  frame could ever carry the marker — which made those switches, the
  `if (frame.echo) continue;` guards and the `ec` output path all unreachable.
  Echo frames are now labelled `ec`, so they stay distinguishable from the `RX`
  lines of the C++ tools and from the unmarked ordinary lines of the Python CLI.

### Documentation

- Python on Windows: the directory holding `libusb-1.0.dll` has to be on `PATH`,
  because `pyusb` resolves the DLL through `ctypes.util.find_library()`, which
  searches `PATH` only — a copy next to `python.exe` is not picked up. Importing
  the package is now documented as working with no DLL present at all.
- Corrected the note on TX echo. A loopback is only marked as one when the sender
  asked for a tag; the default send path writes `kEchoNone` (`0xFFFFFFFF`), so a
  frame from your own adapter arrives looking like ordinary received traffic and
  has to be filtered by ID. `send(..., echo=True)` (Python), `send(frame, true)`
  (C++) and `canfd_send_echo()` (C ABI) are how you opt in.
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
