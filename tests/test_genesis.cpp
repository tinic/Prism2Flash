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

static void success(unsigned bits) {
  struct p2_mock m;
  struct p2_probe_result result;
  struct p2_identity id;
  CHECK(p2_mock_init(&m, bits) == 0);
  CHECK(p2_initialize(&m.bus) == 0);
  CHECK(p2_identity(&m.bus, P2_RID_NICID, &id) == 0);
  CHECK(id.id == 0x801b && id.major == 1);
  CHECK(p2_genesis_probe(&m.bus, bits, &result) == 0);
  CHECK(result.readback[0] == 0 && result.readback[1] == 0xe1 &&
        result.readback[2] == 0xa1 && result.readback[3] == 0xff);
  CHECK(m.genesis_entries == 1 && m.normal_returns == 1);
  CHECK(m.cor == 0x41 && m.ccsr == (bits == 8 ? 0x17 : 0x07));
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
  CHECK(p2_aux_write(&m.bus, 0x7e007e, input, sizeof(input)) == 0);
  CHECK(!memcmp(m.memory + 0x7e007e, input, sizeof(input)));
  CHECK(p2_aux_read(&m.bus, 0x7e007e, output, sizeof(output)) == 0);
  CHECK(!memcmp(input, output, sizeof(input)));
  CHECK(p2_aux_write(&m.bus, 1, input, 2) == P2_E_ARGUMENT);
  CHECK(p2_aux_write(&m.bus, 0x800000, input, 2) == P2_E_ARGUMENT);
  CHECK(p2_aux_read(&m.bus, 0x7ffffe, output, 4) == P2_E_ARGUMENT);
  CHECK(p2_aux_write(&m.bus, 0, input, 3) == P2_E_ARGUMENT);
  CHECK(m.flash_commands == 0);
  p2_mock_free(&m);
}
static void failures(void) {
  struct p2_mock m;
  struct p2_probe_result result;
  struct p2_identity id;
  CHECK(p2_mock_init(&m, 8) == 0);
  CHECK(p2_genesis_probe(&m.bus, 0, &result) == P2_E_ARGUMENT);
  CHECK(m.operations == 0);
  CHECK(p2_genesis_probe(&m.bus, 16, &result) == P2_E_VERIFY);
  CHECK(m.normal_returns == 1 && !result.cleanup_error);
  m.corrupt_readback = 1;
  CHECK(p2_genesis_probe(&m.bus, 8, &result) == P2_E_VERIFY);
  CHECK(m.normal_returns == 2);
  m.corrupt_readback = 0;
  m.clock = UINT32_MAX - 100;
  m.stuck_command = 1;
  CHECK(p2_initialize(&m.bus) == P2_E_TIMEOUT);
  CHECK((uint32_t)(m.clock - (UINT32_MAX - 100)) <= 5010);
  m.stuck_command = 0;
  m.stuck_aux = 1;
  CHECK(p2_aux_enable(&m.bus, 1) == P2_E_TIMEOUT);
  m.stuck_aux = 0;
  m.bad_rid_length = 1;
  CHECK(p2_identity(&m.bus, P2_RID_NICID, &id) == P2_E_IO);
  m.bad_rid_length = 0;
  m.command_failure = 1;
  CHECK(p2_initialize(&m.bus) == P2_E_COMMAND);
  CHECK(m.flash_commands == 0);
  p2_mock_free(&m);
}
static void removal_and_cancel(void) {
  unsigned i;
  /* Exercise removal and cancellation at every protocol I/O boundary. */
  for (i = 1; i < 65; ++i) {
    struct p2_mock m;
    struct p2_probe_result result;
    int rc;
    CHECK(p2_mock_init(&m, 8) == 0);
    m.remove_at = i;
    rc = p2_genesis_probe(&m.bus, 8, &result);
    if (rc != P2_E_REMOVED && rc != P2_E_TIMEOUT && rc != P2_OK)
      fprintf(stderr, "Removal boundary %u returned %d\n", i, rc);
    CHECK(rc == P2_E_REMOVED || rc == P2_E_TIMEOUT || rc == P2_OK);
    CHECK(m.flash_commands == 0);
    p2_mock_free(&m);
    CHECK(p2_mock_init(&m, 8) == 0);
    m.cancel_at = i;
    rc = p2_genesis_probe(&m.bus, 8, &result);
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
  CHECK(p2_read_pda(&m.bus, data, &addr) == P2_OK);
  CHECK(addr == 0x7f0000);
  CHECK(!memcmp(data, m.memory + addr, sizeof(data)));
  data[4] ^= 1;
  CHECK(p2_validate_pda(data) == P2_E_PDA);
  memset(data, 0xff, sizeof(data));
  CHECK(p2_validate_pda(data) == P2_E_PDA);
  CHECK(p2_supported_nic(0x801b));
  CHECK(!p2_supported_nic(0x800c));
  CHECK(!p2_supported_nic(0x8008));
  CHECK(!p2_supported_nic(0x001f));
  p2_mock_free(&m);
  CHECK(p2_mock_init(&m, 8) == 0);
  m.cancel_at = 20;
  CHECK(p2_read_pda(&m.bus, data, &addr) == P2_E_CANCELLED);
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
