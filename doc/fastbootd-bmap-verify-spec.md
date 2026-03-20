# Fastbootd Block Map Verification

**Status**: Proposal (rev 2 — binary wire format per firmware team feedback)
**Date**: 2026-03-18
**Author**: Raspberry Pi Imager team
**Audience**: fastbootd firmware team

## Problem

When Imager flashes an OS image via fastboot with a block map (bmap), unmapped
regions are sent as DONT_CARE and never written to the device.  The existing
whole-disk SHA-256 verification approach (`extract_sha256` over the full raw
image) cannot work because the unmapped regions retain whatever was previously
on the device — the hash will never match.

We need a way to verify that the **mapped regions** were written correctly,
using the per-range checksums that the bmap file already provides.

## Background: bmap file format

A `.bmap` file is a small XML document (typically 2-10 KB) produced by
`bmaptool create`.  It lists which block ranges of a disk image contain
allocated data, along with per-range SHA-256 checksums.

```xml
<?xml version="1.0" ?>
<bmap version="2.0">
    <ImageSize>4068474880</ImageSize>
    <BlockSize>4096</BlockSize>
    <BlocksCount>993280</BlocksCount>
    <MappedBlocksCount>363520</MappedBlocksCount>
    <ChecksumType>sha256</ChecksumType>
    <BlockMap>
        <Range chksum="abc123...">0-8191</Range>
        <Range chksum="def456...">262144-524287</Range>
        <!-- ... typically 20-200 ranges for a full OS image -->
    </BlockMap>
</bmap>
```

- Ranges are **inclusive** (`0-8191` = blocks 0 through 8191)
- Block size is always 4096 bytes
- Checksums cover the raw bytes of each range (contiguous on-disk data)
- Typical OS images have 30-40% of blocks mapped

## Protocol

```
fastboot download <bmap_binary>          # compact binary, serialized by Imager
fastboot oem bmap-load mmcblk0           # memcpy from download buffer, associate with device
fastboot oem bmap-verify mmcblk0         # read-and-hash each range, send INFO progress
```

Imager parses the XML bmap and serializes it to a compact binary format
before sending.  The firmware never sees XML — it receives a flat struct
array that can be consumed with `memcpy` + SHA-256.  No parser needed on
the device side.

### Binary wire format

All integers are **little-endian**, all structs are **packed** (no padding).
The host (x86/ARM64 macOS/Windows/Linux) and device (ARM) may have different
natural alignment — packed layout with explicit endianness ensures the wire
format is identical regardless of architecture.  Both sides must use
`__attribute__((packed))` / `#pragma pack(push, 1)` and convert via
`htole32`/`le32toh` (or equivalent) when reading/writing integer fields.

The payload sent via `fastboot download` is:

```c
#pragma pack(push, 1)

struct BmapHeader {            // offset  size
    uint32_t magic;            //   0      4    0x424D4150 ("BMAP"), little-endian
    uint32_t block_size;       //   4      4    always 4096
    uint32_t range_count;      //   8      4    number of BmapRange entries following
    uint32_t reserved;         //  12      4    must be 0
};                             //         16 bytes total
static_assert(sizeof(BmapHeader) == 16);

struct BmapRange {             // offset  size
    uint32_t start_block;      //   0      4    first block (inclusive)
    uint32_t end_block;        //   4      4    last block (inclusive)
    uint8_t  sha256[32];       //   8     32    SHA-256 digest of [start*blk_sz, (end+1)*blk_sz)
};                             //         40 bytes total
static_assert(sizeof(BmapRange) == 40);

#pragma pack(pop)
```

Total payload size: `16 + 40 * range_count` bytes.  For a typical OS image
with ~150 ranges, that's **6.0 KB**.

The firmware validates `magic` (checking endianness implicitly — a
big-endian misparse would see `0x50414D42`), validates `range_count`
against the download size, and reads the flat array of `range_count`
entries starting at offset 16.

