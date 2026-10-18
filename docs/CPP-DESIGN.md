# C++ design

Project-owned code uses C++23, LLVM formatting, and the pinned AmigaPorts
GCC v16.2-rc17 compiler. Exceptions and RTTI are disabled.

## Ownership and errors

- `firmware::Image` is a move-only owning value. Only the parser can construct
  it; callers see immutable spans of segments, identities, ranges and plugs.
  A segment owns a `ByteBuffer`; copying cannot accidentally duplicate ownership.
- `firmware::Parser` keeps its state private and returns
  `std::expected<Image, Error>`. A failed parse destroys every acquired buffer.
  File and memory input use typed cursors. Parser state is allocated on the heap
  because its 4096-byte metadata buffer exceeds the usual Amiga Shell stack.
- `BoundedVector<T, Limit>` provides bounded, move-aware storage with explicit
  allocation failure. It uses the checked C heap underneath typed ownership, with
  construct_at/destroy_n and normal move semantics. This avoids the throwing
  libstdc++ new/exception startup on Kickstart 2.x. This small container avoids using throwing STL allocation with
  exceptions disabled. Failed growth preserves the old elements. Metadata and
  payload limits are independent of current capacity.
- `PdaView` can only be created after PDA structure/CRC validation. Its record
  views borrow the original bytes. `CardSnapshot` is a value with optional MAC
  and supplier records; a missing record is not represented by a separate mask.
- `PreparedImage` is a move-only value with immutable segment views. Its factory
  validates both sources before cloning, plugging, stamping and merging them.
  Failures destroy private copies; source images and PDA bytes remain unchanged.
  `ImageSource` explicitly supplies the actual basename separately from embedded
  metadata. Exact raw source hashes are accumulated during parsing.
- `File` owns its stream. `Slot` owns the backend/card claim. Their destructors
  cover early returns. Writes still use explicit checked close, and capture
  explicitly checks AUX and backend restoration before releasing ownership.

For example, the parser API is:

```cpp
auto file = p2::File::open(path, "rb");
if (!file)
  return 20;
auto image = p2::firmware::Image::read(*file);
if (!image)
  return report(image.error());
for (const auto &segment : image->segments())
  inspect(segment.address, segment.data.bytes());
```

There is no public raw owning image pointer, segment count or `free_image` call.
Views remain valid only while their owning buffer exists; moving an image
transfers ownership without relocating its payload bytes.

## Card and ABI boundaries

`Card` implements bounded protocol operations directly as methods. Transfers
accept spans; identity and PDA-location reads return typed results. A `Card`
borrows its register bus, and the bus borrows the owning `Slot`. Keep the slot
alive until all operations and cleanup finish. The register callback table and
Amiga SDK calls remain at the backend boundary, where C types/assembly stubs are
required. The backend object owns acquired handles and destroys them in the
established native release order.

Genesis probing and NV acquisition return reports even on failure: partial
read counts, register observations and cleanup errors must survive. `NvCapture`
encapsulates its buffers, deadline and acquisition state. Cancellation is masked
only during cleanup; removal remains sticky. Snapshot register values are never
replayed. Existing AUX is left enabled; only AUX opened by capture is disabled.

`Sha256` initializes itself, accepts byte spans/string views, and returns a digest
value. Taking a digest preserves the accumulated state.

These changes preserve CLI syntax, exit codes, capture manifests and the native
backup gate. They enable no hardware operations or flash programming.
