# gsusb-canfd

[![ci](https://github.com/sorrowfeng/gsusb-canfd/actions/workflows/ci.yml/badge.svg)](https://github.com/sorrowfeng/gsusb-canfd/actions/workflows/ci.yml)
[![PyPI](https://img.shields.io/pypi/v/gsusb-canfd.svg)](https://pypi.org/project/gsusb-canfd/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](#)

Cross-platform, userspace **gs_usb** CAN / CAN FD toolkit in **C++17 and Python**.
Both implementations speak the same wire protocol and expose the same API, so you
can pick whichever fits your project — or use both; they are interchangeable.

It talks to candleLight-compatible USB-CAN adapters directly over `libusb` — no kernel
driver, no Zadig on macOS, no `sudo`. It also handles adapters that differ from the
candleLight reference in two ways:

* device-specific bulk endpoints (`IN = 0x81`, `OUT = 0x01` instead of the
  candleLight `0x81`/`0x02`), and
* CAN FD data-phase timing configured through `GS_USB_BREQ_BT_CONST_EXT` (request 11) and
  `GS_USB_BREQ_DATA_BITTIMING` (request 10).

It speaks the same wire protocol as the Linux `gs_usb` kernel driver.

## Features

* Two implementations, same API and protocol: C++17 (`canfd::CanFdBus`) and pure
  Python (`python/`, package `gsusb_canfd`)
* CAN and CAN FD (ISO), standard and extended IDs, RTR
* Arbitrary bitrates derived from the adapter's clock and sample-point target
* Blocking `receive()` **and** background-thread callback mode
* Hardware timestamps, listen-only, loopback, one-shot
* Stable C ABI (`include/canfd/canfd.h`) for ctypes/cffi, Rust FFI, C#, ...
* Adapter auto-discovery and multi-channel selection
* `canfd` (C++) and `gsusb-canfd` (Python) CLIs (`list` / `send` / `monitor`),
  plus a hardware demo and an interactive terminal

## Requirements

* CMake >= 3.16 and a C++17 compiler
* libusb-1.0

libusb is found automatically, in this order: `pkg-config`, an installed CMake
package, then `find_path`/`find_library`. If none of those work, point CMake at
your installation with `-DLIBUSB_ROOT=<dir>` (a directory containing `include/`
and `lib/`) or the standard `CMAKE_PREFIX_PATH`.

```bash
# macOS
brew install libusb

# Debian/Ubuntu
sudo apt install libusb-1.0-0-dev

# Windows: vcpkg ...
vcpkg install libusb:x64-windows
# ... or the official prebuilt package from
# https://github.com/libusb/libusb/releases, extracted and pointed at with
cmake -S . -B build -DLIBUSB_ROOT=C:/path/to/libusb-1.0.30
```

On Windows the adapter has to be bound to the **WinUSB** driver, which is what
libusb talks to. candleLight-compatible adapters normally ship that way already,
so try the tools first and only reach for Zadig if the adapter is missing —
the `A8FA:8598` unit used for testing needed no driver swap.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Windows

`scripts/build_windows.sh` does the whole thing from Git Bash: downloads libusb
into `.build/`, configures MinGW + Ninja, builds, runs the tests and lists the
attached adapters. No admin rights, no PATH setup.

```bash
./scripts/build_windows.sh
HTTP_PROXY_URL=http://127.0.0.1:7890 ./scripts/build_windows.sh   # if the download needs a proxy
```

| variable | purpose | default |
| --- | --- | --- |
| `MINGW_DIR` | MinGW toolchain root | `g++` from `PATH`, else `C:/Qt/Tools/mingw1310_64` |
| `NINJA_DIR` | directory holding `ninja.exe` | `ninja` from `PATH`, else `C:/Qt/Tools/Ninja` |
| `CMAKE_BIN` | `cmake.exe` | `cmake` from `PATH` |
| `SEVENZIP` | `7z.exe` | `C:/Program Files/7-Zip/7z.exe` |
| `LIBUSB_VER` | libusb release to download | `1.0.30` |
| `HTTP_PROXY_URL` | proxy for the download | none |
| `BUILD_DIR` | build directory | `<repo>/build` |

```bash
# or drive CMake yourself
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCANFD_LIBUSB_INCLUDE_DIR=<sdk>/include/libusb-1.0 \
  -DCANFD_LIBUSB_LIBRARY=<sdk>/MinGW64/static/libusb-1.0.dll.a \
  -DCANFD_STATIC_RUNTIME=ON
```

`CANFD_STATIC_RUNTIME=ON` folds `libstdc++`, `libgcc` and `libwinpthread` into the
executables. Without it a MinGW build also needs `libwinpthread-1.dll` on `PATH`,
which defeats the point of shipping a self-contained tool.

`libusb-1.0.dll` must sit next to the executables at runtime (the script copies it
into the build directory for you).

## Use from C++

```cpp
#include "canfd/canfd.hpp"

canfd::CanFdBus bus;                 // auto-discovers any gs_usb adapter
bus.open();

canfd::BusConfig config;
config.bitrate = 1'000'000;
config.sample_point = 0.80;
config.data_bitrate = 5'000'000;
config.data_sample_point = 0.75;
config.fd = true;
bus.configure(config);

canfd::CanFrame frame;
frame.id = 0x501;
frame.fd = true;
frame.brs = true;
frame.size = 4;
frame.data = {0x00, 0x02, 0x50, 0x01};
bus.send(frame);

canfd::CanFrame rx;
while (bus.receive(rx, std::chrono::milliseconds(500))) {
  if (!rx.echo && rx.id == 0x481) {
    std::printf("%s\n", rx.toString().c_str());
  }
}
```

Callback mode instead of polling:

```cpp
bus.start([](const canfd::CanFrame& frame) { /* runs on an internal thread */ });
// ...
bus.stop();
```

## Integrate into an existing CMake project

**Git submodule**

```bash
git submodule add <repo-url> third_party/gsusb-canfd
```

```cmake
add_subdirectory(third_party/gsusb-canfd)
target_link_libraries(your_app PRIVATE canfd::canfd)
```

**FetchContent**

```cmake
include(FetchContent)
FetchContent_Declare(gsusb_canfd GIT_REPOSITORY <repo-url> GIT_TAG main)
FetchContent_MakeAvailable(gsusb_canfd)
target_link_libraries(your_app PRIVATE canfd::canfd)
```

To build only what you need pass `-DCANFD_BUILD_TESTS=OFF -DCANFD_BUILD_TOOLS=OFF -DCANFD_BUILD_EXAMPLES=OFF`.

## C ABI

`include/canfd/canfd.h` exposes a stable, opaque C interface for callers that cannot
use C++ (ctypes/cffi, Rust FFI, C#, ...). Functions never throw: they return
`CANFD_OK` / `CANFD_ERROR`, and `canfd_last_error()` carries the message. Build a
shared library with `-DBUILD_SHARED_LIBS=ON` to load it dynamically.

```c
#include "canfd/canfd.h"

CanFdAdapterInfo adapters[4];
int count = canfd_scan(adapters, 4);

CanFdHandle* bus = canfd_open(0);
CanFdBusConfig config;
canfd_bus_config_default(&config);
config.bitrate = 1000000;
config.data_bitrate = 5000000;
canfd_configure(bus, &config);

CanFdFrame tx = {0};
tx.id = 0x501;
tx.fd = tx.brs = 1;
tx.size = 4;
tx.data[3] = 0x01;
canfd_send(bus, &tx);

CanFdFrame rx;
int rc = canfd_receive(bus, &rx, 500);   /* 1 frame, 0 timeout, -1 error */
canfd_close(bus);
```

Async receive uses `canfd_start(bus, callback, user)` / `canfd_stop(bus)`; the
callback runs on the library's receive thread. `canfd_open(index)` opens the first
discovered adapter; use `canfd_open_channel(vid, pid, index, channel)` to pin
specific ids (0 = any) and a CAN channel. See `tests/test_c_api.cpp`.

## CLI

```bash
canfd list

canfd monitor --bitrate 1000000 --sample-point 0.80 \
              --data-bitrate 5000000 --data-sample-point 0.75 \
              --trigger --trigger-id 501 --trigger-data 00025001 \
              --count 20

canfd send --classic 123 1122334455667788
```

## Hardware demo

`examples/canfd_demo.cpp` prints the adapter capabilities, the negotiated timing,
sends a trigger on `0x501`, and reports responses on `0x481` (deduplicating repeated
payloads and printing a frame rate every 2 s):

```bash
./build/canfd_demo --seconds 6          # trigger 0x501, show 0x481
./build/canfd_demo --all --all-frames   # raw dump of every frame
./build/canfd_demo --monitor --seconds 5
```

Example run on a `Com Equipment / CANFD Analyser` (`A8FA:8598`):

```
=== open ===
  endpoints in=0x81 out=0x01
  fclk=80000000 Hz feature=0x000005BB [listen_only|loopback|one_shot|hw_timestamp|fd|bt_const_ext]
=== configure (CAN FD) ===
  nominal  brp=2 tseg1=31 tseg2=8 sjw=8  ->  1000000 bit/s @ 80.0%
  data     brp=1 tseg1=11 tseg2=4 sjw=4  ->  5000000 bit/s @ 75.0%
=== tx trigger ===
  sent 501 [4] 00 02 50 01
--- 2001 frames in 2.0s (1000 fps) ---
=== summary: 6033 frames ===
  id 481 : 6033
```

## Interactive terminal

`tools/canfd_term.cpp` is a small REPL. It scans for adapters on start, lets you
pick one (prompts only when several are connected), then prints received frames in
real time while you type your own IDs/payloads to transmit.

```bash
./build/canfd_term
./build/canfd_term --classic
./build/canfd_term --index 1            # skip the picker
./build/canfd_term --bitrate 500000 --data-bitrate 2000000
```

```
=== scanning for gs_usb adapters ===
found 1 adapter(s):
  [0] Com Equipment CANFD Analyser  A8FA:8598  bus=1 addr=6  serial=F0802068387F4D4D
only one adapter, selecting [0]
opened device[0] in=0x81 out=0x01 fclk=80000000 Hz feature=0x000005BB

canfd> send 501 00025001          # standard FD frame
canfd> send 123 1122334455667788
canfd> send ext 1ABCDEF DEADBEEF  # extended frame (also: ext:1ABCDEF, or id > 7FF)
canfd> id 7AB                     # set defaults, then a bare "send"
canfd> data 01020304
canfd> send
canfd> scan                       # list adapters again
canfd> open 1                     # switch to another adapter
canfd> filter 481                 # only show these ids (filter off to clear)
canfd> dedup on                   # hide repeated identical payloads
canfd> fd on | brs on | echo on
canfd> wait 3                     # keep receiving without typing
canfd> quit
```

Extended frames are fully supported: IDs above `0x7FF` are sent as extended
automatically, or force it with `send ext <id> ...`. Received extended frames are
shown with an `X` suffix (e.g. `01ABCDEFX`).

## Python

A pure-Python implementation with the same API lives in `python/`. It talks to the
adapter through `pyusb`/`libusb` and speaks the same wire protocol as the C++
library, so either can be used on its own. It does **not** depend on the `gs_usb` /
`python-can` packages, which assume different endpoints and classic-only timing.

```bash
pip install gsusb-canfd        # published on PyPI
```

The distribution is named `gsusb-canfd`; the import name is `gsusb_canfd`:

```python
from gsusb_canfd import BusConfig, CanFdBus, CanFrame

with CanFdBus() as bus:
    bus.configure(BusConfig(bitrate=1_000_000, sample_point=0.80,
                            data_bitrate=5_000_000, data_sample_point=0.75, fd=True))
    bus.send(CanFrame(id=0x501, data=bytes([0x00, 0x02, 0x50, 0x01]), fd=True, brs=True))
    frame = bus.receive(timeout=0.5)
```

To work on the package itself, install the checkout instead:

```bash
pip install ./python
```

```bash
gsusb-canfd list
gsusb-canfd monitor --trigger --trigger-id 501 --trigger-data 00025001
```

A Python version of the hardware demo lives in
[`python/examples/canfd_demo.py`](python/examples/canfd_demo.py) and mirrors
[`examples/canfd_demo.cpp`](examples/canfd_demo.cpp):

```bash
python python/examples/canfd_demo.py --seconds 6
python python/examples/canfd_demo.py --monitor --seconds 5
```

The protocol logic (bit timing, DLC mapping, frame codec) is pure Python and is
covered by hardware-free unit tests. See [`python/README.md`](python/README.md)
for the API and [`python/PUBLISHING.md`](python/PUBLISHING.md) for releasing.

## Repository layout

```
include/canfd/   public C++ headers and the C ABI (canfd.h)
src/             library implementation
tools/           canfd CLI and interactive terminal
examples/        C++ examples
tests/           C++ unit tests
python/          pure-Python package, CLI, tests and publishing docs
scripts/         helper build scripts
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test and submit changes.

## Notes

* Adapters are auto-discovered by default: known gs_usb USB ids plus manufacturer/
  product heuristics (`scanAdapters()`). Pass a `DeviceSelector` with explicit
  `vid`/`pid` to pin a specific adapter.
* Multi-channel devices: select a channel with `DeviceSelector::channel` (C++),
  `DeviceSelector(channel=...)` (Python) or `--channel C` (both CLIs);
  `channelCount()` / `channel_count()` reports how many the device has.
* `receive()` returns every frame the adapter sees, including the adapter's own TX echo.
  Filter with `CanFrame::echo` and/or the ID.
* Hardware timestamps are used when the device advertises
  `GS_CAN_FEATURE_HW_TIMESTAMP`; RX buffers are then 80 bytes.
* `CanFdBus` owns an exclusive USB handle and is not meant to be shared across threads
  for concurrent `send`/`receive`; the callback mode runs on one internal thread.
* Non-ISO CAN FD is a real CAN FD variant, but the standard gs_usb protocol exposes
  only a single FD mode bit (candleLight firmware is ISO-only). Selecting non-ISO
  requires vendor-specific control that this library does not implement.

## License

MIT
