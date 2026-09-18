// canfd_demo: end-to-end hardware demo for gsusb-canfd on macOS/Linux/Windows.
//
// It scans for an adapter, opens it, prints the negotiated bit timing, transmits
// a trigger frame on 0x501 and prints every response it sees (0x481 by default),
// skipping the adapter's own TX echo.
//
// Build:  cmake --build build --target canfd_demo
// Run:    ./build/canfd_demo --seconds 6
//         ./build/canfd_demo --monitor --seconds 5
//         ./build/canfd_demo --classic --id 123 --rx-id 456 --take-data 11223344

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "canfd/canfd.hpp"

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop.store(true); }

std::string featureString(uint32_t feature) {
  std::string out;
  auto add = [&](const char* name) {
    if (!out.empty()) {
      out += "|";
    }
    out += name;
  };
  if (feature & canfd::feature::kListenOnly) add("listen_only");
  if (feature & canfd::feature::kLoopback) add("loopback");
  if (feature & canfd::feature::kOneShot) add("one_shot");
  if (feature & canfd::feature::kHwTimestamp) add("hw_timestamp");
  if (feature & canfd::feature::kFd) add("fd");
  if (feature & canfd::feature::kBtConstExt) add("bt_const_ext");
  if (out.empty()) {
    out = "none";
  }
  return out;
}

double samplePointOf(const canfd::BitTiming& t) {
  const double total = 1.0 + t.tseg1() + t.phase_seg2;
  return static_cast<double>(1 + t.tseg1()) / total;
}

double bitrateOf(const canfd::BitTiming& t, uint32_t fclk) {
  const double total = 1.0 + t.tseg1() + t.phase_seg2;
  return static_cast<double>(fclk) / (static_cast<double>(t.brp) * total);
}

