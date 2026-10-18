/* SPDX-License-Identifier: GPL-2.0-only
 * Strict, bounded byte-oriented reader. Metadata semantics adapted from
 * Jouni Malinen's GPL-2.0 Host AP utilities 0.4.7 prism2_srec.c.
 */
#include "firmware.h"
#include <stdlib.h>
#include <string.h>

#define INFO_BASE UINT32_C(0xff200000)
#define INFO_LIMIT 4096u
struct reader {
  int (*next)(void *);
  void *ctx;
  unsigned long bytes, line;
};
struct state {
  struct p2_fw_image *image;
  size_t capacity;
  unsigned component, entry, header, count_record, name;
  size_t info_used, info_expected;
  uint8_t info[INFO_LIMIT];
  unsigned long line;
  struct p2_fw_error *error;
};
static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static int failure(struct state *s, int code, const char *message) {
  if (s->error) {
    s->error->code = code;
    s->error->line = s->line;
    strncpy(s->error->message, message, sizeof(s->error->message) - 1);
    s->error->message[sizeof(s->error->message) - 1] = 0;
  }
  return code;
}
void p2_fw_free(struct p2_fw_image *image) {
  size_t i;
  if (!image)
    return;
  for (i = 0; i < image->segment_count; ++i)
    free(image->segments[i].data);
  free(image->segments);
  free(image);
}
static struct p2_identity identity(const uint8_t *p) {
  struct p2_identity id = {le16(p), le16(p + 2), le16(p + 4), le16(p + 6)};
  return id;
}
static int info_record(struct state *s) {
  struct p2_fw_image *im = s->image;
  const uint8_t *p = s->info + 4;
  size_t length = s->info_used - 4, i;
  unsigned type = le16(s->info + 2);
  switch (type) {
  case 1:
    if (length != 8 || s->component)
      return failure(s, P2_FW_FORMAT,
                     "Invalid or duplicate component identity");
    im->component = identity(p);
    s->component = 1;
    break;
  case 2:
    if (length < 10 || (length - 4) % 6 || le16(p) > 1 || !le16(p + 2) ||
        le16(p + 2) > 4)
      return failure(s, P2_FW_FORMAT, "Invalid compatibility record");
    for (i = 4; i < length; i += 6) {
      struct p2_fw_range *r;
      if (im->range_count == P2_FW_MAX_ITEMS)
        return failure(s, P2_FW_LIMIT, "Too many interface ranges");
      r = &im->ranges[im->range_count++];
      *r = p2_fw_range{le16(p), le16(p + 2), le16(p + i), le16(p + i + 2),
                       le16(p + i + 4)};
      if (r->bottom > r->top)
        return failure(s, P2_FW_FORMAT, "Reversed compatibility interval");
    }
    break;
  case 3:
    if (length != 2 || im->has_sequence)
      return failure(s, P2_FW_FORMAT, "Invalid or duplicate build sequence");
    im->build_sequence = le16(p);
    im->has_sequence = 1;
    break;
  case 4:
    if (length != 8)
      return failure(s, P2_FW_FORMAT, "Invalid platform identity");
    if (im->platform_count == P2_FW_MAX_ITEMS)
      return failure(s, P2_FW_LIMIT, "Too many platform identities");
    im->platforms[im->platform_count++] = identity(p);
    break;
  case 0x8001:
    if (s->name++ || !length || length >= sizeof(im->name))
      return failure(s, P2_FW_FORMAT, "Invalid or duplicate firmware name");
    for (i = 0; i < length; ++i) {
      if (p[i] && (p[i] < 32 || p[i] > 126))
        return failure(s, P2_FW_FORMAT, "Non-ASCII firmware name");
      im->name[i] = (char)p[i];
    }
    break;
  default:
    return failure(s, P2_FW_FORMAT, "Unknown firmware information type");
  }
  s->info_used = s->info_expected = 0;
  return P2_FW_OK;
}
static int info_fragment(struct state *s, uint32_t address, const uint8_t *p,
                         size_t length) {
  size_t expected;
  if (address != INFO_BASE + s->info_used || !length)
    return failure(s, P2_FW_FORMAT,
                   "Discontinuous firmware information fragments");
  if (!s->info_used) {
    if (length < 4 || !le16(p))
      return failure(s, P2_FW_FORMAT, "Truncated firmware information header");
    expected = (size_t)le16(p) * 2 + 2;
    /* Names count their payload words; other types count type+payload. */
    if (le16(p + 2) == 0x8001)
      expected += 2;
    if (expected < 4 || expected > INFO_LIMIT)
      return failure(s, P2_FW_LIMIT, "Firmware information exceeds limit");
    s->info_expected = expected;
  }
  if (length > s->info_expected - s->info_used)
    return failure(s, P2_FW_FORMAT, "Firmware information length mismatch");
  memcpy(s->info + s->info_used, p, length);
  s->info_used += length;
  return s->info_used == s->info_expected ? info_record(s) : P2_FW_OK;
}
static int payload(struct state *s, uint32_t address, const uint8_t *p,
                   size_t length) {
  struct p2_fw_image *im = s->image;
  struct p2_fw_segment *record;
  if (!length || address >= UINT32_C(0x01000000) ||
      length > UINT32_C(0x01000000) - address)
    return failure(s, P2_FW_FORMAT,
                   "Empty or overflowing payload address range");
  if (length > P2_FW_MAX_BYTES - im->payload_bytes ||
      im->segment_count == P2_FW_MAX_SEGMENTS)
    return failure(s, P2_FW_LIMIT, "Payload or segment limit exceeded");
  if (im->segment_count == s->capacity) {
    size_t cap = s->capacity ? s->capacity * 2 : 32;
    void *next = realloc(im->segments, cap * sizeof(*im->segments));
    if (!next)
      return failure(s, P2_FW_MEMORY, "Out of memory for segments");
    im->segments = static_cast<p2_fw_segment *>(next);
    s->capacity = cap;
  }
  record = &im->segments[im->segment_count];
  record->data = static_cast<uint8_t *>(malloc(length));
  if (!record->data)
    return failure(s, P2_FW_MEMORY, "Out of memory for payload");
  record->address = address;
  record->length = length;
  memcpy(record->data, p, length);
  ++im->segment_count;
  im->payload_bytes += length;
  return P2_FW_OK;
}
static int s3(struct state *s, uint32_t address, const uint8_t *p,
              size_t length) {
  struct p2_fw_image *im = s->image;
  if (s->info_used && (address & UINT32_C(0xffff0000)) != INFO_BASE)
    return failure(s, P2_FW_FORMAT, "Unfinished firmware information record");
  if (address == UINT32_C(0xff000000)) {
    struct p2_fw_plug *r;
    if (length != 12)
      return failure(s, P2_FW_FORMAT, "Invalid PDA plug record length");
    if (im->plug_count == P2_FW_MAX_ITEMS)
      return failure(s, P2_FW_LIMIT, "Too many plug records");
    r = &im->plugs[im->plug_count++];
    *r = p2_fw_plug{le32(p), le32(p + 4), le32(p + 8)};
    if ((!r->pdr || r->pdr > 0xffffu) && r->pdr != UINT32_MAX)
      return failure(s, P2_FW_FORMAT, "Invalid plug PDR identifier");
    if (!r->length || r->address >= UINT32_C(0x01000000) ||
        r->length > UINT32_C(0x01000000) - r->address)
      return failure(s, P2_FW_FORMAT, "Invalid plug address range");
    return P2_FW_OK;
  }
  if (address == UINT32_C(0xff100000)) {
    struct p2_fw_crc *r;
    if (length != 12)
      return failure(s, P2_FW_FORMAT, "Invalid signature descriptor length");
    if (im->crc_count == P2_FW_MAX_CRCS)
      return failure(s, P2_FW_LIMIT, "Too many signature descriptors");
    r = &im->crcs[im->crc_count++];
    *r = p2_fw_crc{le32(p), le32(p + 4), le32(p + 8)};
    if (r->program > 1 || !r->length || r->start >= UINT32_C(0x01000000) ||
        r->length > UINT32_C(0x01000000) - r->start ||
        (r->program && r->start < 2))
      return failure(s, P2_FW_FORMAT,
                     "Invalid signature descriptor range or flag");
    return P2_FW_OK;
  }
  if ((address & UINT32_C(0xffff0000)) == INFO_BASE)
    return info_fragment(s, address, p, length);
  if (address & UINT32_C(0xff000000))
    return failure(s, P2_FW_FORMAT, "Unknown special S3 address");
  return payload(s, address, p, length);
}
static int hex(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
static int record(struct state *s, const uint8_t *line, size_t length) {
  uint8_t bytes[256];
  size_t n, i, address_length;
  uint32_t address = 0;
  unsigned sum = 0, type;
  if (length < 4 || line[0] != 'S' || (length & 1))
    return failure(s, P2_FW_FORMAT, "Malformed S-record line");
  n = (length - 2) / 2;
  if (n > sizeof(bytes))
    return failure(s, P2_FW_LIMIT, "S-record line too long");
  for (i = 0; i < n; ++i) {
    int hi = hex(line[2 + i * 2]), lo = hex(line[3 + i * 2]);
    if (hi < 0 || lo < 0)
      return failure(s, P2_FW_FORMAT, "Non-hexadecimal S-record byte");
    bytes[i] = (uint8_t)((hi << 4) | lo);
    sum += bytes[i];
  }
  if ((size_t)bytes[0] + 1 != n || (sum & 255) != 255)
    return failure(s, P2_FW_FORMAT, "S-record length or checksum mismatch");
  type = line[1];
  switch (type) {
  case '0':
  case '5':
    address_length = 2;
    break;
  case '6':
    address_length = 3;
    break;
  case '3':
  case '7':
    address_length = 4;
    break;
  default:
    return failure(s, P2_FW_FORMAT, "Unsupported S-record type");
  }
  if (n < address_length + 2)
    return failure(s, P2_FW_FORMAT, "S-record is shorter than its address");
  for (i = 0; i < address_length; ++i)
    address = (address << 8) | bytes[1 + i];
  if (type == '0') {
    if (s->header || s->image->records || s->entry || address)
      return failure(s, P2_FW_FORMAT, "Misplaced S0 header");
    s->header = 1;
    return P2_FW_OK;
  }
  if (type == '3') {
    if (s->count_record)
      return failure(s, P2_FW_FORMAT, "Data after record count");
    ++s->image->records;
    return s3(s, address, bytes + 5, n - 6);
  }
  if (s->info_used)
    return failure(s, P2_FW_FORMAT, "Unfinished firmware information record");
  if (n != address_length + 2)
    return failure(s, P2_FW_FORMAT, "Unexpected control record payload");
  /* Intersil RAM images place S7 before payload S3 records. It supplies
   * the unique entry point, not an end-of-file marker for this dialect. */
  if (type == '7') {
    if (s->entry)
      return failure(s, P2_FW_FORMAT, "Duplicate S7 entry record");
    s->image->entry = address;
    s->entry = 1;
    return P2_FW_OK;
  }
  if (s->count_record || address != s->image->records)
    return failure(s, P2_FW_FORMAT, "Invalid or duplicate record count");
  s->count_record = 1;
  return P2_FW_OK;
}
static int order(const void *a, const void *b) {
  const auto *x = static_cast<const p2_fw_segment *>(a);
  const auto *y = static_cast<const p2_fw_segment *>(b);
  return (x->address > y->address) - (x->address < y->address);
}
static int combine(struct state *s) {
  struct p2_fw_image *im = s->image;
  size_t i = 0, used = 0;
  qsort(im->segments, im->segment_count, sizeof(*im->segments), order);
  /* Validate before mutation so a failure can always free every input segment.
   */
  for (i = 1; i < im->segment_count; ++i)
    if (im->segments[i].address <
        im->segments[i - 1].address + im->segments[i - 1].length)
      return failure(s, P2_FW_FORMAT, "Overlapping payload records");
  i = 0;
  while (i < im->segment_count) {
    size_t j = i + 1, total = im->segments[i].length, k, at = 0;
    uint8_t *data;
    while (j < im->segment_count &&
           im->segments[j].address == im->segments[i].address + total)
      total += im->segments[j++].length;
    if (j == i + 1) {
      im->segments[used++] = im->segments[i++];
      continue;
    }
    data = static_cast<uint8_t *>(malloc(total));
    if (!data) {
      /* Compacted prefix and untouched suffix still own disjoint buffers. */
      for (k = i; k < im->segment_count; ++k)
        free(im->segments[k].data);
      im->segment_count = used;
      return failure(s, P2_FW_MEMORY, "Out of memory combining segments");
    }
    for (k = i; k < j; ++k) {
      memcpy(data + at, im->segments[k].data, im->segments[k].length);
      at += im->segments[k].length;
      free(im->segments[k].data);
    }
    im->segments[used++] = p2_fw_segment{im->segments[i].address, total, data};
    i = j;
  }
  im->segment_count = used;
  return P2_FW_OK;
}
static int parse(struct reader *reader, struct p2_fw_image **out,
                 struct p2_fw_error *error) {
  struct state *s;
  uint8_t line[515];
  int rc = P2_FW_OK, c;
  size_t length = 0;
  if (out)
    *out = nullptr;
  if (error)
    memset(error, 0, sizeof(*error));
  if (!out) {
    if (error) {
      error->code = P2_FW_FORMAT;
      strcpy(error->message, "Missing output pointer");
    }
    return P2_FW_FORMAT;
  }
  s = static_cast<state *>(calloc(1, sizeof(*s)));
  if (!s) {
    if (error) {
      error->code = P2_FW_MEMORY;
      strcpy(error->message, "Out of memory for parser");
    }
    return P2_FW_MEMORY;
  }
  s->error = error;
  s->image = static_cast<p2_fw_image *>(calloc(1, sizeof(*s->image)));
  if (!s->image) {
    rc = failure(s, P2_FW_MEMORY, "Out of memory for firmware");
    goto done;
  }
  while ((c = reader->next(reader->ctx)) != EOF) {
    if (++reader->bytes > P2_FW_MAX_INPUT) {
      rc = failure(s, P2_FW_LIMIT, "Input byte limit exceeded");
      break;
    }
    if (c == '\n') {
      s->line = ++reader->line;
      if (s->line > P2_FW_MAX_LINES) {
        rc = failure(s, P2_FW_LIMIT, "Input line limit exceeded");
        break;
      }
      if (length && line[length - 1] == '\r')
        --length;
      if (length && (rc = record(s, line, length)) != P2_FW_OK)
        break;
      length = 0;
    } else {
      if (length == sizeof(line)) {
        rc = failure(s, P2_FW_LIMIT, "S-record line too long");
        break;
      }
      line[length++] = (uint8_t)c;
    }
  }
  if (!rc && length) {
    s->line = reader->line + 1;
    if (length && line[length - 1] == '\r')
      --length;
    if (length)
      rc = record(s, line, length);
  }
  if (!rc &&
      (!s->entry || !s->component || !s->image->segment_count || s->info_used))
    rc = failure(s, P2_FW_FORMAT,
                 "Missing S7, component, payload or complete metadata");
  if (!rc)
    rc = combine(s);
  if (!rc) {
    *out = s->image;
    s->image = nullptr;
  }
done:
  p2_fw_free(s->image);
  free(s);
  return rc;
}
struct memory {
  const uint8_t *data;
  size_t length, position;
};
static int memory_next(void *ctx) {
  auto *m = static_cast<memory *>(ctx);
  return m->position == m->length ? EOF : m->data[m->position++];
}
int p2_fw_parse(const uint8_t *data, size_t length, struct p2_fw_image **out,
                struct p2_fw_error *error) {
  struct memory m = {data, length, 0};
  struct reader r = {memory_next, &m, 0, 0};
  if (!data && length) {
    if (out)
      *out = nullptr;
    if (error) {
      memset(error, 0, sizeof(*error));
      error->code = P2_FW_FORMAT;
      strcpy(error->message, "Missing input bytes");
    }
    return P2_FW_FORMAT;
  }
  return parse(&r, out, error);
}
static int file_next(void *ctx) { return fgetc(static_cast<FILE *>(ctx)); }
int p2_fw_read(FILE *file, struct p2_fw_image **out,
               struct p2_fw_error *error) {
  struct reader r = {file_next, file, 0, 0};
  int rc;
  if (!file) {
    if (out)
      *out = nullptr;
    if (error) {
      memset(error, 0, sizeof(*error));
      error->code = P2_FW_IO;
      strcpy(error->message, "Missing input stream");
    }
    return P2_FW_IO;
  }
  rc = parse(&r, out, error);
  if (ferror(file)) {
    if (out) {
      p2_fw_free(*out);
      *out = nullptr;
    }
    if (error) {
      memset(error, 0, sizeof(*error));
      error->code = P2_FW_IO;
      strcpy(error->message, "Input read error");
    }
    return P2_FW_IO;
  }
  return rc;
}
