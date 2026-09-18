# gsusb-canfd

Cross-platform, userspace **gs_usb** driver with full **CAN FD** support, written in C++17.

It talks to candleLight-compatible USB-CAN adapters directly over `libusb` — no kernel
driver, no Zadig on macOS, no `sudo`. It also handles adapters that differ from the
candleLight reference in two ways:

* vendor bulk endpoints (`IN = 0x81`, `OUT = 0x01` instead of `0x81`/`0x02`), and
* CAN FD data-phase timing configured through `GS_USB_BREQ_BT_CONST_EXT` (request 11) and
  `GS_USB_BREQ_DATA_BITTIMING` (request 10).

This is the C++ counterpart of the Python `canfd-mac` project and speaks the same wire
protocol as the Linux `gs_usb` kernel driver.

## Features

* CAN and CAN FD, ISO and non-ISO, standard and extended IDs, RTR
* Arbitrary bitrates derived from the adapter's clock and sample-point target
* Blocking `receive()` **and** background-thread callback mode
* Hardware timestamps, listen-only, loopback, one-shot
* Small C++17 API and an optional C ABI (`canfd_c.h`) for easy integration
* `canfd` CLI (`list` / `send` / `monitor`) and examples

## Requirements

* CMake >= 3.16 and a C++17 compiler
* libusb-1.0

```bash
# macOS
brew install libusb

# Debian/Ubuntu
sudo apt install libusb-1.0-0-dev
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Use from C++

```cpp
#include "canfd/canfd.hpp"

canfd::CanFdBus bus;                 // defaults to 0xA8FA:0x8598, device 0
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

For projects that currently link a vendor DLL with a `CanFD_Msg`-style interface, use
`include/canfd/canfd_c.h`:

```c
CanFdHandle* bus = canfd_open(0);
canfd_configure(bus, 1000000, 0.80, 5000000, 0.75, 1);

CanFdMsg tx = {0};
tx.id = 0x501;
tx.flags = CANFD_FLAG_FD | CANFD_FLAG_BRS;
tx.size = 4;
tx.data[3] = 0x01;
canfd_transmit(bus, &tx);

CanFdMsg rx;
if (canfd_receive(bus, &rx, 500) == CANFD_OK) { /* ... */ }
canfd_close(bus);
```

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

## Notes

* `receive()` returns every frame the adapter sees, including the adapter's own TX echo.
  Filter with `CanFrame::echo` and/or the ID.
* Hardware timestamps are used when the device advertises
  `GS_CAN_FEATURE_HW_TIMESTAMP`; RX buffers are then 80 bytes.
* `CanFdBus` owns an exclusive USB handle and is not meant to be shared across threads
  for concurrent `send`/`receive`; the callback mode runs on one internal thread.

## License

MIT
