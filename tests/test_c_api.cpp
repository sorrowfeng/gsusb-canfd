#include <cstdio>

#include "canfd/canfd_c.h"

int main() {
  int failures = 0;

  CanFdMsg msg;
  if (canfd_receive(nullptr, &msg, 10) != CANFD_ERR) {
    std::fprintf(stderr, "FAIL: null handle should return CANFD_ERR\n");
    ++failures;
  }
  if (canfd_transmit(nullptr, &msg) != CANFD_ERR) {
    std::fprintf(stderr, "FAIL: null transmit should return CANFD_ERR\n");
    ++failures;
  }
  if (canfd_scan_info(nullptr, 4) != CANFD_ERR) {
    std::fprintf(stderr, "FAIL: null scan info should return CANFD_ERR\n");
    ++failures;
  }
  canfd_close(nullptr);

  const char* error = canfd_last_error();
  if (error == nullptr) {
    std::fprintf(stderr, "FAIL: last error should never be null\n");
    ++failures;
  }

  if (failures == 0) {
    std::printf("test_c_api: OK\n");
  }
  return failures == 0 ? 0 : 1;
}
