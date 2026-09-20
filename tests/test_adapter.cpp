#include <cstdio>
#include <string>

#include "canfd/device.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

void testNames() {
  canfd::AdapterInfo a;
  a.vendor_id = 0xA8FA;
  a.product_id = 0x8598;
  a.manufacturer = "Com Equipment";
  a.product = "CANFD Analyser";
  a.serial = "F0802068387F4D4D";

  check(a.name() == "Com Equipment CANFD Analyser", "name is manufacturer + product");
  check(a.displayName() == "Com Equipment CANFD Analyser (A8FA:8598)",
        "displayName adds the USB ids");
  check(a.uniqueName() == "Com Equipment CANFD Analyser (A8FA:8598) SN:F0802068387F4D4D",
        "uniqueName adds the serial");

  canfd::AdapterInfo bare;
  bare.vendor_id = 0x0001;
  bare.product_id = 0x0002;
  check(bare.name() == "gs_usb 0001:0002", "name falls back to the USB ids");
  check(bare.uniqueName() == "gs_usb 0001:0002 (0001:0002)",
        "uniqueName without a serial equals displayName");
}

}  // namespace

int main() {
  testNames();
  if (failures == 0) {
    std::printf("test_adapter: OK\n");
  }
  return failures == 0 ? 0 : 1;
}
