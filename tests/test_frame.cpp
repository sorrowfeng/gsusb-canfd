#include <cmath>
#include <cstdio>
#include <cstring>

#include "canfd/frame.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

void testDlc() {
  const std::size_t lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
  for (std::size_t i = 0; i < 16; ++i) {
    const std::size_t expected = lengths[i];
    const uint8_t dlc = canfd::lengthToDlc(expected, true);
    check(dlc == i, "lengthToDlc maps canonical length to its DLC");
    check(canfd::dlcToLength(dlc, true) == expected, "dlcToLength maps canonical DLC");
  }
  check(canfd::lengthToDlc(9, true) == 9, "9 bytes -> DLC 9");
  check(canfd::lengthToDlc(13, true) == 10, "13 bytes -> DLC 10");
  check(canfd::lengthToDlc(33, true) == 14, "33 bytes -> DLC 14");
  check(canfd::lengthToDlc(64, true) == 15, "64 bytes -> DLC 15");

  check(canfd::lengthToDlc(8, false) == 8, "classic 8 bytes");
  check(canfd::dlcToLength(8, false) == 8, "classic dlc 8");
}

void testRoundTripFd() {
  canfd::CanFrame frame;
  frame.id = 0x123;
  frame.fd = true;
  frame.brs = true;
  frame.size = 5;
  for (uint8_t i = 0; i < frame.size; ++i) {
    frame.data[i] = static_cast<uint8_t>(0xA0 + i);
  }

  const auto encoded = canfd::encodeFrame(frame, 7);
  check(encoded.size() == canfd::kHeaderSize + canfd::kMaxPayload, "FD frame length is 76");

  const auto decoded = canfd::decodeFrame(encoded.data(), encoded.size(), true);
  check(decoded.id == frame.id, "id round trip");
  check(decoded.fd && decoded.brs, "fd/brs round trip");
  check(!decoded.extended, "not extended");
  check(decoded.size == frame.size, "size round trip");
  check(decoded.echo, "echo id seen");
  check(std::memcmp(decoded.data.data(), frame.data.data(), frame.size) == 0, "payload round trip");
}

void testRoundTripExtendedClassic() {
  canfd::CanFrame frame;
  frame.id = 0x1ABCDEF;
  frame.extended = true;
  frame.fd = false;
  frame.size = 2;
  frame.data[0] = 0x11;
  frame.data[1] = 0x22;

  const auto encoded = canfd::encodeFrame(frame, canfd::kEchoNone);
  check(encoded.size() == canfd::kHeaderSize + 8, "classic frame length is 20");

  const auto decoded = canfd::decodeFrame(encoded.data(), encoded.size(), false);
  check(decoded.extended, "extended round trip");
  check(decoded.id == frame.id, "extended id round trip");
  check(!decoded.fd, "classic stays classic");
  check(decoded.size == 2 && decoded.data[0] == 0x11 && decoded.data[1] == 0x22,
        "classic payload round trip");
}

void testEchoTag() {
  canfd::CanFrame frame;
  frame.id = 0x501;
  frame.fd = true;
  frame.size = 2;
  frame.data[0] = 0x00;
  frame.data[1] = 0x02;

  check(canfd::kEchoTag != canfd::kEchoNone, "the TX tag is a real tag");

  // Untagged: the frame comes back indistinguishable from received traffic,
  // which is why CanFdBus::send() is documented as unable to produce echoes.
  const auto untagged = canfd::encodeFrame(frame, canfd::kEchoNone);
  const auto untagged_out = canfd::decodeFrame(untagged.data(), untagged.size(), false);
  check(!untagged_out.echo, "kEchoNone does not mark a loopback");

  // Tagged: this is what CanFdBus::send(frame, true) writes, and the tag must
  // not disturb the arbitration id or the payload.
  const auto tagged = canfd::encodeFrame(frame, canfd::kEchoTag);
  const auto tagged_out = canfd::decodeFrame(tagged.data(), tagged.size(), false);
  check(tagged_out.echo, "kEchoTag marks a loopback");
  check(tagged_out.id == frame.id, "the tag leaves the id alone");
  check(tagged_out.size == 2 && tagged_out.data[0] == 0x00 && tagged_out.data[1] == 0x02,
        "the tag leaves the payload alone");
}

void testTimestamp() {
  canfd::CanFrame frame;
  frame.id = 0x10;
  frame.fd = true;
  frame.size = 1;
  auto encoded = canfd::encodeFrame(frame, canfd::kEchoNone);
  encoded.resize(canfd::kHeaderSize + canfd::kMaxPayload + 4, 0);
  encoded[canfd::kHeaderSize + canfd::kMaxPayload + 0] = 0xE8;
  encoded[canfd::kHeaderSize + canfd::kMaxPayload + 1] = 0x03;
  encoded[canfd::kHeaderSize + canfd::kMaxPayload + 2] = 0x00;
  encoded[canfd::kHeaderSize + canfd::kMaxPayload + 3] = 0x00;

  const auto decoded = canfd::decodeFrame(encoded.data(), encoded.size(), true);
  check(std::fabs(decoded.timestamp - 0.001) < 1e-9, "hardware timestamp decoded in seconds");
}

}  // namespace

int main() {
  testDlc();
  testRoundTripFd();
  testRoundTripExtendedClassic();
  testEchoTag();
  testTimestamp();
  if (failures == 0) {
    std::printf("test_frame: OK\n");
  }
  return failures == 0 ? 0 : 1;
}
