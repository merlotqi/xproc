#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <xproc/ipc/options.hpp>
#include <xproc/ipc/socket_channel.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace xproc::ipc {
namespace {

constexpr std::uint32_t k_max_varlen = 16u * 1024u * 1024u;

std::string normalize_socket_host(const std::string& host) {
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    return host.substr(1, host.size() - 2);
  }
  return host;
}

inline std::uint32_t load_le32(const void* p) {
  auto b = static_cast<const std::uint8_t*>(p);
  return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
         (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

inline void store_le32(void* p, std::uint32_t v) {
  auto b = static_cast<std::uint8_t*>(p);
  b[0] = static_cast<std::uint8_t>(v & 0xffu);
  b[1] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
  b[2] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
  b[3] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
}

#if defined(_WIN32)

void winsock_init() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA w{};
    if (::WSAStartup(MAKEWORD(2, 2), &w) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
  });
}

using sock_handle = SOCKET;

sock_handle invalid_sock() { return INVALID_SOCKET; }

bool is_invalid(sock_handle s) { return s == INVALID_SOCKET; }

void close_handle(sock_handle s) noexcept {
  if (!is_invalid(s)) {
    ::closesocket(s);
  }
}

void write_full_sock(sock_handle s, const void* data, std::size_t len) {
  const auto* p = static_cast<const char*>(data);
  std::size_t left = len;
  while (left > 0) {
    const int chunk = static_cast<int>(std::min<std::size_t>(left, static_cast<std::size_t>(INT_MAX)));
    const int r = ::send(s, p, chunk, 0);
    if (r <= 0) {
      throw std::runtime_error("socket send failed");
    }
    p += r;
    left -= static_cast<std::size_t>(r);
  }
}

void read_exact_sock(sock_handle s, void* data, std::size_t len) {
  char* p = static_cast<char*>(data);
  std::size_t left = len;
  while (left > 0) {
    const int chunk = static_cast<int>(std::min<std::size_t>(left, static_cast<std::size_t>(INT_MAX)));
    const int r = ::recv(s, p, chunk, MSG_WAITALL);
    if (r <= 0) {
      throw std::runtime_error("socket recv failed or closed");
    }
    p += r;
    left -= static_cast<std::size_t>(r);
  }
}

#else

using sock_handle = int;

sock_handle invalid_sock() { return -1; }

bool is_invalid(sock_handle s) { return s < 0; }

void close_handle(sock_handle s) noexcept {
  if (!is_invalid(s)) {
    ::close(s);
  }
}

void write_full_sock(sock_handle s, const void* data, std::size_t len) {
  const auto* p = static_cast<const std::uint8_t*>(data);
  std::size_t left = len;
  while (left > 0) {
    const ssize_t r = ::send(s, p, left, 0);
    if (r <= 0) {
      throw std::runtime_error("socket send failed");
    }
    p += static_cast<std::size_t>(r);
    left -= static_cast<std::size_t>(r);
  }
}

void read_exact_sock(sock_handle s, void* data, std::size_t len) {
  auto* p = static_cast<std::uint8_t*>(data);
  std::size_t left = len;
  while (left > 0) {
    const ssize_t r = ::recv(s, p, left, MSG_WAITALL);
    if (r <= 0) {
      throw std::runtime_error("socket recv failed or closed");
    }
    p += static_cast<std::size_t>(r);
    left -= static_cast<std::size_t>(r);
  }
}

#endif

template <typename Attempt>
sock_handle try_resolved_stream_socket(addrinfo* res, Attempt&& attempt) {
  for (int pass = 0; pass < 2; ++pass) {
    for (addrinfo* it = res; it != nullptr; it = it->ai_next) {
      const bool is_ipv6 = (it->ai_family == AF_INET6);
      if ((pass == 0 && !is_ipv6) || (pass == 1 && is_ipv6)) {
        continue;
      }
      sock_handle s = static_cast<sock_handle>(::socket(it->ai_family, it->ai_socktype, it->ai_protocol));
      if (is_invalid(s)) {
        continue;
      }
      if (attempt(s, it)) {
        return s;
      }
      close_handle(s);
    }
  }
  return invalid_sock();
}

