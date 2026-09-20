#include <cmath>
#include <cstdio>

#include "canfd/bit_timing.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

canfd::BitTimingConst defaultConst() {
  canfd::BitTimingConst btc;
  btc.tseg1_min = 1;
  btc.tseg1_max = 256;
  btc.tseg2_min = 1;
  btc.tseg2_max = 128;
  btc.sjw_max = 128;
  btc.brp_min = 1;
  btc.brp_max = 1024;
  btc.brp_inc = 1;
  return btc;
}

void checkTiming(uint32_t bitrate, double sample_point) {
  const uint32_t fclk = 80'000'000;
  const auto btc = defaultConst();
  const auto timing = canfd::calculateBitTiming(bitrate, sample_point, fclk, btc);

  check(timing.sjw >= 1, "sjw must be at least 1");
  check(timing.brp >= btc.brp_min && timing.brp <= btc.brp_max, "brp out of range");

  const uint32_t total = 1 + timing.tseg1() + timing.phase_seg2;
  const double actual_bitrate =
      static_cast<double>(fclk) / (static_cast<double>(timing.brp) * total);
  check(std::fabs(actual_bitrate - bitrate) < bitrate * 0.02, "bitrate mismatch");

  const double actual_sp = static_cast<double>(1 + timing.tseg1()) / total;
  check(std::fabs(actual_sp - sample_point) < 0.05, "sample point mismatch");
}

}  // namespace

int main() {
  checkTiming(1'000'000, 0.80);
  checkTiming(5'000'000, 0.75);
  checkTiming(500'000, 0.875);
  checkTiming(250'000, 0.875);
  checkTiming(2'000'000, 0.80);

  try {
    canfd::calculateBitTiming(0, 0.8, 48'000'000, defaultConst());
    check(false, "zero bitrate should throw");
  } catch (const canfd::ArgumentError& exc) {
    check(exc.code() == canfd::ErrorCode::kArgument, "invalid input is an ArgumentError");
  }

  try {
    canfd::calculateBitTiming(1'000'000, 1.5, 48'000'000, defaultConst());
    check(false, "invalid sample point should throw");
  } catch (const canfd::ArgumentError&) {
  }

  // The typed errors are also CanFdError, so existing catch clauses keep working.
  try {
    canfd::calculateBitTiming(0, 0.8, 48'000'000, defaultConst());
  } catch (const canfd::CanFdError&) {
  }

  const auto bytes = canfd::BitTiming{1, 5, 2, 2, 4}.toBytes();
  check(bytes[0] == 1 && bytes[4] == 5 && bytes[8] == 2 && bytes[12] == 2 && bytes[16] == 4,
        "BitTiming::toBytes little-endian layout");

  if (failures == 0) {
    std::printf("test_bit_timing: OK\n");
  }
  return failures == 0 ? 0 : 1;
}
