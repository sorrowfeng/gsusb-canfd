from gsusb_canfd import DEFAULT_PID, DEFAULT_VID, CanFdBus, DeviceSelector


def test_fresh_bus_is_not_open_or_started():
    bus = CanFdBus(DeviceSelector(vid=DEFAULT_VID, pid=DEFAULT_PID, index=0))
    assert bus.is_open() is False
    assert bus.is_started() is False
    assert bus.is_fd is False
    assert bus.channel_count() == 1
    # closing a bus that was never opened must be a no-op
    bus.close()


def test_default_selector_auto_discovers():
    bus = CanFdBus()
    assert bus.selector.vid == 0
    assert bus.selector.pid == 0
    assert bus.selector.channel == 0
    assert bus.channel == 0


def test_explicit_selector_targets_project_device():
    bus = CanFdBus(DeviceSelector(vid=0xA8FA, pid=0x8598, channel=1))
    assert bus.channel == 1
