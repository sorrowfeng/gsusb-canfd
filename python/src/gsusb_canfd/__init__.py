"""Python implementation of the gsusb-canfd library.

The wire protocol, bit timing and frame codec live in
:mod:`gsusb_canfd.protocol`; :class:`~gsusb_canfd.bus.CanFdBus` speaks to the
adapter through pyusb.
"""

from ._version import __version__
from .bus import (
    DEFAULT_PID,
    DEFAULT_VID,
    AdapterInfo,
    BusConfig,
    CanFdBus,
    DeviceInfo,
    DeviceSelector,
    scan_adapters,
)
from .protocol import (
    ArgumentError,
    BitTiming,
    BitTimingConst,
    BusError,
    CanFdError,
    CanFrame,
    NotFoundError,
    TimeoutError,
    calculate_bit_timing,
    decode_frame,
    dlc_to_length,
    encode_frame,
    length_to_dlc,
)

__all__ = [
    "AdapterInfo",
    "ArgumentError",
    "BusConfig",
    "BitTiming",
    "BitTimingConst",
    "BusError",
    "CanFdBus",
    "CanFdError",
    "CanFrame",
    "DEFAULT_PID",
    "DEFAULT_VID",
    "DeviceInfo",
    "DeviceSelector",
    "NotFoundError",
    "TimeoutError",
    "calculate_bit_timing",
    "decode_frame",
    "dlc_to_length",
    "encode_frame",
    "length_to_dlc",
    "scan_adapters",
    "__version__",
]
