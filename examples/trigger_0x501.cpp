#include <chrono>
#include <cstdio>
#include <thread>

#include "canfd/canfd.hpp"

namespace {

constexpr uint32_t kBitrate = 1'000'000;
constexpr double kSamplePoint = 0.80;
constexpr uint32_t kDataBitrate = 5'000'000;
constexpr double kDataSamplePoint = 0.75;

constexpr uint32_t kTriggerId = 0x501;
constexpr uint32_t kResponseId = 0x481;

}  // namespace

int main() {
  canfd::CanFdBus bus;
  try {
    bus.open();

    canfd::BusConfig config;
    config.bitrate = kBitrate;
    config.sample_point = kSamplePoint;
    config.data_bitrate = kDataBitrate;
    config.data_sample_point = kDataSamplePoint;
    config.fd = true;
    bus.configure(config);
    std::printf("bus up: 1M/80%% + 5M/75%% CAN FD\n");

    canfd::CanFrame trigger;
    trigger.id = kTriggerId;
    trigger.fd = true;
    trigger.brs = true;
    trigger.size = 4;
    trigger.data[0] = 0x00;
    trigger.data[1] = 0x02;
    trigger.data[2] = 0x50;
    trigger.data[3] = 0x01;
    bus.send(trigger);

    auto last_repeat = std::chrono::steady_clock::now();
    canfd::CanFrame frame;
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (now - last_repeat >= std::chrono::seconds(1)) {
        bus.send(trigger);
        last_repeat = now;
      }

      if (!bus.receive(frame, std::chrono::milliseconds(200))) {
        continue;
      }
      if (frame.echo || frame.id != kResponseId) {
        continue;
      }
      std::printf("%s\n", frame.toString().c_str());
    }
  } catch (const std::exception& exc) {
    std::fprintf(stderr, "error: %s\n", exc.what());
    bus.close();
    return 1;
  }
  return 0;
}
