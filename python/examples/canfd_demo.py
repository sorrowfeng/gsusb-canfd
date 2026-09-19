"""End-to-end hardware demo for gsusb-canfd (Python), mirroring examples/canfd_demo.cpp.

It scans for an adapter, opens it, prints the negotiated bit timing, transmits a
trigger frame on 0x501 and prints every response it sees (0x481 by default),
skipping the adapter's own TX echo.

Install the package first (``pip install gsusb-canfd`` or ``pip install ./python``)
then run:

    python examples/canfd_demo.py --seconds 6
    python examples/canfd_demo.py --monitor --seconds 5
    python examples/canfd_demo.py --classic --id 123 --rx-id 456 --take-data 11223344
"""

from __future__ import annotations

import argparse
import time
from typing import List, Optional

from gsusb_canfd import BusConfig, CanFdBus, CanFrame, scan_adapters
from gsusb_canfd.protocol import (
    FEATURE_BT_CONST_EXT,
    FEATURE_FD,
    FEATURE_HW_TIMESTAMP,
    FEATURE_LISTEN_ONLY,
    FEATURE_LOOPBACK,
    FEATURE_ONE_SHOT,
    CanFdError,
)

FEATURES = (
    (FEATURE_LISTEN_ONLY, "listen_only"),
    (FEATURE_LOOPBACK, "loopback"),
    (FEATURE_ONE_SHOT, "one_shot"),
    (FEATURE_HW_TIMESTAMP, "hw_timestamp"),
    (FEATURE_FD, "fd"),
    (FEATURE_BT_CONST_EXT, "bt_const_ext"),
)


def feature_string(feature: int) -> str:
    names = [name for bit, name in FEATURES if feature & bit]
    return "|".join(names) if names else "none"


def parse_hex_bytes(text: str) -> bytes:
    cleaned = "".join(ch for ch in text if ch.isalnum())
    return bytes.fromhex(cleaned) if cleaned else b""


def make_frame(identifier: int, data: bytes, fd: bool) -> CanFrame:
    return CanFrame(
        id=identifier,
        data=data,
        extended=identifier > 0x7FF,
        fd=fd,
        brs=fd,
    )


def sample_point(timing) -> float:
    total = 1 + timing.tseg1 + timing.phase_seg2
    return (1 + timing.tseg1) / total


def bitrate(timing, fclk: int) -> float:
    total = 1 + timing.tseg1 + timing.phase_seg2
    return fclk / (timing.brp * total)


def print_timing(label: str, timing, fclk: int) -> None:
    print(f"  {label:<8} brp={timing.brp} tseg1={timing.tseg1} "
          f"tseg2={timing.phase_seg2} sjw={timing.sjw}  ->  "
          f"{bitrate(timing, fclk):.0f} bit/s @ {sample_point(timing) * 100:.1f}%")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="gsusb-canfd hardware demo")
    parser.add_argument("--seconds", type=float, default=6.0, help="how long to receive")
    parser.add_argument("--monitor", action="store_true",
                        help="do not trigger, just listen")
    parser.add_argument("--no-trigger", action="store_true", help="do not send the trigger")
    parser.add_argument("--classic", action="store_true", help="use classic CAN only")
    parser.add_argument("--bitrate", type=int, default=1_000_000)
    parser.add_argument("--sample-point", type=float, default=0.80)
    parser.add_argument("--data-bitrate", type=int, default=5_000_000)
    parser.add_argument("--data-sample-point", type=float, default=0.75)
    parser.add_argument("--id", type=lambda v: int(v, 16), default=0x501,
                        help="trigger arbitration id (hex)")
    parser.add_argument("--take-data", type=parse_hex_bytes, default=b"\x00\x02\x50\x01",
                        help="trigger payload (hex)")
    parser.add_argument("--rx-id", type=lambda v: int(v, 16), default=0x481,
                        help="only show this id (hex)")
    parser.add_argument("--all", action="store_true", help="show every id, not just --rx-id")
    parser.add_argument("--all-frames", action="store_true",
                        help="print repeated payloads too")
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    trigger = not (args.monitor or args.no_trigger)
    fd = not args.classic

    print("=== scan ===")
    for index, adapter in enumerate(scan_adapters(0, 0)):
        print(f"  {adapter.name()} | {adapter.vendor_id:04X}:{adapter.product_id:04X} "
              f"bus={adapter.bus} addr={adapter.address} serial={adapter.serial}")

    bus = CanFdBus()
    try:
        print("=== open ===")
        bus.open()
        info = bus.device_info
        print(f"  endpoints in=0x{bus.ep_in:02X} out=0x{bus.ep_out:02X}")
        if info is not None:
            print(f"  firmware={info.sw_version / 10.0:.1f} "
                  f"hardware={info.hw_version / 10.0:.1f}")
        print(f"  fclk={bus.fclk_can} Hz feature=0x{bus.feature:08X} "
              f"[{feature_string(bus.feature)}]")

        print(f"=== configure ({'classic CAN' if args.classic else 'CAN FD'}) ===")
        bus.configure(BusConfig(
            bitrate=args.bitrate,
            sample_point=args.sample_point,
            data_bitrate=args.data_bitrate,
            data_sample_point=args.data_sample_point,
            fd=fd,
        ))
        print_timing("nominal", bus.nominal_timing, bus.fclk_can)
        if bus.is_fd and bus.data_timing is not None:
            print_timing("data", bus.data_timing, bus.fclk_can)

        if trigger:
            print("=== tx trigger ===")
            bus.send(make_frame(args.id, args.take_data, fd))
            print(f"  sent {args.id:03X} [{len(args.take_data)}] "
                  f"{' '.join(f'{b:02X}' for b in args.take_data)}")
        else:
            print(f"=== passive monitor for {args.seconds:.1f}s ===")

        target = "all ids" if args.all else f"0x{args.rx_id:03X}"
        print(f"=== rx for {args.seconds:.1f}s (echo filtered, showing {target}) ===")
        deadline = time.monotonic() + args.seconds
        last_trigger = time.monotonic()
        next_report = time.monotonic() + 2.0
        window_start = time.monotonic()

        counts = {}
        last_payload: Optional[bytes] = None
        total = 0
        window = 0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if trigger and now - last_trigger >= 1.0:
                bus.send(make_frame(args.id, args.take_data, fd))
                last_trigger = now

            frame = bus.receive(timeout=0.1)
            if frame is None or frame.echo:
                continue
            if not args.all and frame.id != args.rx_id:
                continue

            counts[frame.id] = counts.get(frame.id, 0) + 1
            total += 1
            window += 1

            if args.all_frames or frame.data != last_payload:
                print(f"  {frame}")
                last_payload = frame.data

            if now >= next_report:
                elapsed = now - window_start
                fps = window / elapsed if elapsed > 0 else 0.0
                print(f"--- {window} frames in {elapsed:.1f}s ({fps:.0f} fps) ---")
                window = 0
                window_start = now
                next_report = now + 2.0

        print(f"=== summary: {total} frames ===")
        for identifier, count in sorted(counts.items()):
            print(f"  id {identifier:03X} : {count}")
    except KeyboardInterrupt:
        print("\ninterrupted")
    except CanFdError as exc:
        print(f"error: {exc}")
        bus.close()
        return 1
    finally:
        bus.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
