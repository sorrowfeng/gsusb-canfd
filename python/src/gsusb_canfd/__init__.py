"""Python implementation of the gsusb-canfd library.

The wire protocol, bit timing and frame codec live in
:mod:`gsusb_canfd.protocol`; :class:`~gsusb_canfd.bus.CanFdBus` speaks to the
adapter through pyusb.
"""

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
    BitTiming,
    BitTimingConst,
    CanFdError,
    CanFrame,
    calculate_bit_timing,
    decode_frame,
    dlc_to_length,
    encode_frame,
    length_to_dlc,
)

__version__ = "0.1.0"

__all__ = [
    "AdapterInfo",
    "BusConfig",
    "BitTiming",
    "BitTimingConst",
    "CanFdBus",
    "CanFdError",
    "CanFrame",
    "DEFAULT_PID",
    "DEFAULT_VID",
    "DeviceInfo",
    "DeviceSelector",
    "calculate_bit_timing",
    "decode_frame",
    "dlc_to_length",
    "encode_frame",
    "length_to_dlc",
    "scan_adapters",
    "__version__",
]
