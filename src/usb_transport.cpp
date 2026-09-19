#include "usb_transport.hpp"

#include <libusb.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "canfd/bit_timing.hpp"
#include "gs_usb_protocol.hpp"

namespace canfd {

namespace {

libusb_context* globalContext() {
  static libusb_context* context = []() -> libusb_context* {
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
      return nullptr;
    }
    return ctx;
  }();
  return context;
}

struct FoundDevice {
  libusb_device* device = nullptr;
  AdapterInfo info;
};

std::string readString(libusb_device_handle* handle, uint8_t index) {
  if (handle == nullptr || index == 0) {
    return {};
  }
  unsigned char buffer[256] = {0};
  const int rc = libusb_get_string_descriptor_ascii(handle, index, buffer, sizeof(buffer));
  if (rc <= 0) {
    return {};
  }
  return std::string(reinterpret_cast<char*>(buffer), static_cast<std::size_t>(rc));
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// gs_usb adapters expose a vendor-specific bulk interface (the class the
// reference firmware uses for the CAN endpoints). Reading the config descriptor
// does not require opening the device, so this works even while another handle
// has the adapter claimed.
bool hasVendorBulkInterface(libusb_device* device) {
  libusb_config_descriptor* config = nullptr;
  if (libusb_get_active_config_descriptor(device, &config) != 0 || config == nullptr) {
    return false;
  }

  bool found = false;
  for (uint8_t i = 0; i < config->bNumInterfaces && !found; ++i) {
    const libusb_interface& intf = config->interface[i];
    for (int a = 0; a < intf.num_altsetting && !found; ++a) {
      const libusb_interface_descriptor& alt = intf.altsetting[a];
      if (alt.bInterfaceClass != 0xFF) {
        continue;
      }
      bool has_in = false;
      bool has_out = false;
      for (uint8_t e = 0; e < alt.bNumEndpoints; ++e) {
        const libusb_endpoint_descriptor& ep = alt.endpoint[e];
        if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
          continue;
        }
        if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
          has_in = true;
        } else {
          has_out = true;
        }
      }
      if (has_in && has_out) {
        found = true;
      }
    }
  }

  libusb_free_config_descriptor(config);
  return found;
}

bool isKnownGsUsbId(uint16_t vid, uint16_t pid) {
  static const uint16_t kKnown[][2] = {
      {0x1D50, 0x606F},  // Geschwister Schneider / candleLight
      {0x1209, 0x2323},  // candleLight
      {0x1CD2, 0x606F},  // CES CANext FD
      {0x16D0, 0x10B8},  // ABE CAN Debugger FD
      {0x1209, 0xCA01},  // Cannectivity
      {0xA8FA, 0x8598},  // Com Equipment CANFD Analyser
  };
  for (const auto& known : kKnown) {
    if (known[0] == vid && known[1] == pid) {
      return true;
    }
  }
  return false;
}

bool looksLikeGsUsb(const AdapterInfo& info, libusb_device* device) {
  if (isKnownGsUsbId(info.vendor_id, info.product_id)) {
    return true;
  }

  const bool have_descriptors = !info.manufacturer.empty() || !info.product.empty();
  if (have_descriptors) {
    const std::string text = toLower(info.manufacturer + " " + info.product);
    for (const char* hint : {"can", "candle", "gs_usb", "gs-usb"}) {
      if (text.find(hint) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  // String descriptors are unavailable -- either the device is already opened
  // and claimed by another handle (libusb_open fails, so nothing can be read),
  // or the adapter does not provide them at all. Falling back to the interface
  // shape keeps a busy adapter discoverable without matching unrelated USB
  // devices, which is what a blanket "no descriptors -> accept" would do.
  return hasVendorBulkInterface(device);
}

std::vector<FoundDevice> findDevices(libusb_context* context, const DeviceSelector& selector,
                                     bool useHeuristics) {
  std::vector<FoundDevice> result;

  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(context, &list);
  if (count < 0) {
    throw CanFdError("libusb_get_device_list failed");
  }

  for (ssize_t i = 0; i < count; ++i) {
    libusb_device* device = list[i];
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(device, &desc) != 0) {
      continue;
    }
    if (selector.vid != 0 && desc.idVendor != selector.vid) {
      continue;
    }
    if (selector.pid != 0 && desc.idProduct != selector.pid) {
      continue;
    }

    AdapterInfo info;
    info.vendor_id = desc.idVendor;
    info.product_id = desc.idProduct;
    info.bus = libusb_get_bus_number(device);
    info.address = libusb_get_device_address(device);

    libusb_device_handle* handle = nullptr;
    if (libusb_open(device, &handle) == 0) {
      info.manufacturer = readString(handle, desc.iManufacturer);
      info.product = readString(handle, desc.iProduct);
      info.serial = readString(handle, desc.iSerialNumber);
      libusb_close(handle);
    }

    if (selector.serial.has_value() && info.serial != *selector.serial) {
      continue;
    }
    if (selector.product.has_value() &&
        toLower(info.product).find(toLower(*selector.product)) == std::string::npos) {
      continue;
    }
    if (useHeuristics && !looksLikeGsUsb(info, device)) {
      continue;
    }

    result.push_back(FoundDevice{libusb_ref_device(device), info});
  }

  libusb_free_device_list(list, 0);
  return result;
}

void freeFound(std::vector<FoundDevice>& devices) {
  for (auto& found : devices) {
    if (found.device != nullptr) {
      libusb_unref_device(found.device);
      found.device = nullptr;
    }
  }
}

bool findBulkInterface(libusb_device* device, libusb_device_handle* handle, uint8_t* ep_in,
                       uint8_t* ep_out, int* interface_number) {
  libusb_config_descriptor* config = nullptr;
  if (libusb_get_active_config_descriptor(device, &config) != 0) {
    if (handle != nullptr && libusb_set_configuration(handle, 1) == 0) {
      libusb_get_active_config_descriptor(device, &config);
    }
  }
  if (config == nullptr) {
    return false;
  }

  bool found = false;
  for (int pass = 0; pass < 2 && !found; ++pass) {
    for (uint8_t i = 0; i < config->bNumInterfaces && !found; ++i) {
      const libusb_interface& intf = config->interface[i];
      for (int a = 0; a < intf.num_altsetting && !found; ++a) {
        const libusb_interface_descriptor& alt = intf.altsetting[a];
        if (pass == 0 && alt.bInterfaceClass != 0xFF) {
          continue;
        }
        uint8_t in = 0;
        uint8_t out = 0;
        bool has_in = false;
        bool has_out = false;
        for (uint8_t e = 0; e < alt.bNumEndpoints; ++e) {
          const libusb_endpoint_descriptor& ep = alt.endpoint[e];
          if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
            continue;
          }
          if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
            if (!has_in) {
              in = ep.bEndpointAddress;
              has_in = true;
            }
          } else if (!has_out) {
            out = ep.bEndpointAddress;
            has_out = true;
          }
        }
        if (has_in && has_out) {
          *ep_in = in;
          *ep_out = out;
          *interface_number = alt.bInterfaceNumber;
          found = true;
        }
      }
    }
  }

  libusb_free_config_descriptor(config);
  return found;
}

}  // namespace

