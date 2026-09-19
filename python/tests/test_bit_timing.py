import math

import pytest

from gsusb_canfd.protocol import BitTimingConst, CanFdError, calculate_bit_timing


def default_const() -> BitTimingConst:
    return BitTimingConst(
        tseg1_min=1, tseg1_max=256,
        tseg2_min=1, tseg2_max=128,
        sjw_max=128,
        brp_min=1, brp_max=1024, brp_inc=1,
    )


@pytest.mark.parametrize(
    "bitrate,sample_point",
    [
        (1_000_000, 0.80),
        (5_000_000, 0.75),
        (500_000, 0.875),
        (250_000, 0.875),
        (2_000_000, 0.80),
    ],
)
def test_timing_matches_requested_bitrate_and_sample_point(bitrate, sample_point):
    fclk = 80_000_000
    timing = calculate_bit_timing(bitrate, sample_point, fclk, default_const())

    assert timing.sjw >= 1
    total = 1 + timing.tseg1 + timing.phase_seg2
    actual_bitrate = fclk / (timing.brp * total)
    assert math.isclose(actual_bitrate, bitrate, rel_tol=0.02)

    actual_sample_point = (1 + timing.tseg1) / total
    assert math.isclose(actual_sample_point, sample_point, abs_tol=0.05)


def test_zero_bitrate_is_rejected():
    with pytest.raises(CanFdError):
        calculate_bit_timing(0, 0.8, 80_000_000, default_const())


def test_invalid_sample_point_is_rejected():
    with pytest.raises(CanFdError):
        calculate_bit_timing(1_000_000, 1.5, 80_000_000, default_const())


def test_timing_bytes_are_little_endian():
    from gsusb_canfd.protocol import BitTiming

    raw = BitTiming(prop_seg=1, phase_seg1=5, phase_seg2=2, sjw=2, brp=4).as_bytes()
    assert raw == bytes(
        [1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0]
    )
