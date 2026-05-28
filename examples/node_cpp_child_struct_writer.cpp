#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <xproc/xproc.hpp>

namespace {

struct telemetry_packet {
  char message[256];
  std::int32_t a;
  std::int32_t b;
};

constexpr std::uint64_t kSchemaId = 0x4E4F44455F435050ULL;  // "NODE_CPP"

struct cli_options {
  std::string shm_path;
  int ticks{100};
  int interval_ms{300};
};

bool parse_int_arg(const char* raw, const char* name, int* out) {
  if (raw == nullptr || out == nullptr) {
    return false;
  }

  char* end = nullptr;
  const long parsed = std::strtol(raw, &end, 10);
  if (end == raw || *end != '\0' || parsed < 0 || parsed > 1'000'000L) {
    std::cerr << "invalid --" << name << " value: " << raw << "\n";
    return false;
  }

  *out = static_cast<int>(parsed);
  return true;
}

bool parse_cli(int argc, char** argv, cli_options* out) {
  if (out == nullptr) {
    return false;
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--shm-path" && i + 1 < argc) {
      out->shm_path = argv[++i];
      continue;
    }
    if (arg == "--ticks" && i + 1 < argc) {
      if (!parse_int_arg(argv[++i], "ticks", &out->ticks)) {
        return false;
      }
      continue;
    }
    if (arg == "--interval-ms" && i + 1 < argc) {
      if (!parse_int_arg(argv[++i], "interval-ms", &out->interval_ms)) {
        return false;
      }
      continue;
    }

    std::cerr << "unknown or incomplete argument: " << arg << "\n";
    return false;
  }

  if (out->shm_path.empty()) {
    std::cerr << "--shm-path is required\n";
    return false;
  }

  return true;
}

void print_usage() {
  std::cerr << "usage: xproc_node_cpp_child_struct_writer --shm-path <path> [--ticks <n>] [--interval-ms <ms>]\n";
}

}  // namespace

int main(int argc, char** argv) {
  cli_options cli;
  if (!parse_cli(argc, argv, &cli)) {
    print_usage();
    return 1;
  }

  try {
    xproc::ipc::producer producer =
        xproc::ipc::attach_fixed_channel(cli.shm_path).with_schema_id(kSchemaId).open_producer();
    for (int i = 0; i <= cli.ticks; ++i) {
      telemetry_packet packet{};
      std::snprintf(packet.message, sizeof(packet.message), "tick-%d", i);
      packet.a = i;
      packet.b = i * 2;
      producer.send_fixed(packet);
      std::this_thread::sleep_for(std::chrono::milliseconds(cli.interval_ms));
    }
  } catch (const std::exception& ex) {
    std::cerr << "node_cpp_child_struct_writer failed: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
