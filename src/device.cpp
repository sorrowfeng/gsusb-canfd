#include "canfd/device.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "canfd/bit_timing.hpp"
#include "gs_usb_protocol.hpp"
#include "usb_transport.hpp"

namespace canfd {

namespace {

uint32_t readU32Le(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

BitTimingConst parseConst(const uint8_t* data) {
  BitTimingConst btc;
  btc.tseg1_min = readU32Le(data + 0);
  btc.tseg1_max = readU32Le(data + 4);
  btc.tseg2_min = readU32Le(data + 8);
  btc.tseg2_max = readU32Le(data + 12);
  btc.sjw_max = readU32Le(data + 16);
  btc.brp_min = readU32Le(data + 20);
  btc.brp_max = readU32Le(data + 24);
  btc.brp_inc = readU32Le(data + 28);
  return btc;
}

}  // namespace

struct CanFdBus::Impl {
  explicit Impl(const DeviceSelector& sel) : selector(sel) {}

  DeviceSelector selector;
  std::unique_ptr<UsbTransport> transport;

  uint32_t feature = 0;
  uint32_t fclk_can = 0;
  BitTimingConst nominal_const;
  BitTimingConst data_const;
  bool has_data_const = false;
  DeviceInfo device_info;
  bool has_device_info = false;

  BitTiming nominal_timing;
  BitTiming data_timing;
  bool is_fd = false;
  bool listen_only = false;
  bool hw_timestamp = true;
  bool started = false;

  std::thread worker;
  std::atomic<bool> running{false};
  ReceiveCallback callback;
  std::mutex callback_mutex;

  std::size_t rxSize() const {
    std::size_t size = kHeaderSize + (is_fd ? kMaxPayload : 8);
    if (hw_timestamp) {
      size += 4;
    }
    return size;
  }

  void readCapabilities() {
    std::vector<uint8_t> buffer(72, 0);
    int received = 0;
    try {
      received = transport->controlIn(gs_usb::kBreqBtConstExt, 0, 0, buffer.data(), 72);
    } catch (const CanFdError&) {
      received = 0;
    }
    if (received >= 72) {
      feature = readU32Le(buffer.data() + 0);
      fclk_can = readU32Le(buffer.data() + 4);
      nominal_const = parseConst(buffer.data() + 8);
      data_const = parseConst(buffer.data() + 40);
      has_data_const = true;
      return;
    }

    std::vector<uint8_t> legacy(40, 0);
    const int legacy_received =
        transport->controlIn(gs_usb::kBreqBtConst, 0, 0, legacy.data(), 40);
    if (legacy_received < 40) {
      throw CanFdError("device did not report bit timing constants");
    }
    feature = readU32Le(legacy.data() + 0);
    fclk_can = readU32Le(legacy.data() + 4);
    nominal_const = parseConst(legacy.data() + 8);
    data_const = nominal_const;
    has_data_const = false;
  }

