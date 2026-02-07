#pragma once

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

inline std::string fixture_path(const char *name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

class TempCsv {
  std::string path_;

public:
  explicit TempCsv(const std::string &content) {
    char tmpl[] = "/tmp/glance_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
      throw std::runtime_error("Failed to create temp file");
    path_ = tmpl;
    ssize_t written = ::write(fd, content.data(), content.size());
    (void)written;
    ::close(fd);
  }

  ~TempCsv() { std::remove(path_.c_str()); }

  TempCsv(const TempCsv &) = delete;
  TempCsv &operator=(const TempCsv &) = delete;

  const char *path() const { return path_.c_str(); }
};

struct CaptureStdout {
  std::ostringstream captured;
  std::streambuf *original;

  CaptureStdout() : original(std::cout.rdbuf(captured.rdbuf())) {}
  ~CaptureStdout() { std::cout.rdbuf(original); }

  std::string str() const { return captured.str(); }
};
