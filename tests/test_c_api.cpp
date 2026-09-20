#include <cstdio>
#include <cstring>

#include "canfd/canfd.h"
#include "canfd/version.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

}  // namespace

int main() {
  check(canfd_version() != nullptr && canfd_version()[0] != '\0', "version is set");
  check(std::strcmp(canfd_version(), CANFD_VERSION) == 0, "C ABI version matches header");
  check(canfd_last_error() != nullptr, "last error is never null");

  CanFdBusConfig config;
  std::memset(&config, 0, sizeof(config));
  canfd_bus_config_default(&config);
  check(config.bitrate == 1'000'000, "default bitrate");
  check(config.data_bitrate == 5'000'000, "default data bitrate");
  check(config.fd == 1, "default fd");
  check(config.hw_timestamp == 1, "default hw_timestamp");
  check(config.drop_echo == 0, "default drop_echo is off");

  CanFdAdapterInfo adapters[1];
  check(canfd_scan(nullptr, 1) == CANFD_ERROR, "null scan is rejected");
  check(canfd_scan(adapters, -1) == CANFD_ERROR, "negative max is rejected");

  CanFdFrame frame;
  std::memset(&frame, 0, sizeof(frame));
  check(canfd_configure(nullptr, &config) == CANFD_ERROR, "null configure is rejected");
  check(canfd_configure(reinterpret_cast<CanFdHandle*>(1), nullptr) == CANFD_ERROR,
        "null config is rejected");
  check(canfd_send(nullptr, &frame) == CANFD_ERROR, "null send is rejected");
  check(canfd_send_echo(nullptr, &frame, 1) == CANFD_ERROR, "null send_echo is rejected");
  check(canfd_send_echo(reinterpret_cast<CanFdHandle*>(1), nullptr, 0) == CANFD_ERROR,
        "null send_echo frame is rejected");
  check(canfd_receive(nullptr, &frame, 10) == CANFD_ERROR, "null receive is rejected");
  check(canfd_start(nullptr, nullptr, nullptr) == CANFD_ERROR, "null start is rejected");

  check(canfd_is_open(nullptr) == 0, "is_open(null)");
  check(canfd_is_started(nullptr) == 0, "is_started(null)");
  check(canfd_is_fd(nullptr) == 0, "is_fd(null)");
  check(canfd_feature(nullptr) == 0, "feature(null)");
  check(canfd_clock_frequency(nullptr) == 0, "clock(null)");
  check(canfd_channel_count(nullptr) == 0, "channel_count(null)");
  check(canfd_channel(nullptr) == -1, "channel(null)");
  check(canfd_endpoints(nullptr, nullptr, nullptr) == CANFD_ERROR, "endpoints(null)");

  canfd_stop(nullptr);
  canfd_close(nullptr);

  if (failures == 0) {
    std::printf("test_c_api: OK\n");
  }
  return failures == 0 ? 0 : 1;
}
