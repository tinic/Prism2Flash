/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef P2_PLATFORM_H
#define P2_PLATFORM_H
#include "prism2.h"
#include <memory>
struct p2_platform;
struct p2_card_info {
  uint16_t manufacturer, product;
  uint32_t config_base, io_base;
  uint8_t config_index;
};
int p2_platform_open(struct p2_platform **, struct p2_card_info *, char *error,
                     size_t error_size);
int p2_platform_configure(struct p2_platform *, struct p2_bus *, char *error,
                          size_t error_size);
/* Non-resetting acquisition path: no INIT, reset, audio/pin or CCSR writes. */
int p2_platform_configure_backup(struct p2_platform *, struct p2_bus *,
                                 char *error, size_t error_size);
int p2_platform_backup_directory(const char *path);
int p2_platform_finish_backup(struct p2_platform *);
void p2_platform_close(struct p2_platform *);
const char *p2_platform_description(void);

namespace p2 {
struct PlatformError {
  int code = P2_E_IO;
  std::array<char, 192> message{};
};
// Scoped ownership around the AmigaOS/SDK boundary. Backend close retains the
// native release order and sticky-removal checks. Backup restoration is checked
// explicitly before close; destruction remains the early-exit fallback.
class Slot {
public:
  Slot(Slot &&) = default;
  Slot &operator=(Slot &&) = default;
  Slot(const Slot &) = delete;
  Slot &operator=(const Slot &) = delete;
  static std::expected<Slot, PlatformError> open() {
    Slot slot;
    PlatformError error;
    p2_platform *platform = nullptr;
    error.code = p2_platform_open(&platform, &slot.info_, error.message.data(),
                                  error.message.size());
    slot.platform_.reset(platform);
    if (error.code)
      return std::unexpected{error};
    return slot;
  }
  std::expected<p2_bus, PlatformError> configure(bool backup = false) {
    p2_bus bus{};
    PlatformError error;
    const auto configure =
        backup ? p2_platform_configure_backup : p2_platform_configure;
    error.code = configure(platform_.get(), &bus, error.message.data(),
                           error.message.size());
    if (error.code)
      return std::unexpected{error};
    return bus;
  }
  [[nodiscard]] int finish_backup() {
    return p2_platform_finish_backup(platform_.get());
  }
  [[nodiscard]] const p2_card_info &info() const { return info_; }
  void close() { platform_.reset(); }

private:
  Slot() = default;
  struct Release {
    void operator()(p2_platform *p) const { p2_platform_close(p); }
  };
  std::unique_ptr<p2_platform, Release> platform_;
  p2_card_info info_{};
};
} // namespace p2

#endif
