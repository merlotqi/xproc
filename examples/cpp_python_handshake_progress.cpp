// C++ parent launches a Python worker, performs a bidirectional xproc handshake,
// then receives structured progress events from the Python side.
//
// The handshake itself uses two xproc varlen channels:
//   - upstream:   Python -> C++  (hello / progress / done / error)
//   - downstream: C++    -> Python (ack)
//
// This is a minimal protocol demo focused on only two concerns:
//   1. xproc-based identity confirmation,
//   2. realtime progress capture.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <xproc/xproc.hpp>

#include "process.hpp"

#ifndef XPROC_SOURCE_ROOT
#define XPROC_SOURCE_ROOT "."
#endif

#ifndef XPROC_BINARY_ROOT
#define XPROC_BINARY_ROOT "."
#endif

namespace {

constexpr std::chrono::milliseconds kPollSleep{20};
constexpr std::chrono::milliseconds kProgressPollSleep{40};
constexpr std::chrono::seconds kHandshakeTimeout{10};
constexpr std::size_t kDataCapacity = 65536;
constexpr std::size_t kShmSize = xproc::ipc::shm_size_for_data_capacity(kDataCapacity);
constexpr std::uint64_t kUpstreamSchemaId = 0x5059455654303031ULL;    // "PYEVT001"
constexpr std::uint64_t kDownstreamSchemaId = 0x5059435452303031ULL;  // "PYCTR001"
constexpr const char* kWorkerScriptName = "cpp_python_handshake_worker.py";
constexpr const char* kProtocolVersion = "1";

struct cli_options {
  std::string python_bin;
  std::string script_path;
  std::string module_dir;
};

struct parsed_message {
  std::string type;
  std::map<std::string, std::string> fields;
};

struct run_summary {
  bool saw_done{false};
  bool saw_error{false};
  std::string error_message;
};

std::filesystem::path source_root() { return std::filesystem::path(XPROC_SOURCE_ROOT); }

std::filesystem::path binary_root() { return std::filesystem::path(XPROC_BINARY_ROOT); }

std::string current_process_id_string() { return std::to_string(xproc::platform::current_process_id()); }

std::string default_python_binary() {
#if defined(__linux__) || defined(__APPLE__)
  return "python3";
#elif defined(_WIN32) || defined(_WIN64)
  return "python";
#endif
}

std::string default_script_path() { return (source_root() / "Python" / "examples" / kWorkerScriptName).string(); }

std::string default_module_dir() { return (binary_root() / "Python" / "stage").string(); }

std::uint64_t make_token() {
  std::random_device rd;
  const std::uint64_t a = (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
  const std::uint64_t b = (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
  return a ^ b;
}

std::string token_to_hex(std::uint64_t token) {
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << token;
  return out.str();
}

cli_options parse_cli(int argc, char** argv) {
  cli_options cli;
  cli.python_bin = default_python_binary();
  cli.script_path = default_script_path();
  cli.module_dir = default_module_dir();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--python=", 0) == 0) {
      cli.python_bin = arg.substr(std::string("--python=").size());
      continue;
    }
    if (arg == "--python" && i + 1 < argc) {
      cli.python_bin = argv[++i];
      continue;
    }
    if (arg.rfind("--script=", 0) == 0) {
      cli.script_path = arg.substr(std::string("--script=").size());
      continue;
    }
    if (arg == "--script" && i + 1 < argc) {
      cli.script_path = argv[++i];
      continue;
    }
    if (arg.rfind("--module-dir=", 0) == 0) {
      cli.module_dir = arg.substr(std::string("--module-dir=").size());
      continue;
    }
    if (arg == "--module-dir" && i + 1 < argc) {
      cli.module_dir = argv[++i];
      continue;
    }
    throw std::invalid_argument("unknown argument: " + arg);
  }

  return cli;
}

xproc::ipc::transport_options make_varlen_options(const std::string& path, bool create_if_missing,
                                                  std::uint64_t schema_id) {
  xproc::ipc::transport_options opts;
  opts.path = path;
  opts.shm_size = create_if_missing ? kShmSize : xproc::ipc::infer_existing_shm_size;
  opts.type = xproc::ipc::channel_type::varlen;
  opts.schema_id = schema_id;
  opts.create_if_missing = create_if_missing;
  return opts;
}

void cleanup_shm(const std::string& path) { xproc::shm::shm::unlink(path); }

std::string escape_field(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '=':
        out += "\\e";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::string unescape_field(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool escaping = false;
  for (const char ch : value) {
    if (!escaping) {
      if (ch == '\\') {
        escaping = true;
      } else {
        out.push_back(ch);
      }
      continue;
    }

    switch (ch) {
      case '\\':
        out.push_back('\\');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'e':
        out.push_back('=');
        break;
      default:
        out.push_back(ch);
        break;
    }
    escaping = false;
  }
  if (escaping) {
    out.push_back('\\');
  }
  return out;
}

std::string build_message(const std::string& type, const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out = type;
  for (const auto& field : fields) {
    out.push_back('\t');
    out += field.first;
    out.push_back('=');
    out += escape_field(field.second);
  }
  return out;
}

parsed_message parse_message(const std::string& raw) {
  parsed_message message;
  std::size_t start = 0;
  bool first = true;
  while (true) {
    const std::size_t tab = raw.find('\t', start);
    const std::string token =
        raw.substr(start, tab == std::string::npos ? std::string::npos : static_cast<std::size_t>(tab - start));
    if (first) {
      message.type = token;
      first = false;
    } else {
      const std::size_t eq = token.find('=');
      if (eq != std::string::npos) {
        message.fields.emplace(token.substr(0, eq), unescape_field(token.substr(eq + 1)));
      }
    }
    if (tab == std::string::npos) {
      break;
    }
    start = tab + 1;
  }
  return message;
}

void send_message(xproc::ipc::producer& producer, const parsed_message& message) {
  std::vector<std::pair<std::string, std::string>> fields;
  fields.reserve(message.fields.size());
  for (const auto& field : message.fields) {
    fields.push_back(field);
  }
  const std::string payload = build_message(message.type, fields);
  producer.send_varlen(payload.data(), static_cast<std::uint32_t>(payload.size()));
}

std::optional<parsed_message> poll_message(xproc::ipc::consumer& consumer) {
  std::optional<parsed_message> message;
  consumer.poll([&](void* ptr, std::uint32_t len) {
    const std::string payload(static_cast<const char*>(ptr), static_cast<std::size_t>(len));
    message = parse_message(payload);
  });
  return message;
}

std::string field_or(const parsed_message& message, const char* name, const std::string& fallback = "") {
  const auto it = message.fields.find(name);
  if (it == message.fields.end()) {
    return fallback;
  }
  return it->second;
}

std::vector<std::string> make_child_argv(const cli_options& cli, const std::string& upstream_path,
                                         const std::string& downstream_path, const std::string& session) {
  return {
      cli.python_bin,    cli.script_path, "--module-dir",      cli.module_dir,
      "--upstream-path", upstream_path,   "--downstream-path", downstream_path,
      "--session",       session,         "--parent-pid",      current_process_id_string(),
  };
}

bool validate_hello(const parsed_message& hello, const xproc::examples::process& child,
                    const std::string& expected_session) {
  if (hello.type != "hello") {
    return false;
  }

  if (field_or(hello, "session") != expected_session) {
    return false;
  }

  if (field_or(hello, "protocol") != kProtocolVersion) {
    return false;
  }

  if (field_or(hello, "parent_pid") != current_process_id_string()) {
    return false;
  }

  const std::string raw_pid = field_or(hello, "pid");
  if (raw_pid.empty()) {
    return false;
  }

  const auto expected_pid = static_cast<long long>(child.pid());
  const auto actual_pid = static_cast<long long>(std::stoll(raw_pid, nullptr, 10));
  return actual_pid == expected_pid;
}

parsed_message wait_for_hello(xproc::ipc::consumer& upstream, xproc::examples::process& child,
                              const std::string& session) {
  const auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;

  while (std::chrono::steady_clock::now() < deadline) {
    while (true) {
      const std::optional<parsed_message> message = poll_message(upstream);
      if (!message.has_value()) {
        break;
      }
      if (!validate_hello(*message, child, session)) {
        throw std::runtime_error("received invalid hello message from Python child");
      }
      return *message;
    }

    if (child.poll_exit()) {
      throw std::runtime_error("Python child exited before handshake completed");
    }

    std::this_thread::sleep_for(kPollSleep);
  }

  throw std::runtime_error("timeout waiting for Python hello message");
}

void handle_upstream_message(const parsed_message& message, run_summary& summary) {
  if (message.type == "progress") {
    const auto percent = static_cast<int>(std::strtol(field_or(message, "percent", "-1").c_str(), nullptr, 10));
    const auto current = static_cast<int>(std::strtol(field_or(message, "current", "0").c_str(), nullptr, 10));
    const auto total = static_cast<int>(std::strtol(field_or(message, "total", "0").c_str(), nullptr, 10));
    std::cout << "[progress]";
    if (percent >= 0) {
      std::cout << ' ' << percent << '%';
    }
    std::cout << " stage=" << field_or(message, "stage", "unknown") << " step=" << current << '/' << total
              << " message=" << field_or(message, "message", "") << "\n";
    return;
  }

  if (message.type == "done") {
    summary.saw_done = true;
    std::cout << "[done] python worker completed after handshake and progress reporting\n";
    return;
  }

  if (message.type == "error") {
    summary.saw_error = true;
    summary.error_message = field_or(message, "message", "unknown Python error");
    std::cerr << "[python error] " << summary.error_message << "\n";
    return;
  }

  if (message.type == "hello") {
    return;
  }

  std::cout << "[event] type=" << message.type << "\n";
}

void consume_progress_until_exit(xproc::ipc::consumer& upstream, xproc::examples::process& child,
                                 run_summary& summary) {
  while (true) {
    bool consumed_any = false;
    while (true) {
      const std::optional<parsed_message> message = poll_message(upstream);
      if (!message.has_value()) {
        break;
      }
      consumed_any = true;
      handle_upstream_message(*message, summary);
    }

    if (child.poll_exit()) {
      break;
    }

    if (!consumed_any) {
      std::this_thread::sleep_for(kProgressPollSleep);
    }
  }

  while (true) {
    const std::optional<parsed_message> message = poll_message(upstream);
    if (!message.has_value()) {
      break;
    }
    handle_upstream_message(*message, summary);
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const cli_options cli = parse_cli(argc, argv);
    const std::string session = token_to_hex(make_token());
    const std::string base =
        "/xproc_cpp_python_demo_" + current_process_id_string() + "_" + token_to_hex(make_token() & 0xffffffu);
    const std::string upstream_path = base + "_up";
    const std::string downstream_path = base + "_down";

    cleanup_shm(upstream_path);
    cleanup_shm(downstream_path);

    xproc::ipc::consumer upstream(make_varlen_options(upstream_path, true, kUpstreamSchemaId));
    xproc::ipc::producer downstream(make_varlen_options(downstream_path, true, kDownstreamSchemaId));

    const std::vector<std::string> child_argv = make_child_argv(cli, upstream_path, downstream_path, session);
    xproc::examples::process child = xproc::examples::process::spawn(child_argv);

    try {
      const parsed_message hello = wait_for_hello(upstream, child, session);
      std::cout << "handshake ok: python pid " << field_or(hello, "pid") << ", progress follows\n";

      parsed_message ack;
      ack.type = "ack";
      ack.fields.emplace("session", session);
      ack.fields.emplace("ok", "1");
      ack.fields.emplace("protocol", kProtocolVersion);
      ack.fields.emplace("parent_pid", current_process_id_string());
      send_message(downstream, ack);

      run_summary summary;
      consume_progress_until_exit(upstream, child, summary);

      const int exit_code = child.wait();
      cleanup_shm(upstream_path);
      cleanup_shm(downstream_path);

      if (summary.saw_error) {
        std::cerr << "Python worker reported an error\n";
        return 1;
      }
      if (exit_code != 0) {
        std::cerr << "Python worker exited with code " << exit_code << "\n";
        return 1;
      }
      if (!summary.saw_done) {
        std::cerr << "Python worker exited without sending a done event\n";
        return 1;
      }

      std::cout << "child exited, parent done\n";
      return 0;
    } catch (...) {
      child.terminate();
      cleanup_shm(upstream_path);
      cleanup_shm(downstream_path);
      throw;
    }
  } catch (const std::exception& ex) {
    std::cerr << "cpp_python_handshake_progress_demo failed: " << ex.what() << "\n";
    return 1;
  }
}
