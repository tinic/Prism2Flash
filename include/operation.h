/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "platform.h"

namespace p2::operation {
enum class Phase { Before, After };
struct Report {
  int preparation_error = 0, acquisition_error = 0, operation_error = 0;
  int readback_error = 0, comparison_error = 0, cleanup_error = 0;
  int journal_error = 0;
  bool operation_started = false, readback_attempted = false;
  [[nodiscard]] bool success() const {
    return operation_started && readback_attempted && !preparation_error &&
           !acquisition_error && !operation_error && !readback_error &&
           !comparison_error && !cleanup_error && !journal_error;
  }
};
// A journal must save, close and reopen-verify both raw captures and the phase
// manifest before capture() succeeds. No persisted flag authorizes a session.
// The only production implementation currently models an 8 MiB linear NV map;
// it is not an approved physical-card profile or a native enabling switch.
class Journal {
public:
  virtual ~Journal() = default;
  virtual CardStatus prepare() = 0;
  virtual CardStatus capture(p2_bus &, Phase, const p2_card_info &) = 0;
  virtual CardStatus compare() = 0;
  virtual CardStatus finish(const Report &) = 0;
};
using Work = CardStatus (*)(Card &, void *);
// Own once, configure only the non-reset acquisition path, retain ownership
// through file verification, operation and readback. Never reclaim/retry/reset.
[[nodiscard]] Report run(Journal &, Work, void *context);
// Host model only. Native commands remain blocked before any backend call.
int run_model(const char *directory, const char *utc, const char *operation,
              Work, void *context);
} // namespace p2::operation
