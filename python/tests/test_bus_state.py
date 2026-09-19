from gsusb_canfd import CanFdBus, DeviceSelector


def test_fresh_bus_is_not_open_or_started():
    bus = CanFdBus(DeviceSelector(vid=0xA8FA, pid=0x8598, index=0))
    assert bus.is_open() is False
    assert bus.is_started() is False
    assert bus.is_fd is False
    # closing a bus that was never opened must be a no-op
    bus.close()


def test_default_selector_targets_project_device():
    bus = CanFdBus()
    assert bus.selector.vid == 0xA8FA
    assert bus.selector.pid == 0x8598
