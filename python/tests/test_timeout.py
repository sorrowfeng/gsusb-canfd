"""Bulk-read timeouts must be reported as "no frame", never as a failure.

An idle CAN bus is the *normal* steady state: :meth:`CanFdBus.receive` spends
most of its life running out of time waiting for traffic, and that has to come
back as ``None``.  pyusb signals it differently on every platform -- errno 110
on Linux, errno 60 on macOS and errno 10060 (WSAETIMEDOUT, "Operation timed
out") on Windows -- so all of them are pinned down here.

These tests need no hardware, no libusb and no kernel driver, so they run on
every CI runner.  They are the regression net for the Windows-only defect where
the guard only knew about errno 60/110 and the word "timeout" without a space,
so a quiet bus made ``receive()`` raise, silently killed the ``start()`` worker
thread and crashed the ``monitor`` CLI.
"""

import time

import pytest
import usb.core

from gsusb_canfd import CanFdError, CanFdBus, CanFrame
from gsusb_canfd import bus as _bus_module

# Looked up rather than imported so that a revert to the old inline guard shows
# up as a precise test failure instead of a collection error that hides the
# rest of this file's checks.
_is_timeout = getattr(_bus_module, "_is_timeout", None)


def _usb_error(message, errno):
    """Build a USBError the way each platform's libusb binding would."""
    return usb.core.USBError(message, errno=errno)


def _classify(exc):
    """Classify *exc* with the library's own timeout predicate."""
    if _is_timeout is None:
        pytest.fail(
            "gsusb_canfd.bus._is_timeout is missing: the bulk-read timeout "
            "guard regressed to a platform-specific check"
        )
    return _is_timeout(exc)


# ------------------------------------------------------------------ classify

@pytest.mark.parametrize(
    "errno, message",
    [
        (110, "Operation timed out"),  # Linux   ETIMEDOUT
        (60, "Operation timed out"),  # macOS   ETIMEDOUT
        (10060, "Operation timed out"),  # Windows WSAETIMEDOUT
    ],
)
def test_platform_timeouts_are_not_failures(errno, message):
    assert _classify(_usb_error(message, errno)) is True


@pytest.mark.parametrize(
    "message",
    ["Operation timed out", "timed out", "TIMEOUT", "bulk read timeout"],
)
def test_message_only_timeouts_are_recognised(message):
    """Some backends raise without populating errno at all."""
    assert _classify(usb.core.USBError(message)) is True


@pytest.mark.parametrize(
    "errno, message",
    [
        (5, "Input/output error"),
        (13, "Access denied (insufficient permissions)"),
        (19, "No such device (it may have been disconnected)"),
        (32, "Pipe error"),
        (-1, "Operation not supported or unimplemented on this platform"),
    ],
)
def test_real_failures_stay_fatal(errno, message):
    """Widening the timeout check must not swallow genuine I/O errors."""
    assert _classify(_usb_error(message, errno)) is False


def test_dedicated_timeout_exception_is_recognised():
    """pyusb >= 1.3 raises a dedicated USBTimeoutError subclass."""
    exc_type = getattr(usb.core, "USBTimeoutError", None)
    if exc_type is None:
        pytest.skip("pyusb without a dedicated USBTimeoutError")
    assert _classify(exc_type("Operation timed out")) is True


# ------------------------------------------------------------------- receive

class _FakeDevice:
    """A pyusb Device stand-in whose IN endpoint always raises."""

    def __init__(self, exc):
        self._exc = exc

    def read(self, endpoint, size, timeout=None):  # noqa: ARG002 - mirror pyusb
        raise self._exc


def _started_bus(exc):
    """A CanFdBus that looks started but has no real hardware behind it."""
    bus = CanFdBus()
    bus.ep_in = 0x81
    bus.hw_timestamp = False
    bus._device = _FakeDevice(exc)
    bus._started = True
    return bus


def test_receive_returns_none_on_a_windows_timeout():
    bus = _started_bus(_usb_error("Operation timed out", 10060))
    assert bus.receive(timeout=0.1) is None


def test_receive_returns_none_on_a_linux_timeout():
    bus = _started_bus(_usb_error("Operation timed out", 110))
    assert bus.receive(timeout=0.1) is None


def test_receive_still_raises_on_a_real_failure():
    bus = _started_bus(_usb_error("Input/output error", 5))
    with pytest.raises(CanFdError):
        bus.receive(timeout=0.1)


# --------------------------------------------------------------- worker loop

def test_worker_thread_survives_an_idle_bus():
    """The async loop must keep polling a quiet bus instead of dying."""
    bus = _started_bus(_usb_error("Operation timed out", 10060))
    received = []
    bus.start(received.append)
    try:
        time.sleep(0.5)
        assert bus._worker is not None
        assert bus._worker.is_alive(), "worker thread died on an idle bus"
        assert bus._running is True
        assert received == []
    finally:
        bus.stop()
        bus._device = None


def test_worker_thread_delivers_frames_after_an_idle_spell():
    """Frames arriving after a quiet period must still reach the callback."""

    class _FlakyDevice:
        def __init__(self):
            self.calls = 0

        def read(self, endpoint, size, timeout=None):  # noqa: ARG002
            self.calls += 1
            if self.calls <= 3:
                raise usb.core.USBError("Operation timed out", errno=10060)
            return bytes(8) + b"\x01\x02\x03\x04"

    bus = CanFdBus()
    bus.ep_in = 0x81
    bus.hw_timestamp = False
    bus.is_fd = False
    bus._device = _FlakyDevice()
    bus._started = True

    received = []
    bus.start(received.append)
    try:
        deadline = time.monotonic() + 3.0
        while not received and time.monotonic() < deadline:
            time.sleep(0.05)
        assert received, "no frame delivered after the idle spell"
        assert isinstance(received[0], CanFrame)
    finally:
        bus.stop()
        bus._device = None
