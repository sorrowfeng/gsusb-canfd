#include "canfd/canfd_c.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "canfd/canfd.hpp"

struct CanFdHandle {
  explicit CanFdHandle(int index) {
    selector.index = index;
    bus = std::make_unique<canfd::CanFdBus>(selector);
  }

  canfd::DeviceSelector selector;
  std::unique_ptr<canfd::CanFdBus> bus;
};

namespace {

thread_local std::string g_last_error;

void setError(const std::string& message) { g_last_error = message; }

canfd::CanFrame toFrame(const CanFdMsg& msg) {
  canfd::CanFrame frame;
  frame.id = msg.id;
  frame.channel = msg.channel;
  frame.size = msg.size > CANFD_MAX_DATA ? CANFD_MAX_DATA : msg.size;
  frame.fd = (msg.flags & CANFD_FLAG_FD) != 0;
  frame.brs = (msg.flags & CANFD_FLAG_BRS) != 0;
  frame.extended = (msg.flags & CANFD_FLAG_EXTENDED) != 0;
  frame.remote = (msg.flags & CANFD_FLAG_REMOTE) != 0;
  for (uint8_t i = 0; i < frame.size; ++i) {
    frame.data[i] = msg.data[i];
  }
  return frame;
}

void fromFrame(const canfd::CanFrame& frame, CanFdMsg& msg) {
  std::memset(&msg, 0, sizeof(msg));
  msg.id = frame.id;
  msg.timestamp_us = static_cast<uint32_t>(frame.timestamp * 1'000'000.0);
  msg.channel = frame.channel;
  msg.size = frame.size;
  msg.flags = 0;
  if (frame.fd) {
    msg.flags |= CANFD_FLAG_FD;
  }
  if (frame.brs) {
    msg.flags |= CANFD_FLAG_BRS;
  }
  if (frame.extended) {
    msg.flags |= CANFD_FLAG_EXTENDED;
  }
  if (frame.remote) {
    msg.flags |= CANFD_FLAG_REMOTE;
  }
  if (frame.echo) {
    msg.flags |= CANFD_FLAG_ECHO;
  }
  if (frame.error) {
    msg.flags |= CANFD_FLAG_ERROR;
  }
  std::memcpy(msg.data, frame.data.data(), frame.size);
}

}  // namespace

extern "C" {

int canfd_scan(void) {
  try {
    const auto adapters = canfd::scanAdapters(0, 0);
    return static_cast<int>(adapters.size());
  } catch (const std::exception& exc) {
    setError(exc.what());
    return -1;
  }
}

int canfd_scan_info(CanFdAdapterInfo* out, int max) {
  if (out == nullptr || max <= 0) {
    setError("invalid argument");
    return CANFD_ERR;
  }
  try {
    const auto adapters = canfd::scanAdapters(0, 0);
    const int count =
        std::min<int>(max, static_cast<int>(adapters.size()));
    for (int i = 0; i < count; ++i) {
      const auto& a = adapters[static_cast<std::size_t>(i)];
      std::memset(&out[i], 0, sizeof(out[i]));
      std::snprintf(out[i].name, sizeof(out[i].name), "%s", a.name().c_str());
      std::snprintf(out[i].manufacturer, sizeof(out[i].manufacturer), "%s",
                    a.manufacturer.c_str());
      std::snprintf(out[i].product, sizeof(out[i].product), "%s", a.product.c_str());
      std::snprintf(out[i].serial, sizeof(out[i].serial), "%s", a.serial.c_str());
      out[i].vendor_id = a.vendor_id;
      out[i].product_id = a.product_id;
      out[i].bus = a.bus;
      out[i].address = a.address;
    }
    return count;
  } catch (const std::exception& exc) {
    setError(exc.what());
    return -1;
  }
}

CanFdHandle* canfd_open(int index) {
  try {
    auto* handle = new CanFdHandle(index);
    handle->bus->open();
    return handle;
  } catch (const std::exception& exc) {
    setError(exc.what());
    return nullptr;
  }
}

int canfd_configure(CanFdHandle* handle, uint32_t bitrate, double sample_point,
                    uint32_t data_bitrate, double data_sample_point, int fd) {
  if (handle == nullptr || !handle->bus) {
    setError("invalid handle");
    return CANFD_ERR;
  }
  try {
    canfd::BusConfig config;
    config.bitrate = bitrate;
    config.sample_point = sample_point;
    config.data_bitrate = data_bitrate;
    config.data_sample_point = data_sample_point;
    config.fd = fd != 0;
    handle->bus->configure(config);
    return CANFD_OK;
  } catch (const std::exception& exc) {
    setError(exc.what());
    return CANFD_ERR;
  }
}

int canfd_transmit(CanFdHandle* handle, const CanFdMsg* msg) {
  if (handle == nullptr || !handle->bus || msg == nullptr) {
    setError("invalid argument");
    return CANFD_ERR;
  }
  try {
    handle->bus->send(toFrame(*msg));
    return CANFD_OK;
  } catch (const std::exception& exc) {
    setError(exc.what());
    return CANFD_ERR;
  }
}

int canfd_receive(CanFdHandle* handle, CanFdMsg* msg, int timeout_ms) {
  if (handle == nullptr || !handle->bus || msg == nullptr) {
    setError("invalid argument");
    return CANFD_ERR;
  }
  try {
    canfd::CanFrame frame;
    if (!handle->bus->receive(frame, std::chrono::milliseconds(timeout_ms))) {
      return CANFD_TIMEOUT;
    }
    fromFrame(frame, *msg);
    return CANFD_OK;
  } catch (const std::exception& exc) {
    setError(exc.what());
    return CANFD_ERR;
  }
}

void canfd_close(CanFdHandle* handle) {
  if (handle == nullptr) {
    return;
  }
  try {
    handle->bus->close();
  } catch (...) {
  }
  delete handle;
}

const char* canfd_last_error(void) { return g_last_error.c_str(); }

}  // extern "C"
