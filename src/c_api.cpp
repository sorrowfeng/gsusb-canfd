#include "canfd/canfd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "canfd/canfd.hpp"
#include "canfd/version.hpp"

struct CanFdHandle {
  explicit CanFdHandle(const canfd::DeviceSelector& selector) : bus(selector) {}

  canfd::CanFdBus bus;
  CanFdReceiveCallback callback = nullptr;
  void* user = nullptr;
};

namespace {

thread_local std::string g_last_error;
thread_local int g_last_error_code = CANFD_ERRC_NONE;

void setError(const std::string& message, int code = CANFD_ERRC_BUS) {
  g_last_error = message;
  g_last_error_code = code;
}

/* Map a thrown exception onto a CANFD_ERRC_* code, so a C caller keeps the
   error class instead of only a message. */
void setException(const std::exception& exc) {
  int code = CANFD_ERRC_BUS;
  if (const auto* canfd_error = dynamic_cast<const canfd::CanFdError*>(&exc)) {
    switch (canfd_error->code()) {
      case canfd::ErrorCode::kNotFound:
        code = CANFD_ERRC_NOT_FOUND;
        break;
      case canfd::ErrorCode::kTimeout:
        code = CANFD_ERRC_TIMEOUT;
        break;
      case canfd::ErrorCode::kArgument:
        code = CANFD_ERRC_ARGUMENT;
        break;
      case canfd::ErrorCode::kBus:
      case canfd::ErrorCode::kNone:
      default:
        code = CANFD_ERRC_BUS;
        break;
    }
  }
  setError(exc.what(), code);
}

void copyString(char* dest, std::size_t size, const std::string& value) {
  std::snprintf(dest, size, "%s", value.c_str());
}

canfd::CanFrame toBusFrame(const CanFdFrame& in) {
  canfd::CanFrame frame;
  frame.id = in.id;
  frame.size = std::min<uint8_t>(in.size, CANFD_MAX_PAYLOAD);
  frame.extended = in.extended != 0;
  frame.fd = in.fd != 0;
  frame.brs = in.brs != 0;
  frame.remote = in.remote != 0;
  frame.error = in.error != 0;
  frame.channel = in.channel;
  for (uint8_t i = 0; i < frame.size; ++i) {
    frame.data[i] = in.data[i];
  }
  return frame;
}

void toCFrame(const canfd::CanFrame& in, CanFdFrame& out) {
  std::memset(&out, 0, sizeof(out));
  out.id = in.id;
  out.size = in.size;
  out.channel = in.channel;
  out.extended = in.extended ? 1 : 0;
  out.fd = in.fd ? 1 : 0;
  out.brs = in.brs ? 1 : 0;
  out.remote = in.remote ? 1 : 0;
  out.error = in.error ? 1 : 0;
  out.echo = in.echo ? 1 : 0;
  out.overflow = in.overflow ? 1 : 0;
  out.timestamp = in.timestamp;
  std::memcpy(out.data, in.data.data(), in.size);
}

}  // namespace

