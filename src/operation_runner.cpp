/* SPDX-License-Identifier: GPL-2.0-only */
#include "operation.h"
namespace p2::operation {
namespace {
int code(const CardStatus &s) { return s ? P2_OK : s.error(); }
int available(p2_bus &bus, bool cancellation = true) {
  if (!bus.present(bus.ctx))
    return P2_E_REMOVED;
  if (cancellation && bus.cancelled && bus.cancelled(bus.ctx))
    return P2_E_CANCELLED;
  return P2_OK;
}
} // namespace
Report run(Journal &journal, Work work, void *context, Recovery recovery) {
  Report result;
  bool journal_finished = false;
  if (!work) {
    result.preparation_error = P2_E_ARGUMENT;
    return result;
  }
  result.preparation_error = code(journal.prepare());
  if (result.preparation_error) {
    result.journal_error = code(journal.finish(result));
    return result;
  }
  // Slot destruction cannot perform a regular configure/close reset: this
  // session only ever uses configure_backup and finish_backup.
  {
    auto slot = Slot::open();
    if (!slot)
      result.acquisition_error = slot.error().code;
    else {
      auto bus = slot->configure(true);
      if (!bus)
        result.acquisition_error = bus.error().code;
      else {
        result.acquisition_error = available(*bus);
        if (!result.acquisition_error)
          result.acquisition_error =
              code(journal.capture(*bus, Phase::Before, slot->info()));
        // Removal/cancellation during slow file verification cannot authorize
        // work. The backend's removal flag stays sticky for this whole claim.
        if (!result.acquisition_error)
          result.acquisition_error = available(*bus);
        if (!result.acquisition_error) {
          Card card{*bus};
          result.operation_started = true;
          result.operation_error = code(work(card, context));
          if (available(*bus, false))
            result.readback_error = P2_E_REMOVED;
          else {
            // A user's Ctrl-C stops the operation, but does not suppress the
            // required evidence readback. Never mask sticky removal.
            auto recovery_bus = *bus;
            recovery_bus.cancelled = nullptr;
            result.readback_attempted = true;
            result.readback_error =
                code(journal.capture(recovery_bus, Phase::After, slot->info()));
            if (!result.readback_error)
              result.readback_error = available(recovery_bus, false);
            if (!result.readback_error)
              result.comparison_error = code(journal.compare());
          }
          if (recovery.required && recovery.hold &&
              recovery.required(context, result) && !available(*bus, false)) {
            // Persist results while still owning the recovery environment.
            // The hook can only return after explicit recovery/release; never
            // reset or release a partially programmed card automatically.
            result.recovery_held = true;
            result.journal_error = code(journal.finish(result));
            journal_finished = true;
            recovery.hold(card, context);
          }
        }
      }
      result.cleanup_error = slot->finish_backup();
    }
    if (!journal_finished)
      result.journal_error = code(journal.finish(result));
  }
  return result;
}

} // namespace p2::operation
