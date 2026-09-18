#ifndef CANFD_FRAME_HPP
#define CANFD_FRAME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace canfd {

constexpr uint32_t kCanEffFlag = 0x80000000u;
constexpr uint32_t kCanRtrFlag = 0x40000000u;
constexpr uint32_t kCanErrFlag = 0x20000000u;
constexpr uint32_t kCanEffMask = 0x1FFFFFFFu;
constexpr uint32_t kEchoNone = 0xFFFFFFFFu;

constexpr std::size_t kMaxPayload = 64;
constexpr std::size_t kHeaderSize = 12;

struct CanFrame {
  uint32_t id = 0;
  std::array<uint8_t, kMaxPayload> data{};
  uint8_t size = 0;
  bool extended = false;
  bool fd = false;
  bool brs = false;
  bool remote = false;
  bool error = false;
  bool echo = false;
  bool overflow = false;
  uint8_t channel = 0;
  double timestamp = 0.0;

  std::string toString() const;
};

uint8_t lengthToDlc(std::size_t length, bool fd);
std::size_t dlcToLength(uint8_t dlc, bool fd);

std::vector<uint8_t> encodeFrame(const CanFrame& frame, uint32_t echo_id);

CanFrame decodeFrame(const uint8_t* buffer, std::size_t length, bool hw_timestamp);

}  // namespace canfd

#endif  // CANFD_FRAME_HPP
