"""gs_usb wire protocol: constants, bit timing, DLC mapping and frame codec.

This module is pure Python (no ``pyusb`` import) so it can be unit tested
without any USB hardware or libusb present.
"""

from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from typing import Optional

# --------------------------------------------------------------- control requests
BREQ_HOST_FORMAT = 0
BREQ_BITTIMING = 1
BREQ_MODE = 2
BREQ_BT_CONST = 4
BREQ_DEVICE_CONFIG = 5
BREQ_DATA_BITTIMING = 10
BREQ_BT_CONST_EXT = 11

HOST_FORMAT_MAGIC = 0x0000BEEF

# ------------------------------------------------------------------- mode flags
MODE_RESET = 0
MODE_START = 1

MODE_LISTEN_ONLY = 1 << 0
MODE_LOOPBACK = 1 << 1
MODE_ONE_SHOT = 1 << 3
MODE_HW_TIMESTAMP = 1 << 4
MODE_FD = 1 << 8

# ---------------------------------------------------------------- device features
FEATURE_LISTEN_ONLY = 1 << 0
FEATURE_LOOPBACK = 1 << 1
FEATURE_ONE_SHOT = 1 << 3
FEATURE_HW_TIMESTAMP = 1 << 4
FEATURE_FD = 1 << 8
FEATURE_BT_CONST_EXT = 1 << 10

# ----------------------------------------------------------------- frame flags
FLAG_OVERFLOW = 1 << 0
FLAG_FD = 1 << 1
FLAG_BRS = 1 << 2
FLAG_ESI = 1 << 3

CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
CAN_EFF_MASK = 0x1FFFFFFF
CAN_SFF_MASK = 0x000007FF

ECHO_NONE = 0xFFFFFFFF

HEADER_SIZE = 12
FD_DATA_SIZE = 64
CLASSIC_DATA_SIZE = 8

# ISO 11898-1 DLC <-> payload length for CAN FD.
DLC_TO_LEN = {
    0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8,
    9: 12, 10: 16, 11: 20, 12: 24, 13: 32, 14: 48, 15: 64,
}
LEN_TO_DLC = {length: dlc for dlc, length in DLC_TO_LEN.items()}


class CanFdError(RuntimeError):
    """Base error; ``code`` classifies it for callers mapping onto their own codes."""

    code = "bus"


class NotFoundError(CanFdError):
    """No matching adapter/interface, or a selector that matches nothing."""

    code = "not_found"


class TimeoutError(CanFdError):  # noqa: A001 - deliberate, mirrors the C++ type
    """An operation exceeded its timeout."""

    code = "timeout"


class BusError(CanFdError):
    """USB/transport failure or an unsupported configuration."""

    code = "bus"


class ArgumentError(CanFdError):
    """Invalid argument, or an operation called in the wrong state."""

    code = "argument"


@dataclass
class BitTimingConst:
    tseg1_min: int
    tseg1_max: int
    tseg2_min: int
    tseg2_max: int
    sjw_max: int
    brp_min: int
    brp_max: int
    brp_inc: int


@dataclass
class BitTiming:
    prop_seg: int
    phase_seg1: int
    phase_seg2: int
    sjw: int
    brp: int

    @property
    def tseg1(self) -> int:
        return self.prop_seg + self.phase_seg1

    def as_bytes(self) -> bytes:
        return struct.pack(
            "<5I", self.prop_seg, self.phase_seg1, self.phase_seg2, self.sjw, self.brp
        )


def calculate_bit_timing(
    bitrate: int, sample_point: float, fclk: int, btc: BitTimingConst
) -> BitTiming:
    """Find prescaler/segment values matching ``sample_point``.

    Mirrors the Linux ``can_calc_bittiming`` logic: ``tseg1`` is
    ``prop_seg + phase_seg1`` and the total bit time is ``1 + tseg1 + tseg2``
    quanta.
    """
    if bitrate <= 0:
        raise CanFdError("bitrate must be positive")
    if not 0.0 < sample_point < 1.0:
        raise CanFdError("sample point must be between 0 and 1")
    if fclk <= 0:
        raise CanFdError("device clock frequency is unknown")

    best: Optional[BitTiming] = None
    best_err: Optional[float] = None
    brp = btc.brp_min
    brp_inc = btc.brp_inc if btc.brp_inc else 1
    while brp <= btc.brp_max:
        total = fclk / (bitrate * brp)
        total_i = int(round(total))
        if total_i >= 2 and abs(total - total_i) <= total * 0.01:
            tseg1 = int(round(sample_point * total_i)) - 1
            tseg2 = total_i - 1 - tseg1
            if btc.tseg1_min <= tseg1 <= btc.tseg1_max and btc.tseg2_min <= tseg2 <= btc.tseg2_max:
                err = abs((1 + tseg1) / total_i - sample_point)
                if best_err is None or err < best_err - 1e-12:
                    best = BitTiming(
                        prop_seg=1,
                        phase_seg1=tseg1 - 1,
                        phase_seg2=tseg2,
                        sjw=max(1, min(tseg2, btc.sjw_max)),
                        brp=brp,
                    )
                    best_err = err
        brp += brp_inc

    if best is None:
        raise CanFdError(
            f"cannot derive bit timing for {bitrate} bit/s at "
            f"{sample_point * 100:.1f}% with fclk={fclk}"
        )
    return best