void set_reuse_addr(sock_handle s) noexcept {
  int yes = 1;
#if defined(_WIN32)
  (void)::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
  (void)::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
}

void try_enable_dual_stack(sock_handle s, int family) noexcept {
  if (family != AF_INET6) {
    return;
  }
  int off = 0;
#if defined(_WIN32)
  (void)::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof(off));
#else
  (void)::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
#endif
}

sock_handle tcp_connect(const std::string& host, std::uint16_t port) {
#if defined(_WIN32)
  winsock_init();
#endif
  const std::string normalized_host = normalize_socket_host(host);
  const std::string port_str = std::to_string(static_cast<int>(port));
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* res = nullptr;
  const int gai = ::getaddrinfo(normalized_host.c_str(), port_str.c_str(), &hints, &res);
  if (gai != 0 || res == nullptr) {
    throw std::runtime_error("getaddrinfo failed for socket transport");
  }
  const sock_handle s = try_resolved_stream_socket(res, [](sock_handle sock, const addrinfo* ai) {
    return ::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0;
  });
  ::freeaddrinfo(res);
  if (is_invalid(s)) {
    throw std::runtime_error("socket connect failed");
  }
  return s;
}

sock_handle tcp_listen(std::uint16_t port, std::uint16_t* out_bound_port) {
#if defined(_WIN32)
  winsock_init();
#endif
  const std::string port_str = std::to_string(static_cast<int>(port));
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;
  addrinfo* res = nullptr;
  if (::getaddrinfo(nullptr, port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
    throw std::runtime_error("getaddrinfo(listen) failed");
  }
  const sock_handle listen_fd = try_resolved_stream_socket(res, [](sock_handle sock, const addrinfo* ai) {
    set_reuse_addr(sock);
    try_enable_dual_stack(sock, ai->ai_family);
    if (::bind(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0) {
      return false;
    }
    return ::listen(sock, 1) == 0;
  });
  ::freeaddrinfo(res);
  if (is_invalid(listen_fd)) {
    throw std::runtime_error("socket bind/listen failed");
  }
  if (out_bound_port != nullptr && port == 0) {
    sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    if (::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&ss), &slen) == 0) {
      if (ss.ss_family == AF_INET) {
        *out_bound_port = ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
      } else if (ss.ss_family == AF_INET6) {
        *out_bound_port = ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
      }
    }
  } else if (out_bound_port != nullptr) {
    *out_bound_port = port;
  }
  return listen_fd;
}

sock_handle tcp_accept(sock_handle listen_fd) {
  const sock_handle c = static_cast<sock_handle>(::accept(listen_fd, nullptr, nullptr));
  if (is_invalid(c)) {
    throw std::runtime_error("socket accept failed");
  }
  return c;
}

}  // namespace

void socket_producer::close_sock() noexcept {
#if defined(_WIN32)
  close_handle(static_cast<SOCKET>(sock_));
  sock_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
#else
  close_handle(sock_);
  sock_ = -1;
#endif
}

socket_producer::socket_producer(const transport_options& opts) : opts_(opts) {
  validate_producer_transport_options(opts_);
  if (opts_.backend != transport_backend::socket) {
    throw std::logic_error("socket_producer: backend must be socket");
  }
  const int max_retries = (opts_.socket_connect_retries > 0) ? opts_.socket_connect_retries : INT_MAX;
  const int retry_ms = (opts_.socket_connect_retry_ms > 0) ? opts_.socket_connect_retry_ms : 10;
  for (int attempt = 0; attempt < max_retries; ++attempt) {
    try {
#if defined(_WIN32)
      sock_ = static_cast<std::uintptr_t>(tcp_connect(opts_.socket_host, opts_.socket_port));
#else
      sock_ = tcp_connect(opts_.socket_host, opts_.socket_port);
#endif
      return;
    } catch (...) {
      if (attempt + 1 >= max_retries) {
        throw;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(retry_ms));
    }
  }
}

