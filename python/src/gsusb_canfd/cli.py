"""Command line interface for gsusb-canfd."""

from __future__ import annotations

import argparse
import sys
import time
from typing import List, Optional

from .bus import BusConfig, CanFdBus, DeviceSelector, scan_adapters
from .protocol import CanFdError, CanFrame


def _parse_id(text: str) -> int:
    return int(text, 16)


def _parse_data(text: str) -> bytes:
    cleaned = (
        text.replace("0x", "")
        .replace(" ", "")
        .replace(",", "")
        .replace(":", "")
        .replace("-", "")
    )
    if not cleaned:
        return b""
    if len(cleaned) % 2:
        raise argparse.ArgumentTypeError("data must contain an even number of hex digits")
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid hex data: {text}") from exc


def _add_device_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--vid", type=lambda v: int(v, 0), default=0,
                        help="USB vendor id (default: 0 = any gs_usb adapter)")
    parser.add_argument("--pid", type=lambda v: int(v, 0), default=0,
                        help="USB product id (default: 0 = any gs_usb adapter)")
    parser.add_argument("--index", type=int, default=0, help="device index")
    parser.add_argument("--channel", type=int, default=0,
                        help="CAN channel on the device (default: 0)")
    parser.add_argument("--serial", default=None, help="select by serial number")
    parser.add_argument("--product", default=None, help="select by product string substring")


def _add_timing_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--bitrate", type=int, default=1_000_000,
                        help="nominal bitrate in bit/s (default: 1000000)")
    parser.add_argument("--sample-point", type=float, default=0.80,
                        help="nominal sample point (default: 0.80)")
    parser.add_argument("--data-bitrate", type=int, default=5_000_000,
                        help="CAN FD data bitrate (default: 5000000)")
    parser.add_argument("--data-sample-point", type=float, default=0.75,
                        help="CAN FD data sample point (default: 0.75)")
    parser.add_argument("--classic", action="store_true", help="use classic CAN only")
    parser.add_argument("--listen-only", action="store_true", help="do not ACK/transmit")
    parser.add_argument("--no-timestamp", action="store_true", help="disable hardware timestamps")


def _config_from_args(args) -> BusConfig:
    return BusConfig(
        bitrate=args.bitrate,
        sample_point=args.sample_point,
        data_bitrate=args.data_bitrate,
        data_sample_point=args.data_sample_point,
        fd=not args.classic,
        listen_only=args.listen_only,
        hw_timestamp=not args.no_timestamp,
    )


def _open(args) -> CanFdBus:
    bus = CanFdBus(
        DeviceSelector(
            vid=args.vid, pid=args.pid, index=args.index, channel=args.channel,
            serial=args.serial, product=args.product,
        )
    )
    bus.open()
    bus.configure(_config_from_args(args))
    return bus


def _print_bus_info(bus: CanFdBus) -> None:
    print(f"opened gs_usb device (in=0x{bus.ep_in:02X}, out=0x{bus.ep_out:02X})")
    print(f"fclk_can={bus.fclk_can} Hz, feature=0x{bus.feature:08X}, "
          f"fd={bus.is_fd}, hw_timestamp={bus.hw_timestamp}")
    if bus.nominal_timing:
        nt = bus.nominal_timing
        print(f"nominal timing: brp={nt.brp} tseg1={nt.tseg1} tseg2={nt.phase_seg2} sjw={nt.sjw}")
    if bus.is_fd and bus.data_timing:
        dt = bus.data_timing
        print(f"data    timing: brp={dt.brp} tseg1={dt.tseg1} tseg2={dt.phase_seg2} sjw={dt.sjw}")


def cmd_list(_args) -> int:
    adapters = scan_adapters(0, 0)
    if not adapters:
        print("no gs_usb compatible devices found")
        return 1
    for index, adapter in enumerate(adapters):
        print(f"[{index}] {adapter.name()}")
        print(f"     vid:pid={adapter.vendor_id:04X}:{adapter.product_id:04X} "
              f"bus={adapter.bus} addr={adapter.address} "
              f"serial={adapter.serial or '-'}")
    return 0


def cmd_send(args) -> int:
    bus = _open(args)
    try:
        _print_bus_info(bus)
        bus.send(CanFrame(id=args.can_id, data=args.data,
                          extended=args.can_id > 0x7FF,
                          fd=not args.classic, brs=not args.classic))
        payload = " ".join(f"{byte:02X}" for byte in args.data)
        print(f"sent {args.can_id:03X} [{len(args.data)}] {payload}")
    finally:
        bus.close()
    return 0


def cmd_monitor(args) -> int:
    bus = _open(args)
    try:
        _print_bus_info(bus)
        timeout = 1.0 / args.rate
        printed = 0
        last_trigger = 0.0
        if args.trigger:
            bus.send(CanFrame(id=args.trigger_id, data=args.trigger_data,
                              extended=args.trigger_id > 0x7FF,
                              fd=not args.classic, brs=not args.classic))
            print(f"trigger: sent {args.trigger_id:03X}")
            last_trigger = time.time()

        while True:
            if args.trigger and args.repeat and time.time() - last_trigger >= args.repeat:
                bus.send(CanFrame(id=args.trigger_id, data=args.trigger_data,
                                  extended=args.trigger_id > 0x7FF,
                                  fd=not args.classic, brs=not args.classic))
                last_trigger = time.time()

            frame = bus.receive(timeout=timeout)
            if frame is None:
                continue
            if frame.echo and not args.show_echo:
                continue
            if args.filter and frame.id not in args.filter:
                continue
            print(frame)
            printed += 1
            if args.count and printed >= args.count:
                break
    except KeyboardInterrupt:
        print()
    finally:
        bus.close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gsusb-canfd",
        description="Userspace gs_usb CAN FD tool (Python implementation)",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="list gs_usb compatible devices")
    p_list.set_defaults(func=cmd_list)

    p_send = sub.add_parser("send", help="send a single frame")
    _add_device_args(p_send)
    _add_timing_args(p_send)
    p_send.add_argument("can_id", type=_parse_id, help="arbitration id, e.g. 501")
    p_send.add_argument("data", type=_parse_data, nargs="?", default=b"", help="hex payload")
    p_send.set_defaults(func=cmd_send)

    p_mon = sub.add_parser("monitor", help="monitor the bus")
    _add_device_args(p_mon)
    _add_timing_args(p_mon)
    p_mon.add_argument("--trigger", action="store_true", help="send the trigger frame first")
    p_mon.add_argument("--trigger-id", type=_parse_id, default=0x501)
    p_mon.add_argument("--trigger-data", type=_parse_data, default=_parse_data("00025001"))
    p_mon.add_argument("--repeat", type=float, default=0.0, help="re-send trigger every N seconds")
    p_mon.add_argument("--filter", type=_parse_id, nargs="*", default=None,
                       help="only show these ids")
    p_mon.add_argument("--show-echo", action="store_true", help="also show TX echoes")
    p_mon.add_argument("--rate", type=float, default=200.0, help="receive rate in Hz")
    p_mon.add_argument("--count", type=int, default=0, help="stop after N frames")
    p_mon.set_defaults(func=cmd_monitor)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except CanFdError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
