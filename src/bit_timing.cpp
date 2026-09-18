#include "canfd/bit_timing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace canfd {

namespace {

void putU32Le(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

}  // namespace

std::array<uint8_t, 20> BitTiming::toBytes() const {
  std::array<uint8_t, 20> out{};
  putU32Le(out.data() + 0, prop_seg);
  putU32Le(out.data() + 4, phase_seg1);
  putU32Le(out.data() + 8, phase_seg2);
  putU32Le(out.data() + 12, sjw);
  putU32Le(out.data() + 16, brp);
  return out;
}

BitTiming calculateBitTiming(uint32_t bitrate, double sample_point, uint32_t fclk,
                             const BitTimingConst& btc) {
  if (bitrate == 0) {
    throw CanFdError("bitrate must be positive");
  }
  if (!(sample_point > 0.0 && sample_point < 1.0)) {
    throw CanFdError("sample point must be between 0 and 1");
  }
  if (fclk == 0) {
    throw CanFdError("device clock frequency is unknown");
  }

  const uint32_t brp_inc = btc.brp_inc == 0 ? 1 : btc.brp_inc;
  bool found = false;
  BitTiming best;
  double best_err = std::numeric_limits<double>::max();

  for (uint64_t brp = btc.brp_min; brp <= btc.brp_max; brp += brp_inc) {
    const double total = static_cast<double>(fclk) / (static_cast<double>(bitrate) * brp);
    const long total_i = std::lround(total);
    if (total_i < 2) {
      continue;
    }
    if (std::fabs(total - static_cast<double>(total_i)) > total * 0.01) {
      continue;
    }
    const long tseg1 = std::lround(sample_point * static_cast<double>(total_i)) - 1;
    const long tseg2 = total_i - 1 - tseg1;
    if (tseg1 < static_cast<long>(btc.tseg1_min) ||
        tseg1 > static_cast<long>(btc.tseg1_max) || tseg2 < static_cast<long>(btc.tseg2_min) ||
        tseg2 > static_cast<long>(btc.tseg2_max)) {
      continue;
    }
    const double err =
        std::fabs((1.0 + static_cast<double>(tseg1)) / static_cast<double>(total_i) -
                  sample_point);
    if (!found || err < best_err - 1e-12) {
      const uint32_t sjw =
          std::max<uint32_t>(1, std::min<uint32_t>(static_cast<uint32_t>(tseg2), btc.sjw_max));
      best.prop_seg = 1;
      best.phase_seg1 = static_cast<uint32_t>(tseg1) - 1;
      best.phase_seg2 = static_cast<uint32_t>(tseg2);
      best.sjw = sjw;
      best.brp = static_cast<uint32_t>(brp);
      best_err = err;
      found = true;
    }
  }

  if (!found) {
    throw CanFdError("cannot derive bit timing for " + std::to_string(bitrate) +
                     " bit/s at " + std::to_string(sample_point * 100.0) +
                     "% with fclk=" + std::to_string(fclk));
  }
  return best;
}

}  // namespace canfd