socket_producer::~socket_producer() { close_sock(); }

void socket_producer::write_full(const void* data, std::size_t len) {
#if defined(_WIN32)
  write_full_sock(static_cast<SOCKET>(sock_), data, len);
#else
  write_full_sock(sock_, data, len);
#endif
}

void socket_producer::send_fixed_bytes(const void* data, std::uint32_t payload_len) {
  if (opts_.type != channel_type::fixed) {
    throw std::logic_error("socket_producer: fixed_bytes requires fixed channel");
  }
  if (payload_len > opts_.item_size) {
    throw std::invalid_argument("socket_producer: payload_len exceeds item_size");
  }
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(opts_.item_size), 0);
  std::memcpy(buf.data(), data, static_cast<std::size_t>(payload_len));
  write_full(buf.data(), buf.size());
}

void socket_producer::send_fixed_sized(const void* data, std::uint32_t byte_length) {
  if (opts_.type != channel_type::fixed) {
    throw std::logic_error("socket_producer: send_fixed_sized requires fixed channel");
  }
  if (byte_length > opts_.item_size) {
    throw std::invalid_argument("socket_producer: byte_length exceeds item_size");
  }
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(opts_.item_size), 0);
  std::memcpy(buf.data(), data, static_cast<std::size_t>(byte_length));
  write_full(buf.data(), buf.size());
}

void socket_producer::send_varlen(const void* data, std::uint32_t len) {
  if (opts_.type != channel_type::varlen) {
    throw std::logic_error("socket_producer: send_varlen requires variable channel");
  }
  if (len > k_max_varlen) {
    throw std::invalid_argument("socket_producer: len too large");
  }
  std::uint8_t prefix[4];
  store_le32(prefix, len);
  write_full(prefix, sizeof(prefix));
  if (len > 0) {
    write_full(data, len);
  }
}

void socket_consumer::close_listen() noexcept {
#if defined(_WIN32)
  close_handle(static_cast<SOCKET>(listen_));
  listen_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
#else
  close_handle(listen_);
  listen_ = -1;
#endif
}

void socket_consumer::close_sock() noexcept {
#if defined(_WIN32)
  close_handle(static_cast<SOCKET>(sock_));
  sock_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
#else
  close_handle(sock_);
  sock_ = -1;
#endif
}

socket_consumer::socket_consumer(const transport_options& opts) : opts_(opts) {
  validate_consumer_transport_options(opts_);
  if (opts_.backend != transport_backend::socket) {
    throw std::logic_error("socket_consumer: backend must be socket");
  }
  std::uint16_t bound = opts_.socket_port;
#if defined(_WIN32)
  listen_ = static_cast<std::uintptr_t>(tcp_listen(opts_.socket_port, &bound));
  sock_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
#else
  listen_ = tcp_listen(opts_.socket_port, &bound);
  sock_ = -1;
#endif
  opts_.socket_port = bound;
}

socket_consumer::~socket_consumer() {
  close_sock();
  close_listen();
}

