from gsusb_canfd.protocol import (
    ECHO_NONE,
    HEADER_SIZE,
    CanFrame,
    decode_frame,
    encode_frame,
)


def test_fd_round_trip():
    frame = CanFrame(id=0x123, data=bytes([0xA0, 0xA1, 0xA2, 0xA3, 0xA4]),
                     fd=True, brs=True)
    encoded = encode_frame(frame, echo_id=7)
    assert len(encoded) == HEADER_SIZE + 64

    decoded = decode_frame(encoded, hw_timestamp=True)
    assert decoded.id == 0x123
    assert decoded.fd and decoded.brs
    assert not decoded.extended
    assert decoded.data == frame.data
    assert decoded.echo is True


def test_extended_classic_round_trip():
    frame = CanFrame(id=0x1ABCDEF, data=bytes([0x11, 0x22]), fd=False, extended=True)
    encoded = encode_frame(frame, echo_id=ECHO_NONE)
    assert len(encoded) == HEADER_SIZE + 8

    decoded = decode_frame(encoded, hw_timestamp=False)
    assert decoded.extended is True
    assert decoded.id == 0x1ABCDEF
    assert decoded.fd is False
    assert decoded.data == bytes([0x11, 0x22])
    assert decoded.echo is False


def test_hardware_timestamp_is_decoded_in_seconds():
    frame = CanFrame(id=0x10, data=b"\x01", fd=True)
    encoded = bytearray(encode_frame(frame, echo_id=ECHO_NONE))
    encoded.extend(b"\x00\x00\x00\x00")
    # 1000 us at the tail of the padded FD payload
    encoded[HEADER_SIZE + 64 : HEADER_SIZE + 68] = (1000).to_bytes(4, "little")

    decoded = decode_frame(bytes(encoded), hw_timestamp=True)
    assert abs(decoded.timestamp - 0.001) < 1e-9


def test_classic_hardware_timestamp_offset():
    frame = CanFrame(id=0x42, data=b"\xAA\xBB", fd=False)
    encoded = bytearray(encode_frame(frame, echo_id=ECHO_NONE))
    assert len(encoded) == HEADER_SIZE + 8
    encoded.extend((2500).to_bytes(4, "little"))

    decoded = decode_frame(bytes(encoded), hw_timestamp=True)
    assert decoded.fd is False
    assert decoded.data == b"\xAA\xBB"
    assert abs(decoded.timestamp - 0.0025) < 1e-9


def test_channel_is_preserved():
    frame = CanFrame(id=0x123, data=b"\x01", fd=True, channel=2)
    encoded = encode_frame(frame, echo_id=ECHO_NONE)
    decoded = decode_frame(encoded, hw_timestamp=False)
    assert decoded.channel == 2


def test_timestamp_falls_back_to_host_clock():
    frame = CanFrame(id=0x10, data=b"\x01", fd=True)
    encoded = encode_frame(frame, echo_id=ECHO_NONE)

    first = decode_frame(encoded, hw_timestamp=False)
    second = decode_frame(encoded, hw_timestamp=False)
    assert first.timestamp > 0.0
    assert second.timestamp >= first.timestamp

    # A zero hardware timestamp is treated the same as a missing one.
    zero_hw = bytearray(encoded)
    zero_hw.extend(b"\x00\x00\x00\x00")
    assert decode_frame(bytes(zero_hw), hw_timestamp=True).timestamp > 0.0
