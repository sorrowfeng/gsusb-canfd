#include "canfd/frame.hpp"

#include <cstdio>

#include "canfd/bit_timing.hpp"
#include "gs_usb_protocol.hpp"

namespace canfd {

namespace {

constexpr std::size_t kDlcToLen[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

void putU32Le(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint32_t getU32Le(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

}  // namespace

uint8_t lengthToDlc(std::size_t length, bool fd) {
  if (!fd) {
    if (length > 8) {
      throw CanFdError("classic CAN payload cannot exceed 8 bytes");
    }
    return static_cast<uint8_t>(length);
  }
  if (length > kMaxPayload) {
    throw CanFdError("CAN FD payload cannot exceed 64 bytes");
  }
  for (uint8_t dlc = 0; dlc < 16; ++dlc) {
    if (kDlcToLen[dlc] >= length) {
      return dlc;
    }
  }
  return 15;
}

std::size_t dlcToLength(uint8_t dlc, bool fd) {
  if (!fd) {
    return dlc > 8 ? 0 : dlc;
  }
  return dlc < 16 ? kDlcToLen[dlc] : 0;
}

std::vector<uint8_t> encodeFrame(const CanFrame& frame, uint32_t echo_id) {
  const bool fd = frame.fd;
  if (frame.size > (fd ? 64u : 8u)) {
    throw CanFdError("payload too large for frame type");
  }

  uint32_t can_id = frame.id & (frame.extended ? kCanEffMask : 0x7FFu);
  if (frame.extended) {
    can_id |= kCanEffFlag;
  }
  if (frame.remote) {
    can_id |= kCanRtrFlag;
  }

  uint8_t flags = 0;
  if (fd) {
    flags |= gs_usb::kFlagFd;
  }
  if (frame.brs) {
    flags |= gs_usb::kFlagBrs;
  }

  const uint8_t dlc = lengthToDlc(frame.size, fd);
  const std::size_t payload_size = fd ? kMaxPayload : 8;
  std::vector<uint8_t> out(kHeaderSize + payload_size, 0);

  putU32Le(out.data() + 0, echo_id);
  putU32Le(out.data() + 4, can_id);
  out[8] = dlc;
  out[9] = 0;
  out[10] = flags;
  out[11] = 0;

  for (uint8_t i = 0; i < frame.size; ++i) {
    out[kHeaderSize + i] = frame.data[i];
  }
  return out;
}

CanFrame decodeFrame(const uint8_t* buffer, std::size_t length, bool hw_timestamp) {
  if (length < kHeaderSize) {
    throw CanFdError("short gs_usb frame");
  }

  CanFrame frame;
  const uint32_t echo_id = getU32Le(buffer + 0);
  const uint32_t can_id = getU32Le(buffer + 4);
  const uint8_t dlc = buffer[8];
  const uint8_t flags = buffer[10];

  frame.fd = (flags & gs_usb::kFlagFd) != 0;
  frame.brs = (flags & gs_usb::kFlagBrs) != 0;
  frame.error = (can_id & kCanErrFlag) != 0;
  frame.remote = (can_id & kCanRtrFlag) != 0;
  frame.extended = (can_id & kCanEffFlag) != 0;
  frame.echo = echo_id != kEchoNone;
  frame.overflow = (flags & gs_usb::kFlagOverflow) != 0;
  frame.channel = buffer[9];
  frame.id = can_id & kCanEffMask;

  const std::size_t payload = dlcToLength(dlc, frame.fd);
  frame.size = static_cast<uint8_t>(payload);
  for (std::size_t i = 0; i < payload && kHeaderSize + i < length; ++i) {
    frame.data[i] = buffer[kHeaderSize + i];
  }

  if (hw_timestamp && length >= kHeaderSize + kMaxPayload + 4) {
    const uint32_t ts_us = getU32Le(buffer + kHeaderSize + kMaxPayload);
    frame.timestamp = static_cast<double>(ts_us) / 1'000'000.0;
  }
  return frame;
}

std::string CanFrame::toString() const {
  char id_buf[24];
  if (extended) {
    std::snprintf(id_buf, sizeof(id_buf), "%08X", id);
  } else {
    std::snprintf(id_buf, sizeof(id_buf), "%03X", id);
  }

  std::string out = id_buf;
  if (extended) {
    out += "X";
  }
  out += fd ? "  [FD]" : "  [  ]";
  char len_buf[16];
  std::snprintf(len_buf, sizeof(len_buf), "  [%2u]  ", size);
  out += len_buf;

  char byte[4];
  for (uint8_t i = 0; i < size; ++i) {
    std::snprintf(byte, sizeof(byte), "%02X", data[i]);
    out += byte;
    if (i + 1 < size) {
      out += " ";
    }
  }
  if (timestamp > 0.0) {
    char ts[32];
    std::snprintf(ts, sizeof(ts), "  @%.6f", timestamp);
    out += ts;
  }
  return out;
}

}  // namespace canfd