void printTiming(const char* label, const canfd::BitTiming& t, uint32_t fclk) {
  std::printf("  %-8s brp=%u tseg1=%u tseg2=%u sjw=%u  ->  %.0f bit/s @ %.1f%%\n", label,
              t.brp, t.tseg1(), t.phase_seg2, t.sjw, bitrateOf(t, fclk), samplePointOf(t) * 100.0);
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

canfd::CanFrame makeFrame(uint32_t id, const std::vector<uint8_t>& data, bool fd) {
  canfd::CanFrame f;
  f.id = id;
  f.extended = id > 0x7FF;
  f.fd = fd;
  f.brs = fd;
  f.size = static_cast<uint8_t>(data.size());
  for (std::size_t i = 0; i < data.size() && i < canfd::kMaxPayload; ++i) {
    f.data[i] = data[i];
  }
  return f;
}

struct Args {
  double seconds = 6.0;
  bool monitor = false;
  bool classic = false;
  bool trigger = true;
  canfd::BusConfig config;
  uint32_t trigger_id = 0x501;
  std::vector<uint8_t> trigger_data = {0x00, 0x02, 0x50, 0x01};
  uint32_t rx_id = 0x481;
  bool filter_rx_id = true;
  bool print_all = false;
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--seconds") {
      args.seconds = std::atof(next().c_str());
    } else if (a == "--monitor") {
      args.monitor = true;
      args.trigger = false;
    } else if (a == "--no-trigger") {
      args.trigger = false;
    } else if (a == "--classic") {
      args.classic = true;
      args.config.fd = false;
    } else if (a == "--bitrate") {
      args.config.bitrate = std::strtoul(next().c_str(), nullptr, 0);
    } else if (a == "--sample-point") {
      args.config.sample_point = std::atof(next().c_str());
    } else if (a == "--data-bitrate") {
      args.config.data_bitrate = std::strtoul(next().c_str(), nullptr, 0);
    } else if (a == "--data-sample-point") {
      args.config.data_sample_point = std::atof(next().c_str());
    } else if (a == "--id") {
      args.trigger_id = std::strtoul(next().c_str(), nullptr, 16);
    } else if (a == "--take-data") {
      args.trigger_data = parseData(next());
    } else if (a == "--rx-id") {
      args.rx_id = std::strtoul(next().c_str(), nullptr, 16);
    } else if (a == "--all") {
      args.filter_rx_id = false;
    } else if (a == "--all-frames") {
      args.print_all = true;
    } else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      std::exit(2);
    }
  }
  return args;
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parseArgs(argc, argv);
  std::signal(SIGINT, onSignal);

  std::printf("=== scan ===\n");
  for (const auto& a : canfd::scanAdapters(0, 0)) {
    std::printf("  %s | %s | %04X:%04X bus=%u addr=%u serial=%s\n", a.manufacturer.c_str(),
                a.product.c_str(), a.vendor_id, a.product_id, a.bus, a.address, a.serial.c_str());
  }

  canfd::CanFdBus bus;
  try {
    std::printf("=== open ===\n");
    bus.open();
    const auto& info = bus.deviceInfo();
    std::printf("  endpoints in=0x%02X out=0x%02X\n", bus.inputEndpoint(), bus.outputEndpoint());
    std::printf("  firmware=%.1f hardware=%.1f\n", info.sw_version / 10.0, info.hw_version / 10.0);
    std::printf("  fclk=%u Hz feature=0x%08X [%s]\n", bus.clockFrequency(), bus.feature(),
                featureString(bus.feature()).c_str());

    std::printf("=== configure (%s) ===\n", args.classic ? "classic CAN" : "CAN FD");
    bus.configure(args.config);
    printTiming("nominal", bus.nominalTiming(), bus.clockFrequency());
    if (bus.isFd()) {
      printTiming("data", bus.dataTiming(), bus.clockFrequency());
    }

    if (args.trigger) {
      std::printf("=== tx trigger ===\n");
      bus.send(makeFrame(args.trigger_id, args.trigger_data, args.config.fd));
      std::printf("  sent %03X [%zu] ", args.trigger_id, args.trigger_data.size());
      for (uint8_t b : args.trigger_data) {
        std::printf("%02X ", b);
      }
      std::printf("\n");
    } else {
      std::printf("=== passive monitor for %.1fs ===\n", args.seconds);
    }

    std::printf("=== rx for %.1fs (echo filtered, showing %s) ===\n", args.seconds,
                args.filter_rx_id ? "0x481" : "all ids");
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(args.seconds);
    auto last_trigger = std::chrono::steady_clock::now();
    auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    auto window_start = std::chrono::steady_clock::now();

    canfd::CanFrame frame;
    std::map<uint32_t, unsigned> counts;
    std::vector<uint8_t> last_payload;
    bool have_last = false;
    unsigned total = 0;
    unsigned window = 0;
    while (!g_stop.load() && std::chrono::steady_clock::now() < deadline) {
      const auto now = std::chrono::steady_clock::now();
      if (args.trigger && now - last_trigger >= std::chrono::seconds(1)) {
        bus.send(makeFrame(args.trigger_id, args.trigger_data, args.config.fd));
        last_trigger = now;
      }

      if (!bus.receive(frame, std::chrono::milliseconds(100))) {
        continue;
      }
      if (frame.echo) {
        continue;
      }
      if (args.filter_rx_id && frame.id != args.rx_id) {
        continue;
      }
      ++counts[frame.id];
      ++total;
      ++window;

      const bool changed =
          !have_last || frame.size != last_payload.size() ||
          !std::equal(frame.data.begin(), frame.data.begin() + frame.size, last_payload.begin());
      if (args.print_all || changed) {
        std::printf("  %s\n", frame.toString().c_str());
        last_payload.assign(frame.data.begin(), frame.data.begin() + frame.size);
        have_last = true;
      }

      if (now >= next_report) {
        const double elapsed =
            std::chrono::duration<double>(now - window_start).count();
        std::printf("--- %u frames in %.1fs (%.0f fps) ---\n", window, elapsed,
                    elapsed > 0.0 ? window / elapsed : 0.0);
        window = 0;
        window_start = now;
        next_report = now + std::chrono::seconds(2);
      }
    }

    std::printf("=== summary: %u frames ===\n", total);
    for (const auto& entry : counts) {
      std::printf("  id %03X : %u\n", entry.first, entry.second);
    }
  } catch (const std::exception& exc) {
    std::fprintf(stderr, "error: %s\n", exc.what());
    bus.close();
    return 1;
  }

  bus.close();
  return 0;
}
