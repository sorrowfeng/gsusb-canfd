// canfd_term: interactive CAN/CAN FD terminal.
//
// On start it scans for gs_usb adapters and lets you pick one, then opens it
// and prints every received frame in real time from a background thread while
// you type your own IDs and payloads to transmit.
//
// Build:  cmake --build build --target canfd_term
// Run:    ./build/canfd_term
//         ./build/canfd_term --classic
//         ./build/canfd_term --bitrate 500000 --data-bitrate 2000000
//         ./build/canfd_term --index 1        # skip the picker
//
// Commands (type "help" at the prompt):
//   send <id> [hexbytes]      transmit; id may be standard or extended
//   send ext <id> [hexbytes]  force extended frame
//   send ext:1ABCDEF 1122     "ext:" prefix also forces extended
//   id <hex> | data <hex>     set defaults used by a bare "send"
//   scan                      list adapters again
//   open <index>              switch to another adapter
//   fd on|off                 CAN FD toggle
//   brs on|off                bit-rate switch toggle (FD only)
//   echo on|off               show our own TX echoes
//   filter <id...>|off        only show matching arbitration ids
//   dedup on|off              hide repeated identical payloads
//   wait <seconds>            keep receiving without typing
//   clear | help | quit

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "canfd/canfd.hpp"

namespace {

std::atomic<bool> g_stop{false};
std::mutex g_print_mutex;
std::mutex g_view_mutex;
std::vector<uint32_t> g_filter;
bool g_dedup = false;
std::map<uint32_t, std::vector<uint8_t>> g_last_payload;

struct State {
  std::unique_ptr<canfd::CanFdBus> bus;
  canfd::DeviceSelector selector;
  canfd::BusConfig config;
  uint32_t default_id = 0x123;
  std::vector<uint8_t> default_data;
  bool brs = true;
  bool show_echo = false;
};

void onSignal(int) { g_stop.store(true); }

std::string trim(const std::string& text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool parseHexId(const std::string& token, uint32_t& id, bool& force_extended) {
  std::string text = token;
  force_extended = false;
  if (text.size() > 4 && lower(text.substr(0, 4)) == "ext:") {
    text = text.substr(4);
    force_extended = true;
  }
  if (text.size() > 2 && lower(text.substr(0, 2)) == "0x") {
    text = text.substr(2);
  }
  if (text.empty() ||
      !std::all_of(text.begin(), text.end(),
                   [](unsigned char c) { return std::isxdigit(c) != 0; })) {
    return false;
  }
  id = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
  return id <= 0x1FFFFFFF;
}

std::vector<uint8_t> parseHexBytes(const std::vector<std::string>& tokens) {
  std::string cleaned;
  for (const auto& token : tokens) {
    for (char c : token) {
      if (std::isxdigit(static_cast<unsigned char>(c))) {
        cleaned += c;
      }
    }
  }
  std::vector<uint8_t> data;
  for (std::size_t i = 0; i + 1 < cleaned.size(); i += 2) {
    data.push_back(static_cast<uint8_t>(std::strtoul(cleaned.substr(i, 2).c_str(), nullptr, 16)));
  }
  return data;
}

std::string formatBytes(const canfd::CanFrame& frame) {
  std::string out;
  char byte[4];
  for (uint8_t i = 0; i < frame.size; ++i) {
    std::snprintf(byte, sizeof(byte), "%02X", frame.data[i]);
    out += byte;
    if (i + 1 < frame.size) {
      out += ' ';
    }
  }
  return out;
}

void printFrame(const canfd::CanFrame& frame) {
  std::lock_guard<std::mutex> lock(g_print_mutex);
  std::printf("TX  %-28s %s\n", formatBytes(frame).c_str(), frame.toString().c_str());
}

void printRx(const canfd::CanFrame& frame, bool show_echo) {
  if (frame.echo && !show_echo) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_view_mutex);
    if (!g_filter.empty() &&
        std::find(g_filter.begin(), g_filter.end(), frame.id) == g_filter.end()) {
      return;
    }
    if (g_dedup) {
      std::vector<uint8_t>& last = g_last_payload[frame.id];
      if (last.size() == frame.size &&
          std::equal(frame.data.begin(), frame.data.begin() + frame.size, last.begin())) {
        return;
      }
      last.assign(frame.data.begin(), frame.data.begin() + frame.size);
    }
  }
  std::lock_guard<std::mutex> lock(g_print_mutex);
  std::printf("%s  %s\n", frame.echo ? "ec" : "RX", frame.toString().c_str());
}

