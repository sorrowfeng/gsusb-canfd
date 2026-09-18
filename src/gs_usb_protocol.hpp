#ifndef CANFD_GS_USB_PROTOCOL_HPP
#define CANFD_GS_USB_PROTOCOL_HPP

#include <cstdint>

namespace canfd {
namespace gs_usb {

// Control requests (include/gs_usb.h from candleLight_fw).
constexpr uint8_t kBreqHostFormat = 0;
constexpr uint8_t kBreqBittiming = 1;
constexpr uint8_t kBreqMode = 2;
constexpr uint8_t kBreqBtConst = 4;
constexpr uint8_t kBreqDeviceConfig = 5;
constexpr uint8_t kBreqDataBittiming = 10;
constexpr uint8_t kBreqBtConstExt = 11;

constexpr uint8_t kModeReset = 0;
constexpr uint8_t kModeStart = 1;

constexpr uint32_t kModeListenOnly = 1u << 0;
constexpr uint32_t kModeLoopback = 1u << 1;
constexpr uint32_t kModeOneShot = 1u << 3;
constexpr uint32_t kModeHwTimestamp = 1u << 4;
constexpr uint32_t kModeFd = 1u << 8;

constexpr uint32_t kFeatureListenOnly = 1u << 0;
constexpr uint32_t kFeatureLoopback = 1u << 1;
constexpr uint32_t kFeatureOneShot = 1u << 3;
constexpr uint32_t kFeatureHwTimestamp = 1u << 4;
constexpr uint32_t kFeatureFd = 1u << 8;
constexpr uint32_t kFeatureBtConstExt = 1u << 10;

constexpr uint8_t kFlagOverflow = 1u << 0;
constexpr uint8_t kFlagFd = 1u << 1;
constexpr uint8_t kFlagBrs = 1u << 2;
constexpr uint8_t kFlagEsi = 1u << 3;

constexpr uint32_t kHostFormatMagic = 0x0000BEEF;

// Control transfer request types.
constexpr uint8_t kCtrlOut = 0x41;
constexpr uint8_t kCtrlIn = 0xC1;

}  // namespace gs_usb
}  // namespace canfd

#endif  // CANFD_GS_USB_PROTOCOL_HPP