def length_to_dlc(length: int, fd: bool) -> int:
    if not fd:
        if length > CLASSIC_DATA_SIZE:
            raise CanFdError("classic CAN payload cannot exceed 8 bytes")
        return length
    if length > FD_DATA_SIZE:
        raise CanFdError("CAN FD payload cannot exceed 64 bytes")
    for dlc, size in DLC_TO_LEN.items():
        if size >= length:
            return dlc
    return 15


def dlc_to_length(dlc: int, fd: bool) -> int:
    if not fd:
        return dlc if dlc <= CLASSIC_DATA_SIZE else 0
    return DLC_TO_LEN.get(dlc, 0)


@dataclass
class CanFrame:
    """A single CAN or CAN FD frame."""

    id: int = 0
    data: bytes = b""
    extended: bool = False
    fd: bool = False
    brs: bool = False
    remote: bool = False
    error: bool = False
    echo: bool = False
    overflow: bool = False
    channel: int = 0
    timestamp: Optional[float] = None

    def __str__(self) -> str:
        ident = f"{self.id:08X}" if self.extended else f"{self.id:03X}"
        kind = "FD" if self.fd else "  "
        ext = "X" if self.extended else ""
        payload = " ".join(f"{byte:02X}" for byte in self.data)
        if self.timestamp is None:
            return f"{ident}{ext}  [{kind}]  [{len(self.data):2d}]  {payload}"
        return f"{self.timestamp:9.6f}  {ident}{ext}  [{kind}]  [{len(self.data):2d}]  {payload}"


def encode_frame(frame: CanFrame, echo_id: int = ECHO_NONE) -> bytes:
    """Serialise a frame into the gs_usb bulk OUT layout."""
    fd = frame.fd
    if len(frame.data) > (FD_DATA_SIZE if fd else CLASSIC_DATA_SIZE):
        raise CanFdError("payload too large for frame type")

    can_id = frame.id & (CAN_EFF_MASK if frame.extended else CAN_SFF_MASK)
    if frame.extended:
        can_id |= CAN_EFF_FLAG
    if frame.remote:
        can_id |= CAN_RTR_FLAG

    flags = 0
    if fd:
        flags |= FLAG_FD
    if frame.brs:
        flags |= FLAG_BRS

    can_dlc = length_to_dlc(len(frame.data), fd)
    payload_size = FD_DATA_SIZE if fd else CLASSIC_DATA_SIZE
    header = struct.pack("<IIBBBB", echo_id, can_id, can_dlc, frame.channel, flags, 0)
    payload = bytes(frame.data) + bytes(payload_size - len(frame.data))
    return header + payload


def decode_frame(buffer: bytes, hw_timestamp: bool) -> CanFrame:
    """Parse a frame from a gs_usb bulk IN buffer."""
    if len(buffer) < HEADER_SIZE:
        raise CanFdError("short gs_usb frame")
    echo_id, can_id, can_dlc, channel, flags, _reserved = struct.unpack_from(
        "<IIBBBB", buffer, 0
    )

    is_fd = bool(flags & FLAG_FD)
    length = dlc_to_length(can_dlc, is_fd)
    data = bytes(buffer[HEADER_SIZE : HEADER_SIZE + length])

    timestamp: Optional[float] = None
    payload_size = FD_DATA_SIZE if is_fd else CLASSIC_DATA_SIZE
    if hw_timestamp and len(buffer) >= HEADER_SIZE + payload_size + 4:
        ts_us = struct.unpack_from("<I", buffer, HEADER_SIZE + payload_size)[0]
        timestamp = ts_us / 1_000_000.0

    if timestamp is None or timestamp <= 0.0:
        # No usable hardware timestamp (not requested, absent from the buffer,
        # or the firmware left it at zero): fall back to the host monotonic
        # clock so CanFrame.timestamp is always meaningful.
        timestamp = time.monotonic()

    return CanFrame(
        id=can_id & CAN_EFF_MASK,
        data=data,
        extended=bool(can_id & CAN_EFF_FLAG),
        fd=is_fd,
        brs=bool(flags & FLAG_BRS),
        remote=bool(can_id & CAN_RTR_FLAG),
        error=bool(can_id & CAN_ERR_FLAG),
        echo=echo_id != ECHO_NONE,
        overflow=bool(flags & FLAG_OVERFLOW),
        channel=channel,
        timestamp=timestamp,
    )