void printAdapters(const std::vector<canfd::AdapterInfo>& adapters) {
  std::lock_guard<std::mutex> lock(g_print_mutex);
  if (adapters.empty()) {
    std::printf("no gs_usb compatible devices found\n");
    return;
  }
  std::printf("found %zu adapter(s):\n", adapters.size());
  for (std::size_t i = 0; i < adapters.size(); ++i) {
    const auto& a = adapters[i];
    std::printf("  [%zu] %s  %04X:%04X  bus=%u addr=%u  serial=%s\n", i, a.name().c_str(),
                a.vendor_id, a.product_id, a.bus, a.address,
                a.serial.empty() ? "-" : a.serial.c_str());
  }
}

bool openDevice(State& state, int index) {
  if (state.bus) {
    state.bus->stop();
    state.bus->close();
    state.bus.reset();
  }
  try {
    state.selector.index = index;
    auto bus = std::make_unique<canfd::CanFdBus>(state.selector);
    bus->open();
    bus->configure(state.config);
    bus->start([&state](const canfd::CanFrame& frame) { printRx(frame, state.show_echo); });

    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("opened device[%d] in=0x%02X out=0x%02X fclk=%u Hz feature=0x%08X\n", index,
                bus->inputEndpoint(), bus->outputEndpoint(), bus->clockFrequency(),
                bus->feature());
    std::printf("bus: nominal %u bit/s, data %u bit/s, mode=%s, extended ids=yes\n",
                state.config.bitrate, state.config.data_bitrate,
                bus->isFd() ? "CAN FD" : "classic CAN");
    state.bus = std::move(bus);
    return true;
  } catch (const std::exception& exc) {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("open failed: %s\n", exc.what());
    state.bus.reset();
    return false;
  }
}

int chooseDevice(const std::vector<canfd::AdapterInfo>& adapters) {
  if (adapters.empty()) {
    return -1;
  }
  if (adapters.size() == 1) {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("only one adapter, selecting [0]\n");
    return 0;
  }
  std::printf("select device [0-%zu] (default 0): ", adapters.size() - 1);
  std::fflush(stdout);
  std::string line;
  if (!std::getline(std::cin, line)) {
    return 0;
  }
  const std::string trimmed = trim(line);
  if (trimmed.empty()) {
    return 0;
  }
  const long value = std::strtol(trimmed.c_str(), nullptr, 10);
  if (value < 0 || value >= static_cast<long>(adapters.size())) {
    std::printf("invalid selection, using [0]\n");
    return 0;
  }
  return static_cast<int>(value);
}

void sendFrame(State& state, uint32_t id, const std::vector<uint8_t>& data, bool force_extended) {
  canfd::CanFrame frame;
  frame.id = id;
  frame.extended = force_extended || id > 0x7FF;
  frame.fd = state.config.fd;
  frame.brs = state.config.fd && state.brs;
  frame.size = static_cast<uint8_t>(std::min<std::size_t>(data.size(), canfd::kMaxPayload));
  for (uint8_t i = 0; i < frame.size; ++i) {
    frame.data[i] = data[i];
  }
  state.bus->send(frame);
  printFrame(frame);
}

