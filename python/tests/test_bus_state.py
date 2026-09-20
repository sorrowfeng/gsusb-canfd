from gsusb_canfd import DEFAULT_PID, DEFAULT_VID, AdapterInfo, BusConfig, CanFdBus, DeviceSelector


def test_adapter_names():
    a = AdapterInfo(
        vendor_id=0xA8FA, product_id=0x8598, bus=1, address=6,
        manufacturer="Com Equipment", product="CANFD Analyser",
        serial="F0802068387F4D4D",
    )
    assert a.name() == "Com Equipment CANFD Analyser"
    assert a.display_name() == "Com Equipment CANFD Analyser (A8FA:8598)"
    assert a.unique_name() == "Com Equipment CANFD Analyser (A8FA:8598) SN:F0802068387F4D4D"

    bare = AdapterInfo(vendor_id=1, product_id=2, bus=1, address=2)
    assert bare.name() == "gs_usb 0001:0002"
    assert bare.unique_name() == "gs_usb 0001:0002 (0001:0002)"


def test_bus_config_defaults():
    config = BusConfig()
    assert config.fd is True
    assert config.hw_timestamp is True
    assert config.drop_echo is False

    bus = CanFdBus()
    assert bus.drop_echo is False


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
