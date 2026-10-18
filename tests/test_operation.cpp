/* SPDX-License-Identifier: GPL-2.0-only */
#include "mock.h"
#include "operation.h"
#include "owned_memory.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);             \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
namespace {
enum class Fault {
  None,
  Prepare,
  Open,
  Configure,
  Before,
  RemoveBefore,
  CancelBefore,
  Work,
  CancelWork,
  RemoveWork,
  After,
  RemoveAfter,
  Compare,
  Cleanup,
  Final
};
struct State {
  Fault fault = Fault::None;
  unsigned opens = 0, closes = 0, configurations = 0, cleanups = 0;
  unsigned before = 0, work = 0, after = 0, compares = 0, final = 0;
  unsigned commands = 0, writes = 0, flash = 0;
} state;
p2_mock *work_mock = nullptr;
} // namespace
struct p2_platform {
  p2_mock mock{};
  ~p2_platform() {
    state.commands = mock.command_writes;
    state.writes = mock.payload_writes;
    state.flash = mock.flash_commands;
    ++state.closes;
    p2_mock_free(&mock);
  }
};
int p2_platform_open(p2_platform **out, p2_card_info *info, char *, size_t) {
  CHECK(++state.opens == 1);
  if (state.fault == Fault::Open)
    return P2_E_IO;
  auto platform = p2::make_owned<p2_platform>();
  CHECK(platform && !p2_mock_init(&platform->mock, 16));
  *out = platform.release();
  *info = p2_card_info{0x0274, 0x1613, 0x3e0, 0x100, 1};
  return P2_OK;
}
int p2_platform_configure(p2_platform *, p2_bus *, char *, size_t) {
  std::abort(); // Regular configure must never run inside this transaction.
}
int p2_platform_configure_backup(p2_platform *platform, p2_bus *bus, char *,
                                 size_t) {
  CHECK(++state.configurations == 1);
  if (state.fault == Fault::Configure)
    return P2_E_IO;
  *bus = platform->mock.bus;
  return P2_OK;
}
int p2_platform_finish_backup(p2_platform *) {
  CHECK(!state.closes && ++state.cleanups == 1);
  return state.fault == Fault::Cleanup ? P2_E_VERIFY : P2_OK;
}
void p2_platform_close(p2_platform *platform) {
  p2::DestroyObject<p2_platform>{}(platform);
}
int p2_platform_backup_directory(const char *) { std::abort(); }
const char *p2_platform_description() { std::abort(); }
namespace {
class TestJournal final : public p2::operation::Journal {
public:
  p2::CardStatus prepare() override {
    CHECK(!state.opens);
    return state.fault == Fault::Prepare
               ? p2::CardStatus{std::unexpected{P2_E_IO}}
               : p2::CardStatus{};
  }
  p2::CardStatus capture(p2_bus &bus, p2::operation::Phase phase,
                         const p2_card_info &info) override {
    CHECK(state.opens == 1 && !state.closes && !state.cleanups);
    CHECK(info.manufacturer == 0x0274 && info.product == 0x1613);
    auto &mock = *static_cast<p2_mock *>(bus.ctx);
    work_mock = &mock;
    CHECK(mock.flash_commands == 0);
    if (phase == p2::operation::Phase::Before) {
      CHECK(++state.before == 1 && !state.work && !state.after);
      CHECK(!mock.command_writes && !mock.payload_writes &&
            !mock.configuration_writes);
      if (state.fault == Fault::Before)
        return std::unexpected{P2_E_IO};
      if (state.fault == Fault::RemoveBefore)
        mock.present = 0;
      if (state.fault == Fault::CancelBefore)
        mock.cancel = 1;
    } else {
      CHECK(++state.after == 1 && state.work == 1 && state.before == 1);
      CHECK(bus.cancelled == nullptr && bus.present(bus.ctx));
      if (state.fault == Fault::After)
        return std::unexpected{P2_E_IO};
      if (state.fault == Fault::RemoveAfter)
        mock.present = 0;
    }
    return {};
  }
  p2::CardStatus compare() override {
    CHECK(++state.compares == 1 && state.before == 1 && state.after == 1);
    CHECK(!state.closes && !state.cleanups);
    return state.fault == Fault::Compare
               ? p2::CardStatus{std::unexpected{P2_E_VERIFY}}
               : p2::CardStatus{};
  }
  p2::CardStatus finish(const p2::operation::Report &report) override {
    CHECK(++state.final == 1 && !state.closes);
    CHECK(report.operation_started == (state.work != 0));
    CHECK(report.readback_attempted == (state.after != 0));
    return state.fault == Fault::Final
               ? p2::CardStatus{std::unexpected{P2_E_IO}}
               : p2::CardStatus{};
  }
};
p2::CardStatus work(p2::Card &card, void *) {
  CHECK(++state.work == 1 && state.before == 1 && !state.after &&
        !state.closes);
  if (state.fault == Fault::Work)
    return std::unexpected{P2_E_COMMAND};
  if (state.fault == Fault::CancelWork)
    work_mock->cancel = 1;
  if (state.fault == Fault::RemoveWork)
    work_mock->present = 0;
  return card.initialize();
}
} // namespace
int main() {
  // Every phase failure independently blocks success; failures before work
  // never issue a command or payload write. Ownership is held until final log.
  constexpr std::array faults{
      Fault::None,         Fault::Prepare, Fault::Open,
      Fault::Configure,    Fault::Before,  Fault::RemoveBefore,
      Fault::CancelBefore, Fault::Work,    Fault::CancelWork,
      Fault::RemoveWork,   Fault::After,   Fault::RemoveAfter,
      Fault::Compare,      Fault::Cleanup, Fault::Final};
  for (const auto fault : faults) {
    state = State{fault};
    TestJournal journal;
    const auto result = p2::operation::run(journal, work, nullptr);
    CHECK(result.success() == (fault == Fault::None));
    CHECK(!state.flash);
    const bool should_work =
        fault == Fault::None || fault == Fault::Work || fault == Fault::After ||
        fault == Fault::RemoveAfter || fault == Fault::Compare ||
        fault == Fault::Cleanup || fault == Fault::Final ||
        fault == Fault::CancelWork || fault == Fault::RemoveWork;
    CHECK(state.work == unsigned(should_work));
    CHECK(state.after == unsigned(should_work && fault != Fault::RemoveWork));
    if (fault == Fault::CancelWork)
      CHECK(result.operation_error == P2_E_CANCELLED &&
            result.readback_attempted);
    if (fault == Fault::RemoveWork)
      CHECK(result.operation_error == P2_E_REMOVED &&
            !result.readback_attempted &&
            result.readback_error == P2_E_REMOVED);
    if (!should_work)
      CHECK(!state.commands && !state.writes);
    CHECK(state.opens == unsigned(fault != Fault::Prepare));
    CHECK(state.closes ==
          unsigned(fault != Fault::Prepare && fault != Fault::Open));
  }
  state = {};
  TestJournal journal;
  const auto invalid = p2::operation::run(journal, nullptr, nullptr);
  CHECK(!invalid.success() && invalid.preparation_error == P2_E_ARGUMENT &&
        !state.opens);
  for (bool removed : {false, true}) {
    state = State{removed ? Fault::RemoveWork : Fault::Work};
    TestJournal recovery_journal;
    unsigned held = 0;
    const p2::operation::Recovery recovery{
        [](void *, const p2::operation::Report &) { return true; },
        [](p2::Card &card, void *ctx) {
          // Journal/readback already persisted; the slot has not been
          // configured down or released when the recovery callback runs.
          CHECK(state.final == 1 && !state.cleanups && !state.closes);
          CHECK(state.after == 1 &&
                card.transport().present(card.transport().ctx));
          ++*static_cast<unsigned *>(ctx);
        }};
    const auto result =
        p2::operation::run(recovery_journal, work, &held, recovery);
    CHECK(!result.success() && held == unsigned(!removed));
    CHECK(result.recovery_held == !removed);
    CHECK(state.final == 1 && state.closes == 1);
  }
  std::puts("Owned transaction: preflight/acquisition/file-gate/operation/"
            "readback/comparison/cleanup/journal faults refuse success");
}