void printHelp() {
  std::lock_guard<std::mutex> lock(g_print_mutex);
  std::printf(
      "\ncommands:\n"
      "  send <id> [hexbytes]      transmit (extended auto-detected when id > 7FF)\n"
      "  send ext <id> [hexbytes]  force an extended frame\n"
      "  send ext:1ABCDEF 1122     ext: prefix also forces extended\n"
      "  id <hex>                  set default id\n"
      "  data <hexbytes>           set default data\n"
      "  scan                      list adapters again\n"
      "  open <index>              switch to another adapter\n"
      "  fd on|off                 CAN FD toggle (default on)\n"
      "  brs on|off                bit-rate switch toggle (default on)\n"
      "  echo on|off               show our own TX echoes (default off)\n"
      "  filter <id...>|off        only show matching arbitration ids\n"
      "  dedup on|off              hide repeated identical payloads\n"
      "  wait <seconds>            keep receiving\n"
      "  clear                     clear the screen\n"
      "  help                      show this help\n"
      "  quit                      stop and exit\n\n");
}

bool handleCommand(State& state, const std::vector<std::string>& tokens) {
  const std::string cmd = lower(tokens[0]);

  if (cmd == "quit" || cmd == "exit" || cmd == "q") {
    return false;
  }
  if (cmd == "help" || cmd == "?") {
    printHelp();
    return true;
  }
  if (cmd == "clear") {
    std::printf("\033[2J\033[H");
    return true;
  }
  if (cmd == "scan") {
    printAdapters(canfd::scanAdapters(0, 0));
    return true;
  }
  if (cmd == "open") {
    if (tokens.size() < 2) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::printf("usage: open <index>\n");
      return true;
    }
    const int index = std::atoi(tokens[1].c_str());
    openDevice(state, index);
    return true;
  }
  if (cmd == "fd" || cmd == "brs" || cmd == "echo") {
    const bool on = tokens.size() > 1 && lower(tokens[1]) == "on";
    const bool off = tokens.size() > 1 && lower(tokens[1]) == "off";
    bool* target = cmd == "fd" ? &state.config.fd
                              : cmd == "brs" ? &state.brs : &state.show_echo;
    if (!on && !off) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::printf("%s is %s\n", cmd.c_str(), *target ? "on" : "off");
      return true;
    }
    *target = on;
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("%s -> %s\n", cmd.c_str(), on ? "on" : "off");
    return true;
  }
  if (cmd == "id") {
    uint32_t id = 0;
    bool forced = false;
    if (tokens.size() < 2 || !parseHexId(tokens[1], id, forced)) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::printf("usage: id <hex>\n");
      return true;
    }
    state.default_id = id;
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("default id -> %X%s\n", id, id > 0x7FF ? " (extended)" : "");
    return true;
  }
  if (cmd == "data") {
    state.default_data = parseHexBytes(std::vector<std::string>(tokens.begin() + 1, tokens.end()));
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("default data -> [%zu]\n", state.default_data.size());
    return true;
  }
  if (cmd == "wait" || cmd == "sleep") {
    const double seconds = tokens.size() > 1 ? std::atof(tokens[1].c_str()) : 1.0;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    return true;
  }
  if (cmd == "filter") {
    std::lock_guard<std::mutex> view_lock(g_view_mutex);
    g_filter.clear();
    if (tokens.size() >= 2 && lower(tokens[1]) != "off") {
      for (std::size_t i = 1; i < tokens.size(); ++i) {
        uint32_t id = 0;
        bool forced = false;
        if (parseHexId(tokens[i], id, forced)) {
          g_filter.push_back(id);
        }
      }
    }
    std::lock_guard<std::mutex> print_lock(g_print_mutex);
    if (g_filter.empty()) {
      std::printf("filter: showing all ids\n");
    } else {
      std::printf("filter:");
      for (uint32_t id : g_filter) {
        std::printf(" %X", id);
      }
      std::printf("\n");
    }
    return true;
  }
  if (cmd == "dedup") {
    const bool on = tokens.size() > 1 && lower(tokens[1]) == "on";
    const bool off = tokens.size() > 1 && lower(tokens[1]) == "off";
    if (!on && !off) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::printf("dedup is %s\n", g_dedup ? "on" : "off");
      return true;
    }
    {
      std::lock_guard<std::mutex> view_lock(g_view_mutex);
      g_dedup = on;
      g_last_payload.clear();
    }
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::printf("dedup -> %s\n", on ? "on" : "off");
    return true;
  }
  if (cmd == "send" || cmd == "s") {
    std::size_t index = 1;
    bool force_extended = false;
    if (index < tokens.size() && lower(tokens[index]) == "ext") {
      force_extended = true;
      ++index;
    }
    if (index >= tokens.size()) {
      sendFrame(state, state.default_id, state.default_data, force_extended);
      return true;
    }
    uint32_t id = 0;
    bool prefix_extended = false;
    if (!parseHexId(tokens[index], id, prefix_extended)) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::printf("invalid id '%s'\n", tokens[index].c_str());
      return true;
    }
    ++index;
    const std::vector<uint8_t> data =
        parseHexBytes(std::vector<std::string>(tokens.begin() + index, tokens.end()));
    sendFrame(state, id, data, force_extended || prefix_extended);
    return true;
  }

  std::lock_guard<std::mutex> lock(g_print_mutex);
  std::printf("unknown command '%s' (type help)\n", cmd.c_str());
  return true;
}

