#ifndef CANFD_USB_TRANSPORT_HPP
#define CANFD_USB_TRANSPORT_HPP

#include <cstdint>
#include <string>

#include "canfd/device.hpp"

struct libusb_context;
struct libusb_device_handle;

namespace canfd {

class UsbTransport {
 public:
  explicit UsbTransport(const DeviceSelector& selector);
  ~UsbTransport();

  UsbTransport(const UsbTransport&) = delete;
  UsbTransport& operator=(const UsbTransport&) = delete;

  void open();
  void close();
  bool isOpen() const { return handle_ != nullptr; }

  int controlIn(uint8_t request, uint16_t value, uint16_t index, uint8_t* data,
                uint16_t size);
  int controlOut(uint8_t request, uint16_t value, uint16_t index, const uint8_t* data,
                 uint16_t size);

  int bulkWrite(const uint8_t* data, int size, unsigned timeout_ms);
  int bulkRead(uint8_t* data, int size, unsigned timeout_ms);

  uint8_t endpointIn() const { return ep_in_; }
  uint8_t endpointOut() const { return ep_out_; }
  int interfaceNumber() const { return interface_number_; }

 private:
  DeviceSelector selector_;
  libusb_device_handle* handle_ = nullptr;
  uint8_t ep_in_ = 0;
  uint8_t ep_out_ = 0;
  int interface_number_ = -1;
  bool interface_claimed_ = false;
};

}  // namespace canfd

#endif  // CANFD_USB_TRANSPORT_HPP
