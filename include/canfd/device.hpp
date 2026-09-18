#ifndef CANFD_DEVICE_HPP
#define CANFD_DEVICE_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "canfd/bit_timing.hpp"
#include "canfd/frame.hpp"

namespace canfd {

constexpr uint16_t kDefaultVid = 0xA8FA;
constexpr uint16_t kDefaultPid = 0x8598;

namespace feature {
constexpr uint32_t kListenOnly = 1u << 0;
constexpr uint32_t kLoopback = 1u << 1;
constexpr uint32_t kOneShot = 1u << 3;
constexpr uint32_t kHwTimestamp = 1u << 4;
constexpr uint32_t kFd = 1u << 8;
constexpr uint32_t kBtConstExt = 1u << 10;
}  // namespace feature

struct AdapterInfo {
  uint16_t vendor_id = 0;
  uint16_t product_id = 0;
  uint8_t bus = 0;
  uint8_t address = 0;
  std::string manufacturer;
  std::string product;
  std::string serial;

  std::string name() const;
};

struct DeviceSelector {
  uint16_t vid = kDefaultVid;
  uint16_t pid = kDefaultPid;
  int index = 0;
  std::optional<std::string> serial;
  std::optional<std::string> product;
};

struct DeviceInfo {
  uint32_t icount = 0;
  uint32_t sw_version = 0;
  uint32_t hw_version = 0;
};

struct BusConfig {
  uint32_t bitrate = 1'000'000;
  double sample_point = 0.80;
  uint32_t data_bitrate = 5'000'000;
  double data_sample_point = 0.75;
  bool fd = true;
  bool listen_only = false;
  bool loopback = false;
  bool one_shot = false;
  bool hw_timestamp = true;
};

std::vector<AdapterInfo> scanAdapters(uint16_t vid = 0, uint16_t pid = 0);

class CanFdBus {
 public:
  using ReceiveCallback = std::function<void(const CanFrame&)>;

  explicit CanFdBus(const DeviceSelector& selector = DeviceSelector{});
  ~CanFdBus();

  CanFdBus(const CanFdBus&) = delete;
  CanFdBus& operator=(const CanFdBus&) = delete;
  CanFdBus(CanFdBus&&) noexcept;
  CanFdBus& operator=(CanFdBus&&) noexcept;

  void open();
  void configure(const BusConfig& config = BusConfig{});
  void send(const CanFrame& frame);
  bool receive(CanFrame& out,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

  void start(ReceiveCallback callback);
  void stop();
  void close();

  bool isOpen() const;
  bool isStarted() const;
  bool isFd() const;

  const DeviceInfo& deviceInfo() const;
  uint32_t feature() const;
  uint32_t clockFrequency() const;
  const BitTiming& nominalTiming() const;
  const BitTiming& dataTiming() const;
  int inputEndpoint() const;
  int outputEndpoint() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canfd

#endif  // CANFD_DEVICE_HPP