extern "C" {

const char* canfd_version(void) { return CANFD_VERSION; }

const char* canfd_last_error(void) { return g_last_error.c_str(); }

int canfd_last_error_code(void) { return g_last_error_code; }

void canfd_bus_config_default(CanFdBusConfig* config) {
  if (config == nullptr) {
    return;
  }
  const canfd::BusConfig defaults;
  config->bitrate = defaults.bitrate;
  config->sample_point = defaults.sample_point;
  config->data_bitrate = defaults.data_bitrate;
  config->data_sample_point = defaults.data_sample_point;
  config->fd = defaults.fd ? 1 : 0;
  config->listen_only = defaults.listen_only ? 1 : 0;
  config->loopback = defaults.loopback ? 1 : 0;
  config->one_shot = defaults.one_shot ? 1 : 0;
  config->hw_timestamp = defaults.hw_timestamp ? 1 : 0;
  config->drop_echo = defaults.drop_echo ? 1 : 0;
}

int canfd_scan(CanFdAdapterInfo* out, int max) {
  if (out == nullptr || max < 0) {
    setError("invalid argument", CANFD_ERRC_ARGUMENT);
    return CANFD_ERROR;
  }
  try {
    const auto adapters = canfd::scanAdapters(0, 0);
    const int count = std::min<int>(max, static_cast<int>(adapters.size()));
    for (int i = 0; i < count; ++i) {
      const auto& adapter = adapters[static_cast<std::size_t>(i)];
      std::memset(&out[i], 0, sizeof(out[i]));
      out[i].vendor_id = adapter.vendor_id;
      out[i].product_id = adapter.product_id;
      out[i].bus = adapter.bus;
      out[i].address = adapter.address;
      copyString(out[i].name, sizeof(out[i].name), adapter.name());
      copyString(out[i].display_name, sizeof(out[i].display_name), adapter.displayName());
      copyString(out[i].unique_name, sizeof(out[i].unique_name), adapter.uniqueName());
      copyString(out[i].manufacturer, sizeof(out[i].manufacturer), adapter.manufacturer);
      copyString(out[i].product, sizeof(out[i].product), adapter.product);
      copyString(out[i].serial, sizeof(out[i].serial), adapter.serial);
    }
    return count;
  } catch (const std::exception& exc) {
    setException(exc);
    return CANFD_ERROR;
  }
}

CanFdHandle* canfd_open_vid_pid(uint16_t vid, uint16_t pid, int index) {
  return canfd_open_channel(vid, pid, index, 0);
}

CanFdHandle* canfd_open_channel(uint16_t vid, uint16_t pid, int index, int channel) {
  try {
    canfd::DeviceSelector selector;
    selector.vid = vid;
    selector.pid = pid;
    selector.index = index;
    selector.channel = static_cast<uint8_t>(channel < 0 ? 0 : channel);
    auto* handle = new CanFdHandle(selector);
    handle->bus.open();
    return handle;
  } catch (const std::exception& exc) {
    setException(exc);
    return nullptr;
  }
}

CanFdHandle* canfd_open(int index) {
  return canfd_open_channel(canfd::kAnyVid, canfd::kAnyPid, index, 0);
}

int canfd_configure(CanFdHandle* handle, const CanFdBusConfig* config) {
  if (handle == nullptr || config == nullptr) {
    setError("invalid argument", CANFD_ERRC_ARGUMENT);
    return CANFD_ERROR;
  }
  try {
    canfd::BusConfig bus_config;
    bus_config.bitrate = config->bitrate;
    bus_config.sample_point = config->sample_point;
    bus_config.data_bitrate = config->data_bitrate;
    bus_config.data_sample_point = config->data_sample_point;
    bus_config.fd = config->fd != 0;
    bus_config.listen_only = config->listen_only != 0;
    bus_config.loopback = config->loopback != 0;
    bus_config.one_shot = config->one_shot != 0;
    bus_config.hw_timestamp = config->hw_timestamp != 0;
    bus_config.drop_echo = config->drop_echo != 0;
    handle->bus.configure(bus_config);
    return CANFD_OK;
  } catch (const std::exception& exc) {
    setException(exc);
    return CANFD_ERROR;
  }
}

int canfd_send(CanFdHandle* handle, const CanFdFrame* frame) {
  return canfd_send_echo(handle, frame, 0);
}

int canfd_send_echo(CanFdHandle* handle, const CanFdFrame* frame, int echo) {
  if (handle == nullptr || frame == nullptr) {
    setError("invalid argument", CANFD_ERRC_ARGUMENT);
    return CANFD_ERROR;
  }
  try {
    handle->bus.send(toBusFrame(*frame), echo != 0);
    return CANFD_OK;
  } catch (const std::exception& exc) {
    setException(exc);
    return CANFD_ERROR;
  }
}

int canfd_receive(CanFdHandle* handle, CanFdFrame* frame, int timeout_ms) {
  if (handle == nullptr || frame == nullptr) {
    setError("invalid argument", CANFD_ERRC_ARGUMENT);
    return CANFD_ERROR;
  }
  try {
    canfd::CanFrame bus_frame;
    if (!handle->bus.receive(bus_frame, std::chrono::milliseconds(timeout_ms))) {
      return CANFD_RECEIVE_TIMEOUT;
    }
    toCFrame(bus_frame, *frame);
    return CANFD_RECEIVE_FRAME;
  } catch (const std::exception& exc) {
    setException(exc);
    return CANFD_ERROR;
  }
}

int canfd_start(CanFdHandle* handle, CanFdReceiveCallback callback, void* user) {
  if (handle == nullptr || callback == nullptr) {
    setError("invalid argument", CANFD_ERRC_ARGUMENT);
    return CANFD_ERROR;
  }
  try {
    handle->callback = callback;
    handle->user = user;
    handle->bus.start([handle](const canfd::CanFrame& frame) {
      CanFdFrame c_frame;
      toCFrame(frame, c_frame);
      if (handle->callback != nullptr) {
        handle->callback(&c_frame, handle->user);
      }
    });
    return CANFD_OK;
  } catch (const std::exception& exc) {
    setException(exc);
    return CANFD_ERROR;
  }
}

void canfd_stop(CanFdHandle* handle) {
  if (handle == nullptr) {
    return;
  }
  try {
    handle->bus.stop();
  } catch (...) {
  }
}

void canfd_close(CanFdHandle* handle) {
  if (handle == nullptr) {
    return;
  }
  try {
    handle->bus.stop();
    handle->bus.close();
  } catch (...) {
  }
  delete handle;
}

int canfd_is_open(CanFdHandle* handle) {
  return handle != nullptr && handle->bus.isOpen() ? 1 : 0;
}

int canfd_is_started(CanFdHandle* handle) {
  return handle != nullptr && handle->bus.isStarted() ? 1 : 0;
}

int canfd_is_fd(CanFdHandle* handle) {
  return handle != nullptr && handle->bus.isFd() ? 1 : 0;
}

uint32_t canfd_feature(CanFdHandle* handle) {
  return handle != nullptr ? handle->bus.feature() : 0;
}

uint32_t canfd_clock_frequency(CanFdHandle* handle) {
  return handle != nullptr ? handle->bus.clockFrequency() : 0;
}

uint32_t canfd_channel_count(CanFdHandle* handle) {
  return handle != nullptr ? handle->bus.channelCount() : 0;
}

int canfd_channel(CanFdHandle* handle) {
  return handle != nullptr ? static_cast<int>(handle->bus.channel()) : -1;
}

int canfd_endpoints(CanFdHandle* handle, int* ep_in, int* ep_out) {
  if (handle == nullptr) {
    setError("invalid argument", CANFD_ERRC_ARGUMENT);
    return CANFD_ERROR;
  }
  if (ep_in != nullptr) {
    *ep_in = handle->bus.inputEndpoint();
  }
  if (ep_out != nullptr) {
    *ep_out = handle->bus.outputEndpoint();
  }
  return CANFD_OK;
}

}  // extern "C"
