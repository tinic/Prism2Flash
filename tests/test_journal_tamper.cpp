/* SPDX-License-Identifier: GPL-2.0-only */
#include "file.h"
#include "operation.h"
#include <cstdio>
#include <cstdlib>

// Decorate the real file journal: corrupt exactly one saved file after its
// phase verification, while the transaction still owns the insertion.
class TamperJournal final : public p2::operation::Journal {
public:
  TamperJournal(const char *directory, const char *target) noexcept
      : inner_(p2::operation::make_model_journal(directory, "20261018T104000Z",
                                                 "inspect")),
        target_(target) {}
  p2::CardStatus prepare() override { return inner_->prepare(); }
  p2::CardStatus capture(p2_bus &bus, p2::operation::Phase phase,
                         const p2_card_info &info) override {
    auto result = inner_->capture(bus, phase, info);
    if (result && phase == p2::operation::Phase::After) {
      auto file = p2::File::open(target_, "r+b");
      if (!file || fputc(0, file.get()) == EOF || !file.close())
        std::abort();
    }
    return result;
  }
  p2::CardStatus compare() override { return inner_->compare(); }
  p2::CardStatus finish(const p2::operation::Report &report) override {
    return inner_->finish(report);
  }

private:
  p2::Owned<p2::operation::Journal> inner_;
  const char *target_;
};
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  TamperJournal journal{argv[1], argv[2]};
  auto result = p2::operation::run(
      journal, [](p2::Card &card, void *) { return card.initialize(); },
      nullptr);
  if (!result.operation_started || !result.readback_attempted ||
      result.acquisition_error || result.operation_error ||
      result.readback_error || result.cleanup_error || result.journal_error ||
      result.success() || result.comparison_error != P2_E_VERIFY)
    return 1;
  return 0;
}
