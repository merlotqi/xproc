#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <xproc/platform/subprocess.hpp>

using namespace xproc::platform;

// ===========================================================================
// pipe_handle
// ===========================================================================

TEST(Subprocess, PipeHandleDefaultInvalid) {
  pipe_handle ph;
  EXPECT_FALSE(ph.valid());
}

TEST(Subprocess, PipeHandleMoveConstruct) {
  pipe_handle a;
  pipe_handle b(std::move(a));
  EXPECT_FALSE(a.valid());
  EXPECT_FALSE(b.valid());
}

TEST(Subprocess, PipeHandleMoveAssign) {
  pipe_handle a;
  pipe_handle b;
  b = std::move(a);
  EXPECT_FALSE(a.valid());
  EXPECT_FALSE(b.valid());
}

// ===========================================================================
// process_options
// ===========================================================================

TEST(Subprocess, ProcessOptionsDefaults) {
  process_options opts;
  EXPECT_TRUE(opts.env.empty());
  EXPECT_TRUE(opts.cwd.empty());
  EXPECT_EQ(opts.stdin_mode, stdio_mode::inherit);
  EXPECT_EQ(opts.stdout_mode, stdio_mode::inherit);
  EXPECT_EQ(opts.stderr_mode, stdio_mode::inherit);
}

// ===========================================================================
// spawn basics
// ===========================================================================

TEST(Subprocess, SpawnInvalidArgv) {
  EXPECT_THROW(process::spawn({}), std::invalid_argument);
}

TEST(Subprocess, SelfExe) {
  std::string exe = process::self_exe();
  EXPECT_FALSE(exe.empty());
}

TEST(Subprocess, SpawnWaitExitCode) {
  auto p = process::spawn({"true"});
  EXPECT_TRUE(p.valid());
  EXPECT_FALSE(p.finished());
  int code = p.wait();
  EXPECT_EQ(code, 0);
  EXPECT_TRUE(p.finished());
}

TEST(Subprocess, SpawnWaitExitCodeNonZero) {
  auto p = process::spawn({"false"});
  int code = p.wait();
  EXPECT_NE(code, 0);
}

TEST(Subprocess, PollExit) {
  auto p = process::spawn({"true"});
  p.wait();
  EXPECT_TRUE(p.finished());
}

TEST(Subprocess, Terminate) {
  auto p = process::spawn({"sleep", "60"});
  EXPECT_TRUE(p.valid());
  p.terminate();
  EXPECT_TRUE(p.finished());
}

TEST(Subprocess, MoveConstruct) {
  auto p = process::spawn({"true"});
  EXPECT_TRUE(p.valid());
  process q(std::move(p));
  EXPECT_FALSE(p.valid());
  EXPECT_TRUE(q.valid());
  q.wait();
}

TEST(Subprocess, MoveAssign) {
  auto p = process::spawn({"true"});
  EXPECT_TRUE(p.valid());
  process q;
  EXPECT_FALSE(q.valid());
  q = std::move(p);
  EXPECT_FALSE(p.valid());
  EXPECT_TRUE(q.valid());
  q.wait();
}

// ===========================================================================
// I/O pipe redirection
// ===========================================================================

TEST(Subprocess, StdoutPipe) {
  process_options opts;
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"echo", "hello"}, opts);
  EXPECT_TRUE(p.stdout_pipe().valid());

  char buf[64] = {};
  std::size_t n = p.stdout_pipe().read(buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  std::string output(buf, n);
  EXPECT_NE(output.find("hello"), std::string::npos);

  p.wait();
}

TEST(Subprocess, StdinPipe) {
  process_options opts;
  opts.stdin_mode = stdio_mode::pipe;
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"cat"}, opts);
  EXPECT_TRUE(p.stdin_pipe().valid());
  EXPECT_TRUE(p.stdout_pipe().valid());

  const char* msg = "test-input";
  p.stdin_pipe().write(msg, std::strlen(msg));
  p.stdin_pipe().close();

  char buf[64] = {};
  std::size_t n = p.stdout_pipe().read(buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  std::string output(buf, n);
  EXPECT_NE(output.find("test-input"), std::string::npos);

  p.wait();
}

TEST(Subprocess, NullDevRedirect) {
  process_options opts;
  opts.stdout_mode = stdio_mode::null_dev;
  auto p = process::spawn({"echo", "should-not-appear"}, opts);
  EXPECT_FALSE(p.stdout_pipe().valid());
  p.wait();
  EXPECT_EQ(p.wait(), 0);
}

// ===========================================================================
// Environment variables
// ===========================================================================

TEST(Subprocess, EnvironmentVariable) {
  // Preserve PATH so execvp can locate /usr/bin/env, plus add our test var.
  const char* path_val = std::getenv("PATH");
  process_options opts;
  opts.env = {{"MY_TEST_VAR", "hello_world"}};
  if (path_val) {
    opts.env.emplace_back("PATH", path_val);
  }
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"/usr/bin/env"}, opts);

  char buf[4096] = {};
  std::size_t total = 0;
  while (true) {
    std::size_t n = p.stdout_pipe().read(buf + total, sizeof(buf) - total);
    if (n == 0) break;
    total += n;
  }
  std::string output(buf, total);
  EXPECT_NE(output.find("MY_TEST_VAR=hello_world"), std::string::npos);
  p.wait();
}

// ===========================================================================
// Working directory
// ===========================================================================

TEST(Subprocess, WorkingDirectory) {
  process_options opts;
  opts.cwd = "/tmp";
  opts.stdout_mode = stdio_mode::pipe;
  auto p = process::spawn({"pwd"}, opts);

  char buf[1024] = {};
  std::size_t n = p.stdout_pipe().read(buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  std::string output(buf, n);
  EXPECT_NE(output.find("/tmp"), std::string::npos);
  p.wait();
}
