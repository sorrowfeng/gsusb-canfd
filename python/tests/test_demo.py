"""Hardware-free checks for the canfd_demo helpers."""

import importlib.util
from pathlib import Path

from gsusb_canfd import CanFrame

_DEMO = Path(__file__).resolve().parents[1] / "examples" / "canfd_demo.py"
_spec = importlib.util.spec_from_file_location("canfd_demo", _DEMO)
demo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(demo)


def test_parse_hex_bytes_accepts_common_separators():
    assert demo.parse_hex_bytes("00 02 50 01") == b"\x00\x02\x50\x01"
    assert demo.parse_hex_bytes("00:02-50,01") == b"\x00\x02\x50\x01"
    assert demo.parse_hex_bytes("") == b""


def test_make_frame_sets_extended_and_fd():
    standard = demo.make_frame(0x123, b"\x01", fd=True)
    assert isinstance(standard, CanFrame)
    assert standard.extended is False
    assert standard.fd and standard.brs

    extended = demo.make_frame(0x1ABCDEF, b"", fd=False)
    assert extended.extended is True
    assert extended.fd is False


def test_feature_string_lists_known_bits():
    assert demo.feature_string(0) == "none"
    text = demo.feature_string(demo.FEATURE_FD | demo.FEATURE_HW_TIMESTAMP)
    assert "fd" in text and "hw_timestamp" in text
