"""`monitor --show-echo` has to be able to see anything at all.

The adapter only marks a loopback as one when the sender asked for a tag, so
the previous CLI could never print an echo frame: it never tagged its own
trigger, which made both `--show-echo` and the `frame.echo` skip a no-op.  These
checks pin the wiring down with a fake bus, so they need no hardware.
"""

import pytest

from gsusb_canfd import cli
from gsusb_canfd.protocol import CanFrame

ECHO_FRAME = CanFrame(id=0x501, data=b"\xDE\xAD\xBE\xEF", fd=True, brs=True, echo=True)
# Printed exactly once per echo frame and nowhere else -- the "trigger: sent 501"
# line would make a plain "501" check useless.
ECHO_MARKER = "DE AD BE EF"


class _FakeBus:
    """Just enough of CanFdBus for cmd_monitor(), driven by a script."""

    ep_in = 0x81
    ep_out = 0x01
    fclk_can = 80_000_000
    feature = 0
    is_fd = True
    hw_timestamp = True
    nominal_timing = None
    data_timing = None

    def __init__(self, script):
        self._script = list(script)
        self.sent = []
        self.closed = False

    def send(self, frame, echo=False):
        self.sent.append((frame, echo))

    def receive(self, timeout=1.0):
        if not self._script:
            raise KeyboardInterrupt
        item = self._script.pop(0)
        if item is None:
            return None
        return item

    def close(self):
        self.closed = True


def _run(monkeypatch, argv, script):
    bus = _FakeBus(script)
    monkeypatch.setattr(cli, "_open", lambda args: bus)
    assert cli.main(argv) == 0
    assert bus.closed, "the bus must be closed even on interrupt"
    return bus


def test_trigger_is_sent_untagged_by_default(monkeypatch, capsys):
    bus = _run(monkeypatch, ["monitor", "--trigger", "--count", "1"], [ECHO_FRAME])
    assert len(bus.sent) == 1
    assert bus.sent[0][1] is False, "no tag unless --show-echo asked for one"
    assert ECHO_MARKER not in capsys.readouterr().out


def test_show_echo_tags_the_trigger_and_prints_the_loopback(monkeypatch, capsys):
    bus = _run(
        monkeypatch,
        ["monitor", "--trigger", "--show-echo", "--count", "1"],
        [ECHO_FRAME],
    )
    assert len(bus.sent) == 1
    assert bus.sent[0][1] is True, "--show-echo must request the TX tag"
    assert ECHO_MARKER in capsys.readouterr().out


class _Clock:
    """A clock that jumps 60 s per read, so --repeat always fires immediately."""

    def __init__(self):
        self._now = 0.0

    def time(self):
        self._now += 60.0
        return self._now


@pytest.mark.parametrize("show_echo", [False, True])
def test_repeated_trigger_uses_the_same_echo_choice(monkeypatch, capsys, show_echo):
    monkeypatch.setattr(cli, "time", _Clock())
    argv = ["monitor", "--trigger", "--repeat", "1.0"]
    if show_echo:
        argv.append("--show-echo")
    bus = _run(monkeypatch, argv, [None, None, ECHO_FRAME])
    assert len(bus.sent) >= 2, "the repeat branch must send the trigger again"
    assert all(echo is show_echo for _, echo in bus.sent)


def test_peer_frames_still_print_without_show_echo(monkeypatch, capsys):
    peer = CanFrame(id=0x481, data=b"\x11", fd=True, brs=True)
    _run(monkeypatch, ["monitor", "--count", "1"], [None, peer])
    assert "481" in capsys.readouterr().out
