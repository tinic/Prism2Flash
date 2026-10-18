/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PRISM2_H
#define PRISM2_H
#include <array>
#include <expected>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>

enum p2_error {
  P2_OK = 0,
  P2_E_ARGUMENT,
  P2_E_REMOVED,
  P2_E_TIMEOUT,
  P2_E_IO,
  P2_E_UNSUPPORTED,
  P2_E_VERIFY,
  P2_E_CANCELLED,
  P2_E_COMMAND,
  P2_E_PDA
};
enum p2_register {
  P2_CMD = 0x00,
  P2_PARAM0 = 0x02,
  P2_PARAM1 = 0x04,
  P2_PARAM2 = 0x06,
  P2_STATUS = 0x08,
  P2_CONTROL = 0x14,
  P2_SELECT0 = 0x18,
  P2_OFFSET0 = 0x1c,
  P2_EVSTAT = 0x30,
  P2_INTEN = 0x32,
  P2_EVACK = 0x34,
  P2_DATA0 = 0x36,
  P2_AUXPAGE = 0x3a,
  P2_AUXOFFSET = 0x3c,
  P2_AUXDATA = 0x3e
};
enum { P2_CFG_COR = 0, P2_CFG_CCSR = 2, P2_EV_CMD = 0x10 };
enum { P2_RID_NICID = 0xfd0b, P2_RID_PRIID = 0xfd02, P2_RID_STAID = 0xfd20 };
#define P2_PDA_SIZE 1024u
#define P2_GENESIS_ADDRESS UINT32_C(0x7e0038)

/* Register values are host integers. AUX payloads are ALWAYS little-endian
 * byte streams. The backend performs register byte swapping exactly once. */
struct p2_bus {
  void *ctx;
  uint16_t (*read16)(void *, uint16_t);
  void (*write16)(void *, uint16_t, uint16_t);
  uint8_t (*read_cfg)(void *, uint8_t);
  void (*write_cfg)(void *, uint8_t, uint8_t);
  void (*delay_us)(void *, uint32_t);
  uint32_t (*now_us)(void *);
  int (*present)(void *);
  int (*cancelled)(void *);
  uint8_t (*read8)(void *, uint16_t) = nullptr;
  void (*write8)(void *, uint16_t, uint8_t) = nullptr;
};
namespace p2 {
struct Identity {
  uint16_t id = 0, variant = 0, major = 0, minor = 0;
  bool operator==(const Identity &) const = default;
};
template <typename T> using CardResult = std::expected<T, p2_error>;
using CardStatus = CardResult<void>;
struct WriteReport {
  p2_error error = P2_OK;
  uint32_t address = 0, last_attempted_address = 0, elapsed_us = 0;
  std::size_t bytes_requested = 0, bytes_written = 0, bytes_verified = 0;
  uint32_t verification_reads = 0, disagreements = 0;
  bool write_started = false;
  std::array<uint8_t, 128> last_readback{};
};
struct ProbeReport {
  unsigned sram_bits = 0;
  uint8_t cor_before = 0, ccsr_before = 0, cor_used = 0;
  std::array<uint8_t, 4> readback{};
  p2_error error = P2_OK;
  p2_error cleanup_error = P2_OK;
  p2_error aux_cleanup_error = P2_OK;
  uint32_t verification_reads = 0, disagreements = 0;
  unsigned genesis_latches = 0;
  bool aux_was_enabled = false, reset_started = false;
  bool io_configuration_restored = false;
  bool aux_state_acquired = false;
};
class Card {
public:
  explicit Card(p2_bus &bus) : bus_(bus) {}
  [[nodiscard]] p2_bus &transport() { return bus_; }
  Card(const Card &) = delete;
  Card &operator=(const Card &) = delete;
  [[nodiscard]] CardStatus enable_aux(bool enable);
  [[nodiscard]] CardResult<uint16_t> aux_control();
  [[nodiscard]] CardStatus ensure_aux_enabled();
  [[nodiscard]] CardStatus read_aux(uint32_t address, std::span<uint8_t> data);
  [[nodiscard]] CardStatus read_nv(uint32_t address, std::span<uint8_t> data);
  // True byte cycles, required by the DOS host-flash JEDEC algorithm.
  // These never substitute word writes or read/modify/write a neighbour.
  [[nodiscard]] CardResult<uint8_t> read_nv_byte(uint32_t address);
  [[nodiscard]] CardStatus write_nv_byte(uint32_t address, uint8_t value);
  [[nodiscard]] CardStatus test_ram_byte_access(uint32_t address);
  [[nodiscard]] CardStatus write_aux(uint32_t address,
                                     std::span<const uint8_t> data);
  // Writes each bounded RAM chunk once, then requires two consecutive exact
  // readbacks. Verification retries never repeat a write. Not a flash engine.
  [[nodiscard]] WriteReport write_verified(uint32_t address,
                                           std::span<const uint8_t> data);
  [[nodiscard]] CardStatus initialize();
  [[nodiscard]] CardResult<Identity> identity(uint16_t rid);
  [[nodiscard]] CardResult<uint32_t>
  read_pda(std::span<uint8_t, P2_PDA_SIZE> data);
  [[nodiscard]] ProbeReport probe(unsigned sram_bits);
  // Split entry/exit for the DOS initial loader. A failed RAM load must keep
  // its recovery state; callers explicitly decide when to leave Genesis.
  [[nodiscard]] ProbeReport enter_genesis(unsigned sram_bits);
  [[nodiscard]] ProbeReport leave_genesis(ProbeReport);
  [[nodiscard]] static bool supports(uint16_t nic_id);

private:
  [[nodiscard]] CardStatus alive() const;
  [[nodiscard]] CardStatus interrupted() const;
  [[nodiscard]] CardStatus wait_mask(uint16_t reg, uint16_t mask,
                                     uint16_t expected, uint32_t timeout);
  [[nodiscard]] CardStatus command(uint16_t cmd, uint16_t param);
  [[nodiscard]] CardStatus transfer(uint32_t address, std::span<uint8_t> read,
                                    std::span<const uint8_t> write,
                                    uint16_t space,
                                    std::optional<uint32_t> started = {});
  [[nodiscard]] CardStatus reset(uint8_t cor, uint8_t hcr);
  [[nodiscard]] CardStatus seek_byte(uint32_t address, uint16_t space);
  [[nodiscard]] CardStatus restore_aux(bool was_enabled);
  p2_bus &bus_;
  bool genesis_active_ = false;
};
} // namespace p2
const char *p2_error_string(int);
int p2_validate_pda(const uint8_t[P2_PDA_SIZE]);
#endif
