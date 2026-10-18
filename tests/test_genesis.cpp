/* SPDX-License-Identifier: GPL-2.0-only */
#include "mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);               \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

template <typename T> static p2_error code(const p2::CardResult<T> &result) {
  return result ? P2_OK : result.error();
}

static void success(unsigned bits) {
  struct p2_mock m;
  p2::ProbeReport result;

  CHECK(p2_mock_init(&m, bits) == 0);
  CHECK(p2::Card{m.bus}.initialize());
  auto id = p2::Card{m.bus}.identity(P2_RID_NICID);
  CHECK(id);
  CHECK(id->id == 0x801b && id->major == 1);
  CHECK((result = p2::Card{m.bus}.probe(bits)).error == 0);
  CHECK(result.readback[0] == 0 && result.readback[1] == 0xe1 &&
        result.readback[2] == 0xa1 && result.readback[3] == 0xff);
  CHECK(m.genesis_entries == 2 && m.normal_returns == 1);
  CHECK(result.genesis_latches == 2);
  // Original DOS trace has two Genesis HCR latches, followed by one normal
  // latch. Native x16 uses DOS's literal COR45/C5; x8 is model-only. Allow
  // 5 ms settling after deassertion; no preliminary bare COR pulse.
  CHECK(m.configuration_writes == 9);
  const uint8_t genesis = bits == 16 ? 0x0f : 0x1f;
  const uint8_t normal = bits == 16 ? 0x07 : 0x17;
  const uint8_t cor = bits == 16 ? 0x45 : 0x41;
  const uint8_t reset = cor | 0x80;
  const uint8_t values[]{reset, genesis, cor,    reset, genesis,
                         cor,   reset,   normal, cor};
  for (unsigned i = 0; i < 9; ++i) {
    CHECK(m.cfg_registers[i] == (i % 3 == 1 ? P2_CFG_CCSR : P2_CFG_COR));
    CHECK(m.cfg_values[i] == values[i]);
  }
  for (unsigned start : {0u, 3u}) {
    CHECK(m.cfg_times[start + 1] == m.cfg_times[start]);
    CHECK(m.cfg_times[start + 2] - m.cfg_times[start + 1] == 5000);
    CHECK(m.cfg_times[start + 3] - m.cfg_times[start + 2] >= 5000);
  }
  CHECK(m.cfg_times[7] - m.cfg_times[6] == 5000);
  CHECK(m.cfg_times[8] - m.cfg_times[7] == 5000);
  CHECK(m.cor == cor && m.ccsr == (bits == 8 ? 0x17 : 0x07));
  CHECK((m.regs[P2_CONTROL / 2] & 0xc000) == 0 && result.cleanup_error == 0);
  CHECK(m.flash_commands == 0);
  p2_mock_free(&m);
}
static void transfer(void) {
  struct p2_mock m;
  uint8_t input[260], output[260];
  unsigned i;
  CHECK(p2_mock_init(&m, 8) == 0);
  for (i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(i * 13 + 7);
  CHECK(code(p2::Card{m.bus}.write_aux(
            0x7e007e, std::span{input}.first(sizeof(input)))) == 0);
  CHECK(!memcmp(m.memory + 0x7e007e, input, sizeof(input)));
  CHECK(code(p2::Card{m.bus}.read_aux(
            0x7e007e, std::span{output}.first(sizeof(output)))) == 0);
  CHECK(!memcmp(input, output, sizeof(input)));
  CHECK(code(p2::Card{m.bus}.write_aux(1, std::span{input}.first(2))) ==
        P2_E_ARGUMENT);
  CHECK(code(p2::Card{m.bus}.write_aux(0x800000, std::span{input}.first(2))) ==
        P2_E_ARGUMENT);
  CHECK(code(p2::Card{m.bus}.read_aux(0x7ffffe, std::span{output}.first(4))) ==
        P2_E_ARGUMENT);
  CHECK(code(p2::Card{m.bus}.write_aux(0, std::span{input}.first(3))) ==
        P2_E_ARGUMENT);
  CHECK(m.flash_commands == 0);
  p2_mock_free(&m);
}
static void failures(void) {
  struct p2_mock m;
  p2::ProbeReport result;

  CHECK(p2_mock_init(&m, 8) == 0);
  CHECK((result = p2::Card{m.bus}.probe(0)).error == P2_E_ARGUMENT);
  CHECK(m.operations == 0);
  CHECK((result = p2::Card{m.bus}.probe(16)).error == P2_E_VERIFY);
  CHECK(m.normal_returns == 1 && !result.cleanup_error);
  m.corrupt_readback = 1;
  CHECK((result = p2::Card{m.bus}.probe(8)).error == P2_E_VERIFY);
  CHECK(m.normal_returns == 2);
  m.corrupt_readback = 0;
  m.clock = UINT32_MAX - 100;
  m.stuck_command = 1;
  CHECK(code(p2::Card{m.bus}.initialize()) == P2_E_TIMEOUT);
  CHECK((uint32_t)(m.clock - (UINT32_MAX - 100)) <= 5010);
  m.stuck_command = 0;
  m.stuck_aux = 1;
  CHECK(code(p2::Card{m.bus}.enable_aux(true)) == P2_E_TIMEOUT);
  m.stuck_aux = 0;
  m.bad_rid_length = 1;
  CHECK(p2::Card{m.bus}.identity(P2_RID_NICID).error() == P2_E_IO);
  m.bad_rid_length = 0;
  m.command_failure = 1;
  CHECK(code(p2::Card{m.bus}.initialize()) == P2_E_COMMAND);
  CHECK(m.flash_commands == 0);
  p2_mock_free(&m);
}
static void removal_and_cancel(void) {
  unsigned i;
  /* Exercise removal and cancellation at every protocol I/O boundary. */
  for (i = 1; i < 65; ++i) {
    struct p2_mock m;
    p2::ProbeReport result;
    int rc;
    CHECK(p2_mock_init(&m, 8) == 0);
    m.remove_at = i;
    rc = (result = p2::Card{m.bus}.probe(8)).error;
    if (rc != P2_E_REMOVED && rc != P2_E_TIMEOUT && rc != P2_OK)
      fprintf(stderr, "Removal boundary %u returned %d\n", i, rc);
    CHECK(rc == P2_E_REMOVED || rc == P2_E_TIMEOUT || rc == P2_OK);
    CHECK(m.flash_commands == 0);
    p2_mock_free(&m);
    CHECK(p2_mock_init(&m, 8) == 0);
    m.cancel_at = i;
    rc = (result = p2::Card{m.bus}.probe(8)).error;
    CHECK(rc == P2_E_CANCELLED || rc == P2_OK);
    CHECK((m.cor & 0x80) == 0);
    CHECK(m.flash_commands == 0);
    p2_mock_free(&m);
  }
}
static void pda(void) {
  struct p2_mock m;
  uint8_t data[P2_PDA_SIZE];
  uint32_t addr;
  CHECK(p2_mock_init(&m, 8) == 0);
  auto location = p2::Card{m.bus}.read_pda(data);
  CHECK(location);
  addr = *location;
  CHECK(addr == 0x7f0000);
  CHECK(!memcmp(data, m.memory + addr, sizeof(data)));
  data[4] ^= 1;
  CHECK(p2_validate_pda(data) == P2_E_PDA);
  memset(data, 0xff, sizeof(data));
  CHECK(p2_validate_pda(data) == P2_E_PDA);
  CHECK(p2::Card::supports(0x801b));
  CHECK(!p2::Card::supports(0x800c));
  CHECK(!p2::Card::supports(0x8008));
  CHECK(!p2::Card::supports(0x001f));
  p2_mock_free(&m);
  CHECK(p2_mock_init(&m, 8) == 0);
  m.cancel_at = 20;
  CHECK(code(p2::Card{m.bus}.read_pda(data)) == P2_E_CANCELLED);
  CHECK((m.regs[P2_CONTROL / 2] & 0xc000) == 0);
  p2_mock_free(&m);
}
int main(void) {
  success(8);
  success(16);
  transfer();
  failures();
  removal_and_cancel();
  pda();
  puts("Genesis protocol checks passed (mock backend; no hardware tested)");
  return 0;
}
