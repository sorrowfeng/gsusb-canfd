# gsusb-canfd (Python)

Part of the [gsusb-canfd](../README.md) project. Python implementation of the
library: a userspace **gs_usb** CAN FD driver built directly on `pyusb`/`libusb`.
It speaks the same wire protocol as the C++ library in this repository, so the two
are interchangeable.

It does **not** depend on the `gs_usb` / `python-can` packages. Those assume the
candleLight reference endpoints (`OUT = 0x02`) and only configure classic CAN
timing, so they cannot talk to adapters that use different bulk endpoints or
CAN FD data-phase timing through `GS_USB_BREQ_BT_CONST_EXT` / `DATA_BITTIMING`.
This package handles both.

## Install

```bash
pip install gsusb-canfd        # from PyPI
# or, to work on the package itself:
pip install ./python
```

The distribution is named `gsusb-canfd`; the import name is `gsusb_canfd`.

Requires `libusb-1.0` at runtime (`brew install libusb`, `apt install libusb-1.0-0-dev`).
Tested on Windows x64, macOS (Apple silicon) and Linux (x86_64 and aarch64); the
wheel is pure Python, so other platforms work too as long as libusb is present.
On Windows there is no system copy to find: put the directory holding `libusb-1.0.dll`
on `PATH`. `pyusb` looks the DLL up through `ctypes.util.find_library()`, which
searches `PATH` only, so a copy sitting next to `python.exe` is not found. Importing
the package works with no DLL present — libusb is resolved lazily, when a device is
opened.

## Use

```python
from gsusb_canfd import BusConfig, CanFdBus, CanFrame

bus = CanFdBus()
with bus:
    bus.configure(BusConfig(bitrate=1_000_000, sample_point=0.80,
                            data_bitrate=5_000_000, data_sample_point=0.75,
                            fd=True))
    bus.send(CanFrame(id=0x501, data=bytes([0x00, 0x02, 0x50, 0x01]),
                      fd=True, brs=True))
    while True:
        frame = bus.receive(timeout=0.5)
        if frame and not frame.echo and frame.id == 0x481:
            print(frame)
```

A frame this adapter sent comes back as a loopback, but it only carries the marker
when you asked for it: `send(frame, echo=True)` makes it arrive with
`frame.echo is True`, while the default (`echo=False`) leaves it indistinguishable
from received traffic. Unless you opted in, filter on `frame.id`.

Extended frames: set `id > 0x7FF` or `CanFrame(extended=True)`.

Async receive (callback on a background thread):

```python
bus.start(lambda frame: print(frame))
...
bus.stop()
```

## Devices

```python
from gsusb_canfd import scan_adapters

for i, adapter in enumerate(scan_adapters()):
    print(i, adapter.name(), f"{adapter.vendor_id:04X}:{adapter.product_id:04X}",
          adapter.serial)
```

Select one with a `DeviceSelector` (`vid`, `pid`, `index`, `channel`, `serial`,
`product`). By default any gs_usb adapter is auto-discovered (known USB ids plus
descriptor heuristics); pass explicit `vid`/`pid` to pin one. On devices with
several CAN channels, `channel` selects which one (`bus.channel_count()` reports
the count).

## CLI

```bash
gsusb-canfd list

gsusb-canfd monitor --bitrate 1000000 --sample-point 0.80 \
                    --data-bitrate 5000000 --data-sample-point 0.75 \
                    --trigger --trigger-id 501 --trigger-data 00025001 \
                    --count 20

gsusb-canfd send --classic 123 1122334455667788

gsusb-canfd monitor --trigger --show-echo   # tag our trigger, show its loopback
```

`--show-echo` is what makes a `monitor` run able to see anything of its own: it
tags the trigger frames this tool sends (`send(..., echo=True)`), so the adapter
returns them marked and they are printed alongside the bus traffic. The C++ CLI,
`canfd_term` (`echo on`) and `CanFdBus::send(frame, true)` behave the same way.

## Hardware demo

[`examples/canfd_demo.py`](examples/canfd_demo.py) mirrors the C++ demo: it prints
the adapter capabilities and negotiated timing, sends a trigger on `0x501`, and
reports responses on `0x481` with a periodic frame rate.

```bash
python examples/canfd_demo.py --seconds 6
python examples/canfd_demo.py --monitor --seconds 5
python examples/canfd_demo.py --all --all-frames
```

## Tests

The protocol logic (bit timing, DLC mapping, frame codec) is pure Python and is
covered by unit tests that need no hardware:

```bash
pip install -e './python[dev]'
pytest python/tests
```

## Releasing

See [PUBLISHING.md](PUBLISHING.md); the version lives in
[`src/gsusb_canfd/_version.py`](src/gsusb_canfd/_version.py).
