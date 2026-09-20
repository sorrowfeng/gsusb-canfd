#ifndef CANFD_BIT_TIMING_HPP
#define CANFD_BIT_TIMING_HPP

#include <array>
#include <cstdint>

#include "canfd/error.hpp"

namespace canfd {

struct BitTimingConst {
  uint32_t tseg1_min = 0;
  uint32_t tseg1_max = 0;
  uint32_t tseg2_min = 0;
  uint32_t tseg2_max = 0;
  uint32_t sjw_max = 0;
  uint32_t brp_min = 0;
  uint32_t brp_max = 0;
  uint32_t brp_inc = 1;
};

struct BitTiming {
  uint32_t prop_seg = 0;
  uint32_t phase_seg1 = 0;
  uint32_t phase_seg2 = 0;
  uint32_t sjw = 0;
  uint32_t brp = 0;

  uint32_t tseg1() const { return prop_seg + phase_seg1; }

  std::array<uint8_t, 20> toBytes() const;
};

BitTiming calculateBitTiming(uint32_t bitrate, double sample_point, uint32_t fclk,
                             const BitTimingConst& btc);

}  // namespace canfd

#endif  // CANFD_BIT_TIMING_HPP
