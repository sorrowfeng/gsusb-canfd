"""Minimal end-to-end example: trigger a device on 0x501 and print 0x481.

Install the package first (``pip install ./python``) then run:

    python examples/trigger_0x501.py
"""

import time

from gsusb_canfd import BusConfig, CanFdBus, CanFrame

BITRATE = 1_000_000
SAMPLE_POINT = 0.80
DATA_BITRATE = 5_000_000
DATA_SAMPLE_POINT = 0.75

TRIGGER_ID = 0x501
TRIGGER_DATA = bytes([0x00, 0x02, 0x50, 0x01])
RESPONSE_ID = 0x481


def main() -> None:
    bus = CanFdBus()
    with bus:
        bus.configure(
            BusConfig(
                bitrate=BITRATE,
                sample_point=SAMPLE_POINT,
                data_bitrate=DATA_BITRATE,
                data_sample_point=DATA_SAMPLE_POINT,
                fd=True,
            )
        )
        print("bus up: 1M/80% + 5M/75% CAN FD")

        trigger = CanFrame(id=TRIGGER_ID, data=TRIGGER_DATA, fd=True, brs=True)
        bus.send(trigger)
        print(f"trigger sent: {TRIGGER_ID:03X} {' '.join(f'{b:02X}' for b in TRIGGER_DATA)}")

        last_repeat = time.time()
        last_data = None
        frames = 0
        print_at = time.time() + 2.0
        while True:
            if time.time() - last_repeat >= 1.0:
                bus.send(trigger)
                last_repeat = time.time()

            frame = bus.receive(timeout=0.2)
            if frame is None or frame.echo or frame.id != RESPONSE_ID:
                continue

            frames += 1
            if frame.data != last_data:
                last_data = frame.data
                print(frame)

            if time.time() >= print_at:
                print(f"--- {frames} frames on 0x{RESPONSE_ID:03X} ---")
                frames = 0
                print_at = time.time() + 2.0


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nbye")
