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
  std::array<char, 17> generated_stamp{};
  std::array<char, 384> generated_directory{};
  const char *directory = nullptr, *timestamp = nullptr;
  int east = 10000;
  if (argc == 3 && !std::strcmp(argv[1], "--local")) {
    struct DateStamp now {};
    DateStamp(&now);
    east = p2::preflight::LocalTime;
    generated_stamp = p2::preflight::clock_label(
        {now.ds_Days, now.ds_Minute, now.ds_Tick}, east);
    timestamp = generated_stamp.data();
    const auto length = std::strlen(argv[2]);
    const auto count = p2_snprintf(
        generated_directory.data(), generated_directory.size(), "%s%sP2-%s",
        argv[2], length && argv[2][length - 1] == ':' ? "" : "/", timestamp);
    if (count <= 0 || size_t(count) >= generated_directory.size())
      return 20;
    directory = generated_directory.data();
  } else if (argc == 4) {
    directory = argv[1];
    timestamp = argv[2];
    const auto end = argv[3] + std::strlen(argv[3]);
    const auto parsed = std::from_chars(argv[3], end, east);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
      return 20;
  } else
    return 20;
  auto session =
      p2::preflight::NativeSession::create(directory, timestamp, east);
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
  for (unsigned point = 0; point < 4; ++point) {
    const auto stamp = (*session)->label();
    p2_printf("Native phase %u label: %s\n", point, stamp.data());
  }
  if (!DeleteFile((CONST_STRPTR) "session-test.bin"))
    return 20;
  // The live destination lock/current directory must prevent its deletion.
  if (DeleteFile((CONST_STRPTR)directory))
    return 20;
  session->reset();
  if (!DeleteFile((CONST_STRPTR)directory))
    return 20;
  puts("Native storage: create, relative stream, flush/close/reopen/hash, "
       "four timestamp phases, retained lock and restored directory passed; "
       "no card access.");
  return 0;
}
