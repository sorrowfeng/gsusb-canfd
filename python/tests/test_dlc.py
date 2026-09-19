import pytest

from gsusb_canfd.protocol import CanFdError, dlc_to_length, length_to_dlc

CANONICAL = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]


@pytest.mark.parametrize("dlc,length", list(enumerate(CANONICAL)))
def test_fd_round_trip(dlc, length):
    assert dlc_to_length(dlc, fd=True) == length
    assert length_to_dlc(length, fd=True) == dlc


@pytest.mark.parametrize(
    "length,expected_dlc",
    [(9, 9), (13, 10), (17, 11), (21, 12), (25, 13), (33, 14), (49, 15), (64, 15)],
)
def test_fd_non_canonical_lengths(length, expected_dlc):
    assert length_to_dlc(length, fd=True) == expected_dlc


def test_classic_lengths():
    for length in range(9):
        assert length_to_dlc(length, fd=False) == length
        assert dlc_to_length(length, fd=False) == length


def test_rejects_oversized_payloads():
    with pytest.raises(CanFdError):
        length_to_dlc(9, fd=False)
    with pytest.raises(CanFdError):
        length_to_dlc(65, fd=True)
