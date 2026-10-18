/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "mock.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                  \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
template <typename T> static p2_error code(const p2::CardResult<T> &result) {
  return result ? P2_OK : result.error();
}

static void hash_tests(void) {
  p2::Sha256 s;
  std::array<char, 65> out;
  unsigned i;
  s = p2::Sha256{};
  out = s.hex();
  CHECK(!strcmp(
      out.data(),
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  s = p2::Sha256{};
  s.update("a");
  s.update("bc");
  out = s.hex();
  CHECK(!strcmp(
      out.data(),
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  s = p2::Sha256{};
  for (i = 0; i < 1000000; ++i)
    s.update("a");
  out = s.hex();
  CHECK(!strcmp(
      out.data(),
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
static void pda_oracle_test(void) {
  /* Original synthetic prefix; 0xe1a3 independently calculated using the
   * retained Host AP util.c CRC table, not the fixture generator. */
  uint8_t pda[P2_PDA_SIZE] = {2, 0, 8, 0, 0x1b, 0x80, 2, 0, 0, 0, 0xa3, 0xe1};
  CHECK(!p2_validate_pda(pda));
  pda[10] ^= 1;
  CHECK(p2_validate_pda(pda) == P2_E_PDA);
}
static void init_nv(struct p2_mock *m) {
  uint32_t i;
  CHECK(!p2_mock_init(m, 0));
  m->nv_memory = static_cast<uint8_t *>(malloc(p2::backup::NvBytes));
  CHECK(m->nv_memory);
  for (i = 0; i < p2::backup::NvBytes; ++i)
    m->nv_memory[i] = (uint8_t)(i * 17 + (i >> 8) * 3);
}
static void transfer_tests(void) {
  struct p2_mock m;
  uint8_t b[260];
  unsigned ops;
  init_nv(&m);
  m.nv_selector_hides_control = true;
  m.regs[P2_CONTROL / 2] = 0xc000;
  CHECK(!code(p2::Card{m.bus}.read_nv(126, b)));
  CHECK(!memcmp(b, m.nv_memory + 126, sizeof(b)) && m.aux_space == 0);
  CHECK(!code(p2::Card{m.bus}.ensure_aux_enabled()));
  m.aux_space = 1; // Reproduce the retained selector from an older caller.
  CHECK(!code(p2::Card{m.bus}.ensure_aux_enabled()) && m.aux_space == 0);
  CHECK(!code(p2::Card{m.bus}.read_nv(0x7ffffe, std::span{b}.first(2))));
  CHECK(!memcmp(b, m.nv_memory + 0x7ffffe, 2));
  ops = m.operations;
  CHECK(code(p2::Card{m.bus}.read_nv(1, std::span{b}.first(2))) ==
        P2_E_ARGUMENT);
  CHECK(code(p2::Card{m.bus}.read_nv(0x7ffffe, std::span{b}.first(4))) ==
        P2_E_ARGUMENT);
  CHECK(code(p2::Card{m.bus}.read_nv(0x800000, std::span{b}.first(0))) ==
        P2_E_ARGUMENT);
  CHECK(code(p2::Card{m.bus}.read_nv(0, std::span{b}.first(3))) ==
        P2_E_ARGUMENT);
  CHECK(m.operations == ops && !m.payload_writes && !m.command_writes &&
        !m.configuration_writes);
  p2_mock_free(&m);
}
static void capture_tests(int enabled, int mismatch, bool hidden = false) {
  struct p2_mock m;
  p2::backup::CaptureReport result;
  FILE *a = tmpfile(), *b = tmpfile();
  uint16_t regs[5] = {0x1234, 0x5678, 0xabcd, 0x4444, 0x2020};
  unsigned i;
  static const uint16_t regids[] = {P2_PARAM0, P2_PARAM1, P2_PARAM2, P2_AUXPAGE,
                                    P2_AUXOFFSET};
  CHECK(a && b);
  init_nv(&m);
  m.regs[P2_CONTROL / 2] = enabled ? 0xc000 : 0;
  m.nv_selector_hides_control = hidden;
  m.aux_space = hidden ? 1 : 0;
  for (i = 0; i < 5; ++i)
    m.regs[regids[i] / 2] = regs[i];
  if (mismatch)
    m.corrupt_nv_after = p2::backup::NvBytes;
  else
    m.corrupt_nv_once_at = 5;
  CHECK((result = p2::backup::capture(m.bus, *a, *b)).error ==
        (mismatch ? P2_E_VERIFY : P2_OK));
  CHECK(result.bytes[0] == p2::backup::NvBytes);
  CHECK(result.bytes[1] == p2::backup::NvBytes);
  CHECK((result.mismatch_bytes != 0) == (mismatch != 0));
  if (!mismatch)
    CHECK(result.page_disagreements == 1 &&
          result.page_reads == 4 * (p2::backup::NvBytes / 128) + 1);
  if (!mismatch)
    CHECK(strlen(result.sha256[0].data()) == 64 &&
          !strcmp(result.sha256[0].data(), result.sha256[1].data()));
  CHECK(!result.cleanup_error &&
        m.regs[P2_CONTROL / 2] == (enabled ? 0xc000 : 0));
  for (i = 0; i < 3; ++i)
    CHECK(m.regs[regids[i] / 2] == (enabled ? regs[i] : 0));
  CHECK(m.regs[P2_AUXOFFSET / 2] == 0 && !m.scratch_writes_after_disable);
  CHECK(!m.command_writes && !m.payload_writes && !m.configuration_writes &&
        !m.flash_commands);
  fclose(a);
  fclose(b);
  p2_mock_free(&m);
}
static void single_capture_tests() {
  for (bool corrupt_alias : {false, true}) {
    p2_mock m;
    init_nv(&m);
    m.nv_selector_hides_control = true;
    if (!corrupt_alias)
      m.nv_unstable_until = m.clock + 2000000;
    if (corrupt_alias)
      m.nv_memory[0xe8258] ^= 0x20;
    FILE *file = tmpfile();
    CHECK(file);
    const auto result = p2::backup::capture_single(m.bus, *file, 0x20000);
    CHECK(result.error == (corrupt_alias ? P2_E_VERIFY : P2_OK));
    CHECK(result.bytes[0] == p2::backup::NvBytes && !result.bytes[1]);
    CHECK(result.mismatch_bytes == (corrupt_alias ? 1u : 0u));
    CHECK(!corrupt_alias || result.first_mismatch == 0xe8258);
    CHECK(!m.payload_writes && !m.command_writes && !m.configuration_writes);
    CHECK(ftell(file) == p2::backup::NvBytes);
    fclose(file);
    p2_mock_free(&m);
  }
}
static void lost_aux_tests() {
  for (bool enabled : {false, true}) {
    p2_mock m;
    init_nv(&m);
    m.regs[P2_CONTROL / 2] = enabled ? 0xc000 : 0;
    m.drop_aux_at_nv_read = 8;
    FILE *file = tmpfile();
    CHECK(file);
    auto result = p2::backup::capture_single(m.bus, *file, 0x20000);
    CHECK(result.error == P2_E_VERIFY && result.aux_losses == 1);
    CHECK(!result.bytes[0] && !m.regs[P2_CONTROL / 2]);
    CHECK(m.aux_enables == (enabled ? 0u : 1u));
    CHECK(result.cleanup_error == (enabled ? P2_E_VERIFY : P2_OK));
    CHECK(!m.payload_writes && !m.command_writes && !m.configuration_writes);
    fclose(file);
    p2_mock_free(&m);
  }
  p2_mock m;
  init_nv(&m);
  // Disable at the last word of an agreeing second page read. Its legitimate
  // FF payload would otherwise hide the loss until the next page.
  m.nv_memory[126] = m.nv_memory[127] = 0xff;
  m.drop_aux_at_nv_read = 128;
  FILE *file = tmpfile();
  CHECK(file);
  const auto result = p2::backup::capture_single(m.bus, *file, 0x20000);
  CHECK(result.error == P2_E_VERIFY && result.aux_losses == 1);
  CHECK(result.page_reads == 2 && m.nv_reads == 128 && !result.bytes[0]);
  CHECK(m.aux_enables == 1 && !m.regs[P2_CONTROL / 2]);
  fclose(file);
  p2_mock_free(&m);
}
static void failure_tests(void) {
  struct p2_mock m;
  p2::backup::CaptureReport result;
  FILE *a, *b;
  unsigned i;
  for (i = 1; i < 140; i += 9) {
    CHECK(!p2_mock_init(&m, 0));
    m.remove_at = i;
    a = tmpfile();
    b = tmpfile();
    CHECK(a && b);
    CHECK((result = p2::backup::capture(m.bus, *a, *b)).error == P2_E_REMOVED);
    CHECK(!m.command_writes && !m.payload_writes && !m.configuration_writes);
    fclose(a);
    fclose(b);
    p2_mock_free(&m);
  }
  CHECK(!p2_mock_init(&m, 0));
  m.cancel_at = 60;
  a = tmpfile();
  b = tmpfile();
  CHECK(a && b);
  CHECK((result = p2::backup::capture(m.bus, *a, *b)).error == P2_E_CANCELLED);
  CHECK(!result.cleanup_error && !m.command_writes && !m.payload_writes);
  fclose(a);
  fclose(b);
  p2_mock_free(&m);
  CHECK(!p2_mock_init(&m, 0));
  m.stuck_aux = 1;
  a = tmpfile();
  b = tmpfile();
  CHECK(a && b);
  CHECK((result = p2::backup::capture(m.bus, *a, *b)).error == P2_E_TIMEOUT);
  CHECK(result.cleanup_error == P2_E_TIMEOUT && m.clock < 300000 &&
        !m.command_writes && !m.payload_writes);
  fclose(a);
  fclose(b);
  p2_mock_free(&m);
  init_nv(&m);
  m.unstable_nv = 1;
  a = tmpfile();
  b = tmpfile();
  CHECK(a && b);
  CHECK((result = p2::backup::capture(m.bus, *a, *b)).error == P2_E_VERIFY);
  CHECK(result.page_reads == 20 && result.page_disagreements == 19 &&
        !result.bytes[0]);
  CHECK(!result.cleanup_error && !m.command_writes && !m.payload_writes);
  fclose(a);
  fclose(b);
  p2_mock_free(&m);
}
int main(void) {
  hash_tests();
  pda_oracle_test();
  transfer_tests();
  capture_tests(0, 0);
  capture_tests(1, 0);
  capture_tests(1, 0, true);
  capture_tests(0, 1);
  single_capture_tests();
  lost_aux_tests();
  failure_tests();
  CHECK(p2::backup::valid_utc("20261018T104000Z"));
  CHECK(p2::backup::valid_utc("20240229T235959Z"));
  CHECK(!p2::backup::valid_utc("20260229T104000Z"));
  CHECK(!p2::backup::valid_utc("20261018T246000Z"));
  CHECK(!p2::backup::valid_utc("20261018T104000Z\n"));
  puts("SHA-256 vectors, NV bounds, full reads, "
       "mismatch/removal/cancel/timeout and canonical AUX cleanup passed");
  return 0;
}