Note: block indices are `uint32_t`.  At 4096-byte blocks this addresses
up to 16 TB, which is sufficient for any current device.

### `oem bmap-load <partition>`

Associates the download buffer (already in memory from the preceding
`fastboot download`) with the named block device.  The firmware:

1. Validates `BmapHeader.magic == 0x424D4150`
2. Checks `version == 1`
3. Stores the pointer to the range array and `range_count`

Responds `OKAY` on success, `FAILbad bmap header` on validation failure.

The download buffer is retained until the next `download` command overwrites
it, or until `bmap-verify` completes.

### `oem bmap-verify <partition>`

Iterates over the stored range list and for each range:

1. Reads `(end - begin + 1) * block_size` bytes from the block device
   starting at `begin * block_size`
2. Computes SHA-256 over the read data
3. Compares against `BmapRange.checksum`

**Responses**:

```
INFO verifying range 0/147 (blocks 0-8191)
INFO verifying range 1/147 (blocks 262144-524287)
...
OKAY
```

On first mismatch:
```
FAIL:<range_index>:<expected_hex>:<actual_hex>
```

Progress `INFO` lines are emitted at least every 2 seconds so Imager can
drive a progress bar.

## Imager-side flow

```
1. Download bmap XML from manifest URL (small, ~5 KB)
2. Parse bmap XML → BlockMap (already implemented in Imager)
3. Flash image using bmap-aware sparse encoder (DONT_CARE for unmapped blocks)
4. Serialize BlockMap → binary wire format (BmapHeader + BmapRange[])
5. fastboot download <binary>
6. fastboot oem bmap-load mmcblk0
7. fastboot oem bmap-verify mmcblk0
8. Report result to user
```

Steps 5-7 replace the current whole-disk verify pass.  Imager already has
the XML parser and the parsed `BlockMap`; serializing to the binary format
is a trivial loop over the range list.

## Design considerations

### Why binary, not XML on the wire?

- The firmware already has SHA-256 (required for fastboot).  Adding an XML
  parser is a significant dependency for embedded firmware.
- The binary format is a single `memcpy`-friendly struct.  No allocation,
  no string parsing, no error recovery.
- Imager already has the XML parser.  Serializing to binary is ~20 lines.

### Why not read-back verification?

Reading back mapped ranges over USB and hashing locally would work but is
slower: the data traverses USB twice (write + read-back) and the host must
buffer or stream the read data.  On-device verification only reads from
local storage (eMMC/SD), which is faster, and avoids USB bandwidth
contention.

### Memory requirements

The download buffer is already allocated for the flash path (256 MB
typical).  The bmap binary is < 10 KB for any realistic image, reusing the
same buffer.  The firmware additionally needs one SHA-256 context (~200
bytes).  No extra allocation required.

### Timeout

Verification of a 4 GB image with 30-40% mapped blocks reads ~1.2-1.6 GB
from storage.  At typical eMMC read speeds (100-200 MB/s), this takes
6-16 seconds.  Imager will use a 120-second timeout for the `bmap-verify`
command to account for slower media.

### Fallback

If the device doesn't support `oem bmap-verify` (older firmware), Imager
skips verification and logs a warning.  The flash itself is still correct —
verification is a safety check, not a functional requirement.

### Checksum algorithm

SHA-256 only.  The firmware uses OpenSSL EVP for hashing (already a
dependency for fastboot).  The `reserved` field in the header is available
for future extension (e.g., a checksum type selector) if needed.

## Non-goals

- **Unmapped region verification**: By definition, unmapped blocks are
  unallocated filesystem free space.  Their content is irrelevant.
- **Partial verification**: All mapped ranges are verified.  There is no
  "spot check" mode.
- **Bmap generation on device**: The bmap is always generated at image
  build time and provided by the OS manifest.
- **XML parsing on device**: The client serializes to binary.  The firmware
  never handles XML.
