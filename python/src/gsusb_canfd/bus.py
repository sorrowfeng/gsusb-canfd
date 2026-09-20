"""pyusb transport and the high-level :class:`CanFdBus` API.

The wire protocol (control requests, frame layout, bit timing) lives in
:mod:`gsusb_canfd.protocol`; this module only deals with libusb and the
device lifecycle.
"""

from __future__ import annotations

import struct
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional

import usb.core
import usb.util

from .protocol import (
    BREQ_BITTIMING,
    BREQ_BT_CONST,
    BREQ_BT_CONST_EXT,
    BREQ_DATA_BITTIMING,
    BREQ_DEVICE_CONFIG,
    BREQ_HOST_FORMAT,
    BREQ_MODE,
    ECHO_NONE,
    FEATURE_BT_CONST_EXT,
    FEATURE_FD,
    FEATURE_HW_TIMESTAMP,
    FEATURE_LISTEN_ONLY,
    FEATURE_LOOPBACK,
    FEATURE_ONE_SHOT,
    HEADER_SIZE,
    HOST_FORMAT_MAGIC,
    MODE_FD,
    MODE_HW_TIMESTAMP,
    MODE_LISTEN_ONLY,
    MODE_LOOPBACK,
    MODE_ONE_SHOT,
    MODE_RESET,
    MODE_START,
    BitTiming,
    BitTimingConst,
    CanFdError,
    CanFrame,
    calculate_bit_timing,
    decode_frame,
    encode_frame,
)

DEFAULT_VID = 0xA8FA
DEFAULT_PID = 0x8598

# USB ids the Linux gs_usb driver binds to (plus this project's test adapter).
KNOWN_DEVICES = (
    (0x1D50, 0x606F),  # Geschwister Schneider / candleLight
    (0x1209, 0x2323),  # candleLight
    (0x1CD2, 0x606F),  # CES CANext FD
    (0x16D0, 0x10B8),  # ABE CAN Debugger FD
    (0x1209, 0xCA01),  # Cannectivity
    (0xA8FA, 0x8598),  # Com Equipment CANFD Analyser
)


@dataclass
class AdapterInfo:
    vendor_id: int
    product_id: int
    bus: int
    address: int
    manufacturer: str = ""
    product: str = ""
    serial: str = ""

    def name(self) -> str:
        parts = [part for part in (self.manufacturer, self.product) if part]
        if parts:
            return " ".join(parts)
        return f"gs_usb {self.vendor_id:04X}:{self.product_id:04X}"

    def display_name(self) -> str:
        return f"{self.name()} ({self.vendor_id:04X}:{self.product_id:04X})"

    def unique_name(self) -> str:
        label = self.display_name()
        return f"{label} SN:{self.serial}" if self.serial else label


@dataclass
class DeviceSelector:
    vid: int = 0
    pid: int = 0
    # 0-based position among the adapters matching the filters below, in
    # enumeration order (not a global device number).
    index: int = 0
    channel: int = 0
    serial: Optional[str] = None
    product: Optional[str] = None
    # Exact USB location; only used when no serial is given.
    bus: Optional[int] = None
    address: Optional[int] = None


@dataclass
class DeviceInfo:
    icount: int = 0
    sw_version: int = 0
    hw_version: int = 0
    channel_count: int = 1


@dataclass
class BusConfig:
    bitrate: int = 1_000_000
    sample_point: float = 0.80
    data_bitrate: int = 5_000_000
    data_sample_point: float = 0.75
    fd: bool = True
    listen_only: bool = False
    loopback: bool = False
    one_shot: bool = False
    hw_timestamp: bool = True
    # Drop frames this adapter transmitted back to us (CanFrame.echo) from the
    # receive path. Only echo-tagged sends -- send(frame, echo=True) -- can be
    # recognised, so pair this with those. Defaults to False (no behaviour
    # change); set it True to keep loopbacks out of the normal RX stream.
    drop_echo: bool = False


_TIMEOUT_EXC = tuple(
    exc for exc in (getattr(usb.core, "USBTimeoutError", None),) if exc is not None
)
_TIMEOUT_ERRNOS = frozenset((60, 110, 10060))
"""errno values that mean "no data before the timeout" rather than a real failure.

* 60    - ETIMEDOUT on macOS
* 110   - ETIMEDOUT on Linux
* 10060 - WSAETIMEDOUT on Windows (the one libusb surfaces through pyusb)
"""


