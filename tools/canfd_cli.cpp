#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "canfd/canfd.hpp"

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop.store(true); }

uint32_t parseId(const std::string& text) {
  return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
}

std::vector<uint8_t> parseData(const std::string& text) {
  std::string cleaned;
  for (char c : text) {
    if (std::isxdigit(static_cast<unsigned char>(c))) {
      cleaned += c;
    }
  }
  std::vector<uint8_t> out;
  for (std::size_t i = 0; i + 1 < cleaned.size(); i += 2) {
    out.push_back(static_cast<uint8_t>(std::strtoul(cleaned.substr(i, 2).c_str(), nullptr, 16)));
  }
  return out;
}

struct Options {
  uint16_t vid = canfd::kAnyVid;
  uint16_t pid = canfd::kAnyPid;
  int index = 0;
  uint8_t channel = 0;
  canfd::BusConfig config;
  uint32_t id = 0;
  std::vector<uint8_t> data;
  uint32_t trigger_id = 0x501;
  std::vector<uint8_t> trigger_data = {0x00, 0x02, 0x50, 0x01};
  bool trigger = false;
  bool show_echo = false;
  int count = 0;
};

std::string usage() {
  return "usage:\n"
         "  canfd list [--vid V] [--pid P]\n"
         "  canfd send [--vid V] [--pid P] [--index N] [--channel C] [--classic] ID [HEXDATA]\n"
         "  canfd monitor [--vid V] [--pid P] [--index N] [--channel C] [--classic]\n"
         "                [--bitrate B] [--sample-point S]\n"
         "                [--data-bitrate B] [--data-sample-point S]\n"
         "                [--trigger] [--trigger-id ID] [--trigger-data HEX]\n"
         "                [--show-echo] [--count N]\n"
         "  --vid/--pid default to 0 (any gs_usb adapter)\n";
}

bool parseArgs(int argc, char** argv, Options& opts) {
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--vid") {
      opts.vid = static_cast<uint16_t>(std::strtoul(next("--vid").c_str(), nullptr, 0));
    } else if (arg == "--pid") {
      opts.pid = static_cast<uint16_t>(std::strtoul(next("--pid").c_str(), nullptr, 0));
    } else if (arg == "--index") {
      opts.index = std::atoi(next("--index").c_str());
    } else if (arg == "--channel") {
      opts.channel = static_cast<uint8_t>(std::atoi(next("--channel").c_str()));
    } else if (arg == "--bitrate") {
      opts.config.bitrate = std::strtoul(next("--bitrate").c_str(), nullptr, 0);
    } else if (arg == "--sample-point") {
      opts.config.sample_point = std::atof(next("--sample-point").c_str());
    } else if (arg == "--data-bitrate") {
      opts.config.data_bitrate = std::strtoul(next("--data-bitrate").c_str(), nullptr, 0);
    } else if (arg == "--data-sample-point") {
      opts.config.data_sample_point = std::atof(next("--data-sample-point").c_str());
    } else if (arg == "--classic") {
      opts.config.fd = false;
    } else if (arg == "--trigger") {
      opts.trigger = true;
    } else if (arg == "--show-echo") {
      opts.show_echo = true;
    } else if (arg == "--trigger-id") {
      opts.trigger_id = parseId(next("--trigger-id"));
    } else if (arg == "--trigger-data") {
      opts.trigger_data = parseData(next("--trigger-data"));
    } else if (arg == "--count") {
      opts.count = std::atoi(next("--count").c_str());
    } else if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
      std::fprintf(stderr, "unknown option %s\n", arg.c_str());
      return false;
    } else if (opts.id == 0) {
      opts.id = parseId(arg);
    } else if (opts.data.empty()) {
      opts.data = parseData(arg);
    }
  }
  return true;
}

canfd::CanFrame makeFrame(uint32_t id, const std::vector<uint8_t>& data, bool fd) {
  canfd::CanFrame frame;
  frame.id = id;
  frame.extended = id > 0x7FF;
  frame.fd = fd;
  frame.brs = fd;
  frame.size = static_cast<uint8_t>(data.size());
  for (std::size_t i = 0; i < data.size(); ++i) {
    frame.data[i] = data[i];
  }
  return frame;
}

int listCommand() {
  const auto adapters = canfd::scanAdapters(0, 0);
  if (adapters.empty()) {
    std::printf("no gs_usb compatible devices found\n");
    return 1;
  }
  for (std::size_t i = 0; i < adapters.size(); ++i) {
    const auto& a = adapters[i];
    std::printf("[%zu] %s\n", i, a.name().c_str());
    std::printf("     vid:pid=%04X:%04X bus=%u addr=%u serial=%s\n", a.vendor_id, a.product_id,
                a.bus, a.address, a.serial.empty() ? "-" : a.serial.c_str());
  }
  return 0;
}

void printBusInfo(const canfd::CanFdBus& bus) {
  std::printf("opened gs_usb device (in=0x%02X, out=0x%02X)\n", bus.inputEndpoint(),
              bus.outputEndpoint());
  const auto& info = bus.deviceInfo();
  std::printf("fclk_can=%u Hz, feature=0x%08X, fd=%d, fw=%.1f hw=%.1f\n", bus.clockFrequency(),
              bus.feature(), bus.isFd() ? 1 : 0, info.sw_version / 10.0, info.hw_version / 10.0);
}

int run(bool monitor, Options& opts) {
  canfd::DeviceSelector selector;
  selector.vid = opts.vid;
  selector.pid = opts.pid;
  selector.index = opts.index;
  selector.channel = opts.channel;

  canfd::CanFdBus bus(selector);
  bus.open();
  bus.configure(opts.config);
  printBusInfo(bus);

  if (!monitor) {
    bus.send(makeFrame(opts.id, opts.data, opts.config.fd));
    std::printf("sent %03X [%zu]\n", opts.id, opts.data.size());
    bus.close();
    return 0;
  }

  if (opts.trigger) {
    // --show-echo is what makes the trigger's own loopback visible, so the tag
    // is only requested when the user asked to see echoes.
    bus.send(makeFrame(opts.trigger_id, opts.trigger_data, opts.config.fd), opts.show_echo);
    std::printf("trigger: sent %03X\n", opts.trigger_id);
  }

  std::signal(SIGINT, onSignal);
  int printed = 0;
  canfd::CanFrame frame;
  while (!g_stop.load()) {
    if (!bus.receive(frame, std::chrono::milliseconds(100))) {
      continue;
    }
    if (frame.echo && !opts.show_echo) {
      continue;
    }
    std::printf("%s  %s\n", frame.echo ? "ec" : "RX", frame.toString().c_str());
    ++printed;
    if (opts.count > 0 && printed >= opts.count) {
      break;
    }
  }
  bus.close();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fputs(usage().c_str(), stderr);
    return 2;
  }

  const std::string command = argv[1];
  if (command == "list") {
    return listCommand();
  }
  if (command == "send" || command == "monitor") {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
      return 2;
    }
    try {
      return run(command == "monitor", opts);
    } catch (const std::exception& exc) {
      std::fprintf(stderr, "error: %s\n", exc.what());
      return 1;
    }
  }

  std::fputs(usage().c_str(), stderr);
  return 2;
}