bool socket_consumer::ensure_peer_connected() {
#if defined(_WIN32)
  if (sock_ != static_cast<std::uintptr_t>(INVALID_SOCKET)) {
    return true;
  }
  if (is_invalid(static_cast<SOCKET>(listen_))) {
    return false;
  }
  const SOCKET ls = static_cast<SOCKET>(listen_);
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(ls, &readfds);
  TIMEVAL tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  if (::select(0, &readfds, nullptr, nullptr, &tv) <= 0 || !FD_ISSET(ls, &readfds)) {
    return false;
  }
  const SOCKET c = ::accept(ls, nullptr, nullptr);
  if (c == INVALID_SOCKET) {
    return false;
  }
  // Replace existing (closed/stale) sock; keep listen_ open for potential reconnections.
  if (sock_ != static_cast<std::uintptr_t>(INVALID_SOCKET)) {
    close_handle(static_cast<SOCKET>(sock_));
  }
  sock_ = static_cast<std::uintptr_t>(c);
  return true;
#else
  if (sock_ >= 0) {
    return true;
  }
  if (listen_ < 0) {
    return false;
  }
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(listen_, &readfds);
  timeval tv{};
  if (::select(listen_ + 1, &readfds, nullptr, nullptr, &tv) <= 0 || !FD_ISSET(listen_, &readfds)) {
    return false;
  }
  const int c = ::accept(listen_, nullptr, nullptr);
  if (c < 0) {
    return false;
  }
  // Replace existing (closed/stale) sock; keep listen_ open for potential reconnections.
  if (sock_ >= 0) {
    close_handle(sock_);
  }
  sock_ = c;
  return true;
#endif
}

void socket_consumer::wait() {
#if defined(_WIN32)
  // If already connected, sleep briefly (no select on data socket needed).
  if (sock_ != static_cast<std::uintptr_t>(INVALID_SOCKET)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
  }
  if (is_invalid(static_cast<SOCKET>(listen_))) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
  }
  // Wait on listen socket with select() timeout to avoid busy-spin.
  const SOCKET ls = static_cast<SOCKET>(listen_);
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(ls, &readfds);
  TIMEVAL tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 5000;  // 5 ms timeout
  ::select(0, &readfds, nullptr, nullptr, &tv);
#else
  // If already connected, sleep briefly (no select on data socket needed).
  if (sock_ >= 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
  }
  if (listen_ < 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
  }
  // Wait on listen socket with select() timeout to avoid busy-spin.
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(listen_, &readfds);
  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 5000;  // 5 ms timeout
  ::select(listen_ + 1, &readfds, nullptr, nullptr, &tv);
#endif
}

bool socket_consumer::poll_impl(const std::function<void(void*, std::uint32_t)>& handler) {
  if (!ensure_peer_connected()) {
    return false;
  }

#if defined(_WIN32)
  const SOCKET s = static_cast<SOCKET>(sock_);
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(s, &readfds);
  TIMEVAL tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  const int sel = ::select(0, &readfds, nullptr, nullptr, &tv);
  if (sel <= 0 || !FD_ISSET(s, &readfds)) {
    return false;
  }
#else
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(sock_, &readfds);
  timeval tv{};
  const int sel = ::select(sock_ + 1, &readfds, nullptr, nullptr, &tv);
  if (sel <= 0 || !FD_ISSET(sock_, &readfds)) {
    return false;
  }
#endif

  try {
    if (opts_.type == channel_type::fixed) {
      std::vector<std::uint8_t> buf(static_cast<std::size_t>(opts_.item_size));
#if defined(_WIN32)
      read_exact_sock(static_cast<SOCKET>(sock_), buf.data(), buf.size());
#else
      read_exact_sock(sock_, buf.data(), buf.size());
#endif
      handler(buf.data(), static_cast<std::uint32_t>(buf.size()));
      return true;
    }
    std::uint32_t len_le = 0;
#if defined(_WIN32)
    read_exact_sock(static_cast<SOCKET>(sock_), &len_le, sizeof(len_le));
#else
    read_exact_sock(sock_, &len_le, sizeof(len_le));
#endif
    const std::uint32_t len = load_le32(&len_le);
    if (len > k_max_varlen) {
      throw std::runtime_error("socket_consumer: varlen too large");
    }
    std::vector<std::uint8_t> payload(len);
    if (len > 0) {
#if defined(_WIN32)
      read_exact_sock(static_cast<SOCKET>(sock_), payload.data(), len);
#else
      read_exact_sock(sock_, payload.data(), len);
#endif
    }
    handler(payload.data(), len);
    return true;
  } catch (const std::runtime_error&) {
    return false;
  }
}

}  // namespace xproc::ipc