std::vector<AdapterInfo> scanAdapters(uint16_t vid, uint16_t pid) {
  libusb_context* context = globalContext();
  if (context == nullptr) {
    throw CanFdError("libusb_init failed");
  }

  DeviceSelector selector;
  selector.vid = vid;
  selector.pid = pid;

  std::vector<AdapterInfo> adapters;
  auto found = findDevices(context, selector, vid == 0 && pid == 0);
  adapters.reserve(found.size());
  for (const auto& device : found) {
    adapters.push_back(device.info);
  }
  freeFound(found);
  return adapters;
}

UsbTransport::UsbTransport(const DeviceSelector& selector) : selector_(selector) {}

UsbTransport::~UsbTransport() { close(); }

void UsbTransport::open() {
  if (handle_ != nullptr) {
    return;
  }
  libusb_context* context = globalContext();
  if (context == nullptr) {
    throw CanFdError("libusb_init failed");
  }

  auto found = findDevices(context, selector_, selector_.vid == 0 && selector_.pid == 0);
  if (found.empty()) {
    throw CanFdError("no gs_usb device found");
  }
  if (selector_.index < 0 || static_cast<std::size_t>(selector_.index) >= found.size()) {
    const std::size_t available = found.size();
    freeFound(found);
    throw CanFdError("device index out of range (" + std::to_string(available) + " found)");
  }

  libusb_device* device = found[static_cast<std::size_t>(selector_.index)].device;
  const int rc = libusb_open(device, &handle_);
  freeFound(found);
  if (rc != 0) {
    handle_ = nullptr;
    throw CanFdError("libusb_open failed: " + std::string(libusb_error_name(rc)));
  }

#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x01000102
  libusb_set_auto_detach_kernel_driver(handle_, 1);
#endif

  int interface_number = -1;
  if (!findBulkInterface(device, handle_, &ep_in_, &ep_out_, &interface_number)) {
    close();
    throw CanFdError("no bulk endpoints found on device");
  }
  interface_number_ = interface_number;

  if (libusb_claim_interface(handle_, interface_number_) != 0) {
    close();
    throw CanFdError("failed to claim USB interface");
  }
  interface_claimed_ = true;
}

void UsbTransport::close() {
  if (handle_ != nullptr) {
    if (interface_claimed_) {
      libusb_release_interface(handle_, interface_number_);
      interface_claimed_ = false;
    }
    libusb_close(handle_);
    handle_ = nullptr;
  }
}

int UsbTransport::controlIn(uint8_t request, uint16_t value, uint16_t index, uint8_t* data,
                            uint16_t size) {
  const int rc = libusb_control_transfer(handle_, gs_usb::kCtrlIn, request, value, index, data,
                                         size, 1000);
  if (rc < 0) {
    throw CanFdError("control request " + std::to_string(request) +
                     " failed: " + std::string(libusb_error_name(rc)));
  }
  return rc;
}

int UsbTransport::controlOut(uint8_t request, uint16_t value, uint16_t index,
                             const uint8_t* data, uint16_t size) {
  const int rc = libusb_control_transfer(handle_, gs_usb::kCtrlOut, request, value, index,
                                         const_cast<uint8_t*>(data), size, 1000);
  if (rc < 0) {
    throw CanFdError("control request " + std::to_string(request) +
                     " failed: " + std::string(libusb_error_name(rc)));
  }
  return rc;
}

int UsbTransport::bulkWrite(const uint8_t* data, int size, unsigned timeout_ms) {
  int transferred = 0;
  const int rc = libusb_bulk_transfer(handle_, ep_out_, const_cast<uint8_t*>(data), size,
                                      &transferred, timeout_ms);
  if (rc != 0) {
    throw CanFdError("bulk write failed: " + std::string(libusb_error_name(rc)));
  }
  return transferred;
}

int UsbTransport::bulkRead(uint8_t* data, int size, unsigned timeout_ms) {
  int transferred = 0;
  const int rc = libusb_bulk_transfer(handle_, ep_in_, data, size, &transferred, timeout_ms);
  if (rc == LIBUSB_ERROR_TIMEOUT) {
    return -1;
  }
  if (rc != 0) {
    throw CanFdError("bulk read failed: " + std::string(libusb_error_name(rc)));
  }
  return transferred;
}

}  // namespace canfd
