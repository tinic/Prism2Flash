/* SPDX-License-Identifier: GPL-2.0-only */
#include "backup.h"
#include "file.h"
#include "p2_stdio.h"
#include "preflight.h"
#include "sha256.h"
#include <charconv>
#include <cstring>
#include <dos/dos.h>
#include <proto/dos.h>

// Real DOS filesystem/lock test, without a PCMCIA backend. The uniquely-created
// directory and test file are removed only after successful close/reopen/hash
// checks. Failure preserves the owned directory for inspection.
int main(int argc, char **argv) {
  if (argc != 4)
    return 20;
  int east = 10000;
  const auto end = argv[3] + std::strlen(argv[3]);
  const auto parsed = std::from_chars(argv[3], end, east);
  if (parsed.ec != std::errc{} || parsed.ptr != end)
    return 20;
  auto session = p2::preflight::NativeSession::create(argv[1], argv[2], east);
  if (!session) {
    p2_printf("Native session rejected: %d\n", session.error());
    return 20;
  }
  constexpr char content[] = "Prism2 native journal close/reopen test\n";
  auto stream = p2::File::open("session-test.bin", "w+b");
  p2::Sha256 hash;
  hash.update(std::string_view{content, sizeof(content) - 1});
  auto digest = hash.hex();
  if (!stream ||
      fwrite(content, 1, sizeof(content) - 1, stream.get()) !=
          sizeof(content) - 1 ||
      fflush(stream.get()) || !stream.close() ||
      p2::backup::verify_file("session-test.bin", digest.data(),
                              sizeof(content) - 1))
    return 20;
  p2_bus clock{};
  clock.now_us = [](void *) -> uint32_t { return 0; };
  for (const auto point : {p2::preflight::Checkpoint::BeforeStart,
                           p2::preflight::Checkpoint::BeforeEnd,
                           p2::preflight::Checkpoint::AfterStart,
                           p2::preflight::Checkpoint::AfterEnd}) {
    auto stamp = (*session)->stamp(point, clock);
    if (!stamp)
      return 20;
    p2_printf("Native phase %u UTC: %s\n", unsigned(point), stamp->data());
  }
  if (!DeleteFile((CONST_STRPTR) "session-test.bin"))
    return 20;
  // The live destination lock/current directory must prevent its deletion.
  if (DeleteFile((CONST_STRPTR)argv[1]))
    return 20;
  session->reset();
  if (!DeleteFile((CONST_STRPTR)argv[1]))
    return 20;
  puts("Native storage: create, relative stream, flush/close/reopen/hash, "
       "four UTC phases, retained lock and restored directory passed; "
       "no card access.");
  return 0;
}
