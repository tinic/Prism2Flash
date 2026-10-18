/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <memory>
#include <stdio.h>

namespace p2 {
// Owning stream. close() is explicit wherever a delayed write error matters;
// destruction is the fallback for early returns and read-only streams.
class File {
public:
  File() = default;
  [[nodiscard]] static File open(const char *path, const char *mode) {
    return File{fopen(path, mode)};
  }
  [[nodiscard]] bool close() {
    auto *file = stream_.release();
    return !file || fclose(file) == 0;
  }
  explicit operator bool() const { return stream_ != nullptr; }
  FILE *get() const { return stream_.get(); }
  FILE &operator*() const { return *stream_; }

private:
  struct Close {
    void operator()(FILE *file) const { fclose(file); }
  };
  explicit File(FILE *file) : stream_(file) {}
  std::unique_ptr<FILE, Close> stream_;
};
} // namespace p2
