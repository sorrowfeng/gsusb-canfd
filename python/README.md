# gsusb-canfd (Python)

Python implementation of the `gsusb-canfd` library: a userspace **gs_usb**
CAN FD driver built directly on `pyusb`/`libusb`. It speaks the same wire
protocol as the C++ library in this repository, so the two are interchangeable.

It does **not** depend on the `gs_usb` / `python-can` packages. Those assume the
candleLight reference endpoints (`OUT = 0x02`) and only configure classic CAN
timing, so they cannot talk to adapters that use different bulk endpoints or
CAN FD data-phase timing through `GS_USB_BREQ_BT_CONST_EXT` / `DATA_BITTIMING`.
This package handles both.

## Install

```bash
pip install ./python
```

Requires `libusb-1.0` at runtime (`brew install libusb`, `apt install libusb-1.0-0-dev`).

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
```

## Tests

The protocol logic (bit timing, DLC mapping, frame codec) is pure Python and is
covered by unit tests that need no hardware:

```bash
pip install -e './python[dev]'
pytest python/tests
```