def _is_timeout(exc: usb.core.USBError) -> bool:
    """Return True when *exc* means the bulk read simply ran out of time.

    pyusb's exception taxonomy differs by version and platform: Linux and macOS
    raise a plain ``USBError`` carrying errno 60/110, Windows raises one with
    errno 10060 and the message "Operation timed out", and pyusb >= 1.3 has a
    dedicated ``usb.core.USBTimeoutError``. Checking only ``errno in (60, 110)``
    or ``"timeout" in str(exc)`` therefore misses Windows entirely, turning an
    idle bus into a fatal :class:`CanFdError`.
    """
    if _TIMEOUT_EXC and isinstance(exc, _TIMEOUT_EXC):
        return True
    if getattr(exc, "errno", None) in _TIMEOUT_ERRNOS:
        return True
    message = str(exc).lower()
    return "timed out" in message or "timeout" in message


def _get_string(device, index) -> str:
    if not index:
        return ""
    try:
        return usb.util.get_string(device, index) or ""
    except (ValueError, usb.core.USBError):
        return ""


def _bulk_endpoints(interface):
    ep_in = ep_out = None
    for endpoint in interface:
        if usb.util.endpoint_type(endpoint.bmAttributes) != usb.util.ENDPOINT_TYPE_BULK:
            continue
        if usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_IN:
            if ep_in is None:
                ep_in = endpoint.bEndpointAddress
        elif ep_out is None:
            ep_out = endpoint.bEndpointAddress
    return ep_in, ep_out


def _has_vendor_bulk_interface(device) -> bool:
    try:
        for config in device:
            for interface in config:
                if getattr(interface, "bInterfaceClass", None) != 0xFF:
                    continue
                ep_in, ep_out = _bulk_endpoints(interface)
                if ep_in is not None and ep_out is not None:
                    return True
    except Exception:
        return False
    return False


def _looks_like_gs_usb(device) -> bool:
    if (device.idVendor, device.idProduct) in KNOWN_DEVICES:
        return True
    text = f"{_get_string(device, device.iManufacturer)} {_get_string(device, device.iProduct)}".lower()
    if text.strip():
        return any(hint in text for hint in ("can", "candle", "gs_usb", "gs-usb"))
    # Descriptors unavailable (device already claimed, or no strings): fall back
    # to the interface shape so a busy adapter stays discoverable.
    return _has_vendor_bulk_interface(device)


def _matching_devices(
    vid: int,
    pid: int,
    serial: Optional[str],
    product: Optional[str],
    bus: Optional[int] = None,
    address: Optional[int] = None,
) -> List:
    use_heuristic = vid == 0 and pid == 0
    found = []
    for device in usb.core.find(find_all=True):
        if vid and device.idVendor != vid:
            continue
        if pid and device.idProduct != pid:
            continue
        if serial is not None and _get_string(device, device.iSerialNumber) != serial:
            continue
        if product is not None and product.lower() not in _get_string(device, device.iProduct).lower():
            continue
        # bus/address only disambiguate when no serial was given.
        if serial is None:
            if bus is not None and int(device.bus) != bus:
                continue
            if address is not None and int(device.address) != address:
                continue
        if use_heuristic and not _looks_like_gs_usb(device):
            continue
        found.append(device)
    return found


def scan_adapters(vid: int = 0, pid: int = 0) -> List[AdapterInfo]:
    """Return the gs_usb-looking adapters currently attached.

    With ``vid``/``pid`` at 0 the name heuristics are applied; pass explicit
    ids to list any device regardless of its descriptors.
    """
    adapters: List[AdapterInfo] = []
    for device in _matching_devices(vid, pid, None, None):
        adapters.append(
            AdapterInfo(
                vendor_id=device.idVendor,
                product_id=device.idProduct,
                bus=int(device.bus),
                address=int(device.address),
                manufacturer=_get_string(device, device.iManufacturer),
                product=_get_string(device, device.iProduct),
                serial=_get_string(device, device.iSerialNumber),
            )
        )
    return adapters


def _find_interface(device):
    for config in device:
        for interface in config:
            if interface.bInterfaceClass == 0xFF:
                ep_in, ep_out = _bulk_endpoints(interface)
                if ep_in is not None and ep_out is not None:
                    return config, interface, ep_in, ep_out
    try:
        config = device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
        config = device.get_active_configuration()
    for interface in config:
        ep_in, ep_out = _bulk_endpoints(interface)
        if ep_in is not None and ep_out is not None:
            return config, interface, ep_in, ep_out
    raise CanFdError("no bulk endpoints found on device")