struct Options {
  canfd::DeviceSelector selector;
  canfd::BusConfig config;
  bool index_given = false;
  bool show_echo = false;
};

Options parseArgs(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--vid") {
      options.selector.vid = static_cast<uint16_t>(std::strtoul(next().c_str(), nullptr, 0));
    } else if (a == "--pid") {
      options.selector.pid = static_cast<uint16_t>(std::strtoul(next().c_str(), nullptr, 0));
    } else if (a == "--index") {
      options.selector.index = std::atoi(next().c_str());
      options.index_given = true;
    } else if (a == "--bitrate") {
      options.config.bitrate = std::strtoul(next().c_str(), nullptr, 0);
    } else if (a == "--sample-point") {
      options.config.sample_point = std::atof(next().c_str());
    } else if (a == "--data-bitrate") {
      options.config.data_bitrate = std::strtoul(next().c_str(), nullptr, 0);
    } else if (a == "--data-sample-point") {
      options.config.data_sample_point = std::atof(next().c_str());
    } else if (a == "--classic") {
      options.config.fd = false;
    } else if (a == "--echo") {
      options.show_echo = true;
    } else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      std::exit(2);
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parseArgs(argc, argv);
  std::signal(SIGINT, onSignal);

  State state;
  state.selector = options.selector;
  state.config = options.config;
  state.show_echo = options.show_echo;

  std::printf("=== scanning for gs_usb adapters ===\n");
  const std::vector<canfd::AdapterInfo> adapters = canfd::scanAdapters(0, 0);
  printAdapters(adapters);
  if (adapters.empty()) {
    return 1;
  }

  const int index = options.index_given ? options.selector.index : chooseDevice(adapters);
  if (!openDevice(state, index)) {
    return 1;
  }
  std::printf("type 'help' for commands, 'quit' to exit\n\n");

  try {
    std::string line;
    while (!g_stop.load()) {
      std::printf("canfd> ");
      std::fflush(stdout);
      if (!std::getline(std::cin, line)) {
        break;
      }
      const std::string trimmed = trim(line);
      if (trimmed.empty()) {
        continue;
      }
      std::istringstream stream(trimmed);
      std::vector<std::string> tokens;
      std::string token;
      while (stream >> token) {
        tokens.push_back(token);
      }
      if (!handleCommand(state, tokens)) {
        break;
      }
    }
  } catch (const std::exception& exc) {
    std::fprintf(stderr, "error: %s\n", exc.what());
  }

  std::printf("\nstopping...\n");
  if (state.bus) {
    state.bus->stop();
    state.bus->close();
  }
  return 0;
}