  void readDeviceInfo() {
    std::vector<uint8_t> buffer(12, 0);
    try {
      const int received =
          transport->controlIn(gs_usb::kBreqDeviceConfig, 1, 0, buffer.data(), 12);
      if (received >= 12) {
        device_info.icount = buffer[3];
        device_info.sw_version = readU32Le(buffer.data() + 4);
        device_info.hw_version = readU32Le(buffer.data() + 8);
        has_device_info = true;
      }
    } catch (const CanFdError&) {
      has_device_info = false;
    }
  }
};

CanFdBus::CanFdBus(const DeviceSelector& selector) : impl_(new Impl(selector)) {}

CanFdBus::~CanFdBus() {
  try {
    stop();
    close();
  } catch (...) {
  }
}

CanFdBus::CanFdBus(CanFdBus&&) noexcept = default;
CanFdBus& CanFdBus::operator=(CanFdBus&&) noexcept = default;

void CanFdBus::open() {
  if (impl_->transport && impl_->transport->isOpen()) {
    return;
  }
  impl_->transport.reset(new UsbTransport(impl_->selector));
  impl_->transport->open();

  const uint8_t magic[4] = {0xEF, 0xBE, 0x00, 0x00};
  try {
    impl_->transport->controlOut(gs_usb::kBreqHostFormat, 1,
                                 static_cast<uint16_t>(impl_->transport->interfaceNumber()),
                                 magic, 4);
  } catch (const CanFdError&) {
  }

  impl_->readCapabilities();
  impl_->readDeviceInfo();
}

void CanFdBus::configure(const BusConfig& config) {
  if (!impl_->transport || !impl_->transport->isOpen()) {
    throw CanFdError("call open() before configure()");
  }

  const bool fd = config.fd && (impl_->feature & gs_usb::kFeatureFd);
  if (config.fd && !(impl_->feature & gs_usb::kFeatureFd)) {
    throw CanFdError("device does not support CAN FD");
  }

  impl_->nominal_timing = calculateBitTiming(config.bitrate, config.sample_point,
                                             impl_->fclk_can, impl_->nominal_const);
  const auto nominal_bytes = impl_->nominal_timing.toBytes();
  impl_->transport->controlOut(gs_usb::kBreqBittiming, 0, 0, nominal_bytes.data(), 20);

  if (fd) {
    if (!impl_->has_data_const) {
      throw CanFdError("device does not report CAN FD timing constants");
    }
    impl_->data_timing = calculateBitTiming(config.data_bitrate, config.data_sample_point,
                                            impl_->fclk_can, impl_->data_const);
    const auto data_bytes = impl_->data_timing.toBytes();
    impl_->transport->controlOut(gs_usb::kBreqDataBittiming, 0, 0, data_bytes.data(), 20);
  }

  impl_->is_fd = fd;
  impl_->listen_only = config.listen_only;
  impl_->hw_timestamp =
      config.hw_timestamp && ((impl_->feature & gs_usb::kFeatureHwTimestamp) != 0);

  uint32_t flags = 0;
  if (config.listen_only) {
    if (!(impl_->feature & gs_usb::kFeatureListenOnly)) {
      throw CanFdError("device does not support listen-only mode");
    }
    flags |= gs_usb::kModeListenOnly;
  }
  if (config.loopback) {
    if (!(impl_->feature & gs_usb::kFeatureLoopback)) {
      throw CanFdError("device does not support loopback mode");
    }
    flags |= gs_usb::kModeLoopback;
  }
  if (config.one_shot) {
    if (!(impl_->feature & gs_usb::kFeatureOneShot)) {
      throw CanFdError("device does not support one-shot mode");
    }
    flags |= gs_usb::kModeOneShot;
  }
  if (fd) {
    flags |= gs_usb::kModeFd;
  }
  if (impl_->hw_timestamp) {
    flags |= gs_usb::kModeHwTimestamp;
  }

  uint8_t mode[8];
  mode[0] = gs_usb::kModeStart & 0xFF;
  mode[1] = 0;
  mode[2] = 0;
  mode[3] = 0;
  mode[4] = static_cast<uint8_t>(flags & 0xFF);
  mode[5] = static_cast<uint8_t>((flags >> 8) & 0xFF);
  mode[6] = static_cast<uint8_t>((flags >> 16) & 0xFF);
  mode[7] = static_cast<uint8_t>((flags >> 24) & 0xFF);
  impl_->transport->controlOut(gs_usb::kBreqMode, 0, 0, mode, 8);
  impl_->started = true;
}

void CanFdBus::send(const CanFrame& frame) {
  if (!impl_->transport || !impl_->started) {
    throw CanFdError("device is not started");
  }

  CanFrame out = frame;
  if (frame.fd && !impl_->is_fd) {
    throw CanFdError("cannot send CAN FD frame on a classic CAN bus");
  }

  const std::vector<uint8_t> encoded = encodeFrame(out, kEchoNone);
  impl_->transport->bulkWrite(encoded.data(), static_cast<int>(encoded.size()), 1000);
}

bool CanFdBus::receive(CanFrame& out, std::chrono::milliseconds timeout) {
  if (!impl_->transport || !impl_->started) {
    throw CanFdError("device is not started");
  }

  std::vector<uint8_t> buffer(impl_->rxSize(), 0);
  const int received = impl_->transport->bulkRead(
      buffer.data(), static_cast<int>(buffer.size()), static_cast<unsigned>(timeout.count()));
  if (received < 0) {
    return false;
  }
  out = decodeFrame(buffer.data(), static_cast<std::size_t>(received), impl_->hw_timestamp);
  return true;
}

void CanFdBus::start(ReceiveCallback callback) {
  if (!impl_->transport || !impl_->started) {
    throw CanFdError("device is not started");
  }
  if (impl_->running.exchange(true)) {
    throw CanFdError("receive loop is already running");
  }
  {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->callback = std::move(callback);
  }
  impl_->worker = std::thread([this]() {
    CanFrame frame;
    while (impl_->running.load()) {
      try {
        if (!receive(frame, std::chrono::milliseconds(100))) {
          continue;
        }
      } catch (const CanFdError&) {
        break;
      }
      std::lock_guard<std::mutex> lock(impl_->callback_mutex);
      if (impl_->callback) {
        impl_->callback(frame);
      }
    }
  });
}

void CanFdBus::stop() {
  if (!impl_->running.exchange(false)) {
    if (impl_->worker.joinable()) {
      impl_->worker.join();
    }
    return;
  }
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  std::lock_guard<std::mutex> lock(impl_->callback_mutex);
  impl_->callback = nullptr;
}

void CanFdBus::close() {
  if (!impl_->transport) {
    return;
  }
  if (impl_->started && impl_->transport->isOpen()) {
    const uint8_t reset[8] = {gs_usb::kModeReset, 0, 0, 0, 0, 0, 0, 0};
    try {
      impl_->transport->controlOut(gs_usb::kBreqMode, 0, 0, reset, 8);
    } catch (const CanFdError&) {
    }
    impl_->started = false;
  }
  impl_->transport->close();
  impl_->transport.reset();
}

bool CanFdBus::isOpen() const {
  return impl_->transport && impl_->transport->isOpen();
}

bool CanFdBus::isStarted() const { return impl_->started; }

bool CanFdBus::isFd() const { return impl_->is_fd; }

const DeviceInfo& CanFdBus::deviceInfo() const { return impl_->device_info; }

uint32_t CanFdBus::feature() const { return impl_->feature; }

uint32_t CanFdBus::clockFrequency() const { return impl_->fclk_can; }

const BitTiming& CanFdBus::nominalTiming() const { return impl_->nominal_timing; }

const BitTiming& CanFdBus::dataTiming() const { return impl_->data_timing; }

int CanFdBus::inputEndpoint() const {
  return impl_->transport ? static_cast<int>(impl_->transport->endpointIn()) : -1;
}

int CanFdBus::outputEndpoint() const {
  return impl_->transport ? static_cast<int>(impl_->transport->endpointOut()) : -1;
}

}  // namespace canfd