class CanFdBus:
    """A single gs_usb CAN (FD) channel."""

    def __init__(self, selector: Optional[DeviceSelector] = None):
        self.selector = selector or DeviceSelector()
        self.channel = int(self.selector.channel)

        self._device = None
        self._interface = None
        self.ep_in: Optional[int] = None
        self.ep_out: Optional[int] = None

        self.feature = 0
        self.fclk_can = 0
        self.nominal_const: Optional[BitTimingConst] = None
        self.data_const: Optional[BitTimingConst] = None
        self.device_info: Optional[DeviceInfo] = None

        self.nominal_timing: Optional[BitTiming] = None
        self.data_timing: Optional[BitTiming] = None
        self.is_fd = False
        self.listen_only = False
        self.hw_timestamp = True
        self.drop_echo = False
        self._started = False

        self._worker: Optional[threading.Thread] = None
        self._running = False
        self._callback: Optional[Callable[[CanFrame], None]] = None

    # ------------------------------------------------------------------ setup

    def _match(self):
        devices = _matching_devices(
            self.selector.vid,
            self.selector.pid,
            self.selector.serial,
            self.selector.product,
            self.selector.bus,
            self.selector.address,
        )
        if not devices:
            raise CanFdError(
                f"no gs_usb device found (vid=0x{self.selector.vid:04X} "
                f"pid=0x{self.selector.pid:04X})"
            )
        if not 0 <= self.selector.index < len(devices):
            raise CanFdError(
                f"device index {self.selector.index} out of range ({len(devices)} found)"
            )
        return devices[self.selector.index]

    def open(self, adapter: Optional[AdapterInfo] = None) -> "CanFdBus":
        if adapter is not None:
            # Open the exact adapter from a previous scan_adapters() call:
            # prefer the (stable) serial, fall back to the USB location.
            self.selector.vid = adapter.vendor_id
            self.selector.pid = adapter.product_id
            self.selector.index = 0
            self.selector.product = None
            if adapter.serial:
                self.selector.serial = adapter.serial
                self.selector.bus = None
                self.selector.address = None
            else:
                self.selector.serial = None
                self.selector.bus = adapter.bus
                self.selector.address = adapter.address

        device = self._match()
        self._device = device
        try:
            if device.is_kernel_driver_active(0):
                device.detach_kernel_driver(0)
        except (NotImplementedError, usb.core.USBError):
            pass

        _config, interface, ep_in, ep_out = _find_interface(device)
        self._interface = interface
        self.ep_in = ep_in
        self.ep_out = ep_out

        try:
            device.ctrl_transfer(
                0x41,
                BREQ_HOST_FORMAT,
                1,
                interface.bInterfaceNumber,
                struct.pack("<I", HOST_FORMAT_MAGIC),
            )
        except usb.core.USBError:
            pass

        self._read_capabilities()
        self._read_device_info()
        return self

    def _read_capabilities(self):
        device = self._device
        try:
            raw = bytes(device.ctrl_transfer(0xC1, BREQ_BT_CONST_EXT, self.channel, 0, 72))
        except usb.core.USBError:
            raw = b""
        if len(raw) >= 72:
            values = struct.unpack("<18I", raw[:72])
            self.feature = values[0]
            self.fclk_can = values[1]
            self.nominal_const = BitTimingConst(*values[2:10])
            self.data_const = BitTimingConst(*values[10:18])
            return

        raw = bytes(device.ctrl_transfer(0xC1, BREQ_BT_CONST, self.channel, 0, 40))
        values = struct.unpack("<10I", raw[:40])
        self.feature = values[0]
        self.fclk_can = values[1]
        self.nominal_const = BitTimingConst(*values[2:10])
        self.data_const = self.nominal_const

    def _read_device_info(self):
        try:
            raw = bytes(self._device.ctrl_transfer(0xC1, BREQ_DEVICE_CONFIG, 1, 0, 12))
            _r1, _r2, _r3, icount, sw_version, hw_version = struct.unpack("<4B2I", raw[:12])
            self.device_info = DeviceInfo(
                icount=icount, sw_version=sw_version, hw_version=hw_version,
                channel_count=icount + 1,
            )
        except (usb.core.USBError, struct.error):
            self.device_info = None

    # -------------------------------------------------------------- configure

    def configure(self, config: Optional[BusConfig] = None) -> "CanFdBus":
        config = config or BusConfig()
        if self._device is None:
            raise CanFdError("call open() before configure()")
        if self.nominal_const is None:
            raise CanFdError("device capabilities not available")

        fd = config.fd and bool(self.feature & FEATURE_FD)
        if config.fd and not (self.feature & FEATURE_FD):
            raise CanFdError("device does not support CAN FD")

        self.nominal_timing = calculate_bit_timing(
            config.bitrate, config.sample_point, self.fclk_can, self.nominal_const
        )
        self._control_out(BREQ_BITTIMING, self.nominal_timing.as_bytes())

        if fd:
            if self.data_const is None:
                raise CanFdError("device does not report CAN FD timing constants")
            self.data_timing = calculate_bit_timing(
                config.data_bitrate, config.data_sample_point, self.fclk_can, self.data_const
            )
            self._control_out(BREQ_DATA_BITTIMING, self.data_timing.as_bytes())

        self.is_fd = fd
        self.listen_only = config.listen_only
        self.hw_timestamp = bool(config.hw_timestamp and (self.feature & FEATURE_HW_TIMESTAMP))
        self.drop_echo = bool(config.drop_echo)

        flags = 0
        if config.listen_only:
            if not (self.feature & FEATURE_LISTEN_ONLY):
                raise CanFdError("device does not support listen-only mode")
            flags |= MODE_LISTEN_ONLY
        if config.loopback:
            if not (self.feature & FEATURE_LOOPBACK):
                raise CanFdError("device does not support loopback mode")
            flags |= MODE_LOOPBACK
        if config.one_shot:
            if not (self.feature & FEATURE_ONE_SHOT):
                raise CanFdError("device does not support one-shot mode")
            flags |= MODE_ONE_SHOT
        if fd:
            flags |= MODE_FD
        if self.hw_timestamp:
            flags |= MODE_HW_TIMESTAMP

        self._control_out(BREQ_MODE, struct.pack("<II", MODE_START, flags))
        self._started = True
        return self

    def _control_out(self, request: int, data: bytes):
        try:
            return self._device.ctrl_transfer(0x41, request, self.channel, 0, data)
        except usb.core.USBError as exc:
            raise CanFdError(f"control request {request} failed: {exc}") from exc

    # ------------------------------------------------------------------- I/O

    @property
    def _rx_size(self) -> int:
        base = HEADER_SIZE + (64 if self.is_fd else 8)
        return base + (4 if self.hw_timestamp else 0)

    def send(self, frame: CanFrame, echo: bool = False) -> None:
        if self._device is None or not self._started:
            raise CanFdError("device is not started")
        if frame.fd and not self.is_fd:
            raise CanFdError("cannot send a CAN FD frame on a classic CAN bus")

        out = CanFrame(
            id=frame.id,
            data=bytes(frame.data),
            extended=frame.extended,
            fd=frame.fd,
            brs=frame.brs and self.is_fd,
            remote=frame.remote,
            channel=self.channel,
        )

        echo_id = 1 if echo else ECHO_NONE
        self._device.write(self.ep_out, encode_frame(out, echo_id))

    def receive(self, timeout: float = 1.0) -> Optional[CanFrame]:
        if self._device is None or not self._started:
            raise CanFdError("device is not started")

        deadline = time.monotonic() + timeout if timeout > 0 else None
        while True:
            if deadline is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                timeout_ms = max(1, int(remaining * 1000))
            else:
                timeout_ms = 0  # block until a frame arrives

            try:
                buffer = self._device.read(self.ep_in, self._rx_size, timeout=timeout_ms)
            except usb.core.USBError as exc:
                if _is_timeout(exc):
                    return None
                raise CanFdError(f"bulk read failed: {exc}") from exc

            frame = decode_frame(bytes(buffer), self.hw_timestamp)
            if self.drop_echo and frame.echo:
                continue  # loopback of our own TX: skip it
            return frame

    # ------------------------------------------------------- async receive

    def start(self, callback: Callable[[CanFrame], None]) -> None:
        if self._device is None or not self._started:
            raise CanFdError("device is not started")
        if self._running:
            raise CanFdError("receive loop is already running")
        self._callback = callback
        self._running = True

        def loop():
            while self._running:
                try:
                    frame = self.receive(timeout=0.1)
                except CanFdError:
                    break
                if frame is not None and self._callback is not None:
                    self._callback(frame)

        self._worker = threading.Thread(target=loop, name="gsusb-canfd-rx", daemon=True)
        self._worker.start()

    def stop(self) -> None:
        self._running = False
        if self._worker is not None:
            self._worker.join(timeout=2.0)
            self._worker = None
        self._callback = None

    # ------------------------------------------------------------- teardown

    def is_open(self) -> bool:
        return self._device is not None

    def is_started(self) -> bool:
        return self._started

    def channel_count(self) -> int:
        return self.device_info.channel_count if self.device_info is not None else 1

    def close(self) -> None:
        self.stop()
        if self._device is not None and self._started:
            try:
                self._control_out(BREQ_MODE, struct.pack("<II", MODE_RESET, 0))
            except CanFdError:
                pass
            self._started = False
        if self._device is not None:
            try:
                usb.util.dispose_resources(self._device)
            except usb.core.USBError:
                pass
            self._device = None

    def __enter__(self) -> "CanFdBus":
        return self.open()

    def __exit__(self, *_exc):
        self.close()
