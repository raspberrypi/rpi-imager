# Fuzz harnesses

libFuzzer targets for the parsers that read input nobody vetted: bytes off a
card, out of a downloaded image, or from a USB device that claims to be a
Compute Module. Off by default, because libFuzzer needs Clang and the tree
builds with GCC.

## Building them

    cmake -S src -B build-fuzz -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DRPI_IMAGER_FUZZERS=ON \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DQt6_DIR=/path/to/qt/gcc_arm64/lib/cmake/Qt6 -DXZ_SANDBOX=no
    cmake --build build-fuzz --target fuzzers

`RPI_IMAGER_FUZZERS` is deliberately outside `BUILD_TESTING`: these need Qt
Core and the parser sources, not Catch2 and the whole test tree. It also puts
the build into the same category as a sanitiser build, which turns LTO off --
without that the vendored archives are bitcode the harnesses cannot link
against, and `-DXZ_SANDBOX=no` is needed because xz will not build under a
sanitiser with Landlock on.

Linux only. The FAT and partition-table harnesses keep their image in a memfd
and link the Linux file-operations backend, and both want a template
filesystem, found through `FUZZ_FAT_TEMPLATE`. `make_seeds.py` builds it --
nothing else did, and losing a scratch directory was enough to take both
harnesses out of service until it was noticed.

## Seeds and the template

Four harnesses cannot get started on their own, and two more cannot start at
all without a filesystem to read. `make_seeds.py` builds the lot:

    python3 src/test/fuzz/make_seeds.py /path/to/corpus-root

It prints the `FUZZ_FAT_TEMPLATE=` line to export. Set that variable first if
the template should live somewhere else, and it will be built there instead --
or left alone if it already exists.

The template is FAT32, thirty-four megabytes, holding a `config.txt`, a
`cmdline.txt`, a name long enough to need an LFN chain and a file in
`overlays/` -- the four things `fuzz_fatdir` reads back by hand. Thirty-four
is the smallest a FAT32 can be: 512-byte sectors at one sector per cluster
give about 69,000 of them, and below 65,525 `mkfs.vfat` writes a FAT16
instead. The size matters beyond fidelity, because every iteration lays the
template down again: 100 executions a second at 34 MiB against 55 at 64 MiB,
for the same coverage. It needs `dosfstools` and `mtools`.

`fuzz_parttable` needs a table pointing at the filesystem 64 KiB into the
image -- the mutator will not produce that start LBA by itself, and without it
the FAT parser behind the table is never reached. `fuzz_fastboot` needs replies
framed the way its transport reads them. `fuzz_sparse_roundtrip` needs large
inputs, because libFuzzer takes its input limit from the largest file in the
corpus and the boundary behaviour only appears once an image spans segments.
`fuzz_bmap` needs a document that parses: a .bmap is XML, and a mutator will
not invent well-formed XML carrying the four elements the parser insists on.
`fuzz_drivelist` needs both shapes of lsblk answer. The booleans arrive as
JSON booleans on some versions and as the strings `"0"` and `"1"` on others,
and the parser handles both -- so a corpus with only one shape covers half
the branches, and a mutator will not turn one into the other because it would
have to invent matching quotes. Five seeded answers, including a system disk
beside a loop device and a card, reach 704 edges.

`fuzz_sparse_bmap` needs images at all. libFuzzer takes its length limit from
the largest file in the corpus, and from empty that is 4096 bytes -- one
block, which is less than the ten header bytes plus a block the target reads
before it looks at anything. Unseeded it reached **2 edges over 39 million
executions**, having refused every input on the size check: a harness that
asked nothing and found nothing, exactly as measured. Six images, between six
and twelve blocks, took it to 565.

`fuzz_bootfiles` needs a TAR: a mutator will not produce a 512-byte header
with a correct octal checksum in it, and over two minutes from cold it did
not, so the archive never opened and everything past it -- `find()`, the
chip-prefixed lookup, the size an entry declares -- went unvisited. A bundle
holding the four names the firmware manager asks for took it from 207 edges
to 369. `fuzz_der` needs a SubjectPublicKeyInfo, which is four nested
structures deep: 98 edges against 149 for the same thirty seconds. Neither
seed is a real key or a real bundle, because neither parser does arithmetic
on the contents -- shape and lengths are all they read.

`fuzz_imagesize` is the one case here that manages without -- gzip and xz
magic numbers are short enough to stumble on -- but it is given real
containers anyway, for 121 edges against 106 over the same thirty seconds.

Generated rather than committed: the sparse images are a megabyte between them.
The effect is not marginal -- from a cold corpus holding nothing but its seed,
`fuzz_parttable` reaches 805 edges in a minute, against 632 for the version
without a filesystem behind the table after twenty-three. `fuzz_bmap` is
starker still: 25 edges unseeded against 279 seeded, over forty seconds each,
because unseeded it never once gets past `parse()` to the serialising and
lookup paths the harness is actually for.

Run with `ASAN_OPTIONS=detect_container_overflow=0`: Qt is not built with
ASan, and mixing instrumented and uninstrumented containers produces false
reports that have nothing to do with the code under test.

## Running them

    src/test/fuzz/run_fuzzers.sh -t 300

Every harness in the build directory, five minutes each, against a corpus
under `<build>/fuzz-corpus` that is kept between runs. That is the point of
it: libFuzzer writes back everything that reached new code, so the next
session starts where this one stopped instead of re-deriving the first
minute. It builds the seeds and the FAT template if they are not there, sets
the three environment variables below, and leaves anything a target finds in
`fuzz-corpus/findings`, named for the harness that found it.

`-b` picks the build directory, `-c` the corpus root, and named targets run
only those. `-m` minimises each corpus first, which is worth doing
occasionally: a corpus grown over several sessions is mostly inputs that no
longer reach anything the others do not.

`-j` runs several targets at once. Each libFuzzer is one process on one core
and the runner used to start them one at a time, so twenty-two targets at
twenty minutes each occupied a four-core machine for seven hours and the
budget per target kept being cut to fit the night. Three targets took twelve
seconds against thirty serially. Output is buffered per target and printed in
target order at the end, so a parallel run reads exactly like a serial one.

Do not reach for the same trick on `seed_sweep.sh`. Fuzz targets own their
corpora and share nothing; parallel storms would share a font cache and an
XDG tree, and a phantom finding from contention costs more than the hours it
saves. A sweep that survived SIGTERM and ran alongside another for 78 minutes
is what made that concrete.

### The input length limit

libFuzzer takes its limit from the largest file in the corpus, and falls back
to 4096 only when the corpus is empty. Seeding a target with small files
therefore pins it below that default *for good*: `fuzz_asn1_length` had been
running on inputs of at most **seven bytes** and `fuzz_imagesize` on 184,
against harnesses that accept a megabyte. The tell was the targets sitting
near 4018 -- those started from nothing and got the default, while the seeded
ones never escaped their seeds.

The runner now passes `-max_len` explicitly: never below 4096, and never
below what a corpus already holds, so a target that has grown past it keeps
what it found.

The three inputs that actually found defects are seeds rather than corpus
entries, for the same reason: `make_seeds.py` is in git and the corpus is
not, so they are replayed by every run from cold whatever happens to the
build tree.

**The corpus is worth more than the build directory it sits in.** It is
gitignored, so `rm -rf build-fuzz` throws away every input the fuzzers have
ever kept, and the next session starts from the seeds again. What that costs
is measurable: `fuzz_bmap` went 109 inputs, then 647, then 950, then 2,257
over four sessions, and `fuzz_blconfig` was two million executions into an
accumulated corpus when it found the byte-order-mark defect -- a run from
cold does not get there. Point `-c` somewhere outside the build tree, or
move the directory aside before cleaning.

## Checks beyond the defaults

The targets build with `-fsanitize=unsigned-integer-overflow` as well.
Wrapping is defined behaviour, so it is not in UBSan's default set -- but it
is still a wrong answer where a length or a digit accumulation is meant to be
bounded, and it found two things here: a boot order too long for its field
accepted as a truncated one, and a FAT free-cluster count wrapping to four
billion when a card understated it.

It is a Clang check; GCC has no `-fsanitize=unsigned-integer-overflow`, so
the GCC UBSan build cannot carry it. That is another reason the fuzzers are
worth running rather than trusting the suite's own sanitiser pass.

Two deliberate wraps show up, in Qt's `qHash` and ZSTD's sentinels. Run with

    UBSAN_OPTIONS=suppressions=src/test/fuzz/ubsan.supp:halt_on_error=0

`local-bounds` and `float-divide-by-zero` were tried alongside and reported
nothing on any target; they are not carried.

## What the slow one needed

`fuzz_customisation_gen` covers the generators that write a script run as
root on first boot, which makes it the one here worth the most time -- and it
was managing eleven executions a second. The cause was not the generators: a
plaintext `wifiPassword` reaches PBKDF2-HMAC-SHA1, 4096 rounds of it, several
times per input.

That derivation is not what the target is for. A passphrase long enough to be
derived (8..63) comes out as hex either way, so the quoting downstream sees
hex whether the rounds ran or not; a value outside that range is passed
through raw, which is the case where the quoting has work to do, and it never
enters PBKDF2 at all. Handing the generator a PSK already derived skips the
rounds and loses none of the escaping.

Measured over the same 300 seconds from the same corpus: **4,477 executions
reaching 1,827 edges, against 21,752 reaching 1,835**. Further, not merely
faster. One input in sixteen still takes the plaintext path, because the
derivation and the length test around it are ours as well.

## What has been tried and does not help

**Raising `-max_len` beyond 4096 for the container parsers.** 65536 looked
obviously right for `fuzz_imagesize`, which reads archives. Measured over
forty-five seconds it grew the corpus, found no new edge, and dropped
execution from about 717 a second to 214. Bigger inputs are not free. A
target that needs them should have to show it.

`-use_value_profile=1`. Measured on four targets over ninety seconds each,
against the same seeds: pieeprom 306 edges either way, oslist 119 either way,
configtxt 282 either way. Throughput fell by 35% to 50% on three of them, and
the corpus grew several times over -- value profiling tracks progress through
comparisons, and here that produced far more features against the same code.

bmap looked like the exception at 279 against 321. Run again twice it came
back 323 against 280, the other way round, so that was one run's noise rather
than a gain. Nothing in this directory wants it.

A dictionary of JSON keys for the OS list is the other one: 408 edges warm
either way, 227 against 230 from cold.

Seeds for `fuzz_blconfig` are the third, and they are worth recording because
the reasoning that produced them was wrong in an instructive way. It reaches
100 edges over 1.4 million executions, which reads like a target stuck at an
early return: everything it checks is behind a line starting with the sixteen
bytes `IMAGER_REPO_URL=`, and no mutator assembles that from padding.

It does not need to. libFuzzer intercepts `memcmp`, and `startsWith` on a
QByteArray goes through it, so the comparison hands the literal straight to
the mutator. Twenty of the 81 corpus entries from a cold two-minute run carry
the key. Eight hand-written regions -- CRLF, a stale tail behind the padding,
a padded value, no trailing newline -- measured 100 edges against 100.

The general form: a low edge count is the size of the code, not the depth
reached into it, and a fixed literal that gates a parser is not a reason for
either a seed or a dictionary.

## What each one covers

| harness | input, and where it comes from |
| --- | --- |
| `fuzz_bmap` | .bmap XML shipped beside an image in a repository |
| `fuzz_oslist` | the OS list JSON, from a repository URL the user can set |
| `fuzz_fatdir` | FAT directory and cluster chains, off whatever card is inserted, read *and* written |
| `fuzz_parttable` | MBR and GPT, which choose where the FAT parser starts reading |
| `fuzz_pieeprom` | bootloader EEPROM images, which arrive as a firmware download |
| `fuzz_fastboot` | fastboot responses, chosen entirely by the attached device |
| `fuzz_rpiboot_uri` | the device URI, rebuilt from text between components |
| `fuzz_imagesize` | .xz, .gz and .zst headers of what the downloader fetched |
| `fuzz_configtxt` | config.txt merging -- a property, not a crash hunt |
| `fuzz_customisation` | the shell quoting behind firstrun.sh, which runs as root |
| `fuzz_customisation_gen` | the generators that quoting protects; slow, give it hours |
| `fuzz_sparse_roundtrip` | the sparse encoder, checked by decoding what it wrote |
| `fuzz_sparse_bmap` | the same encoder driven with a block map, which is how a real write runs |
| `fuzz_bootloader_image` | pieeprom.original.bin, the other EEPROM reader in this tree |
| `fuzz_bootfiles` | fastboot/bootfiles.bin, the TAR inside a firmware release |
| `fuzz_der` | a SubjectPublicKeyInfo blob, on its way to the boot ROM's format |
| `fuzz_fileserver` | the filename a booting device asks rpiboot to serve it |
| `fuzz_rowdiff` | the in-place list rebuild, whose caller indexes one list by the other |
| `fuzz_ringbuffer` | the buffer between download and disk -- an operation order, not a parse |
| `fuzz_blconfig` | the bootloader flash region, which can name the OS list repository |
| `fuzz_drivelist` | the lsblk JSON that decides which drives are offered as a target |
| `fuzz_asn1_length` | one DER length, with arbitrary bytes and an arbitrary offset |

## Defects these found

Eight, and it is worth being exact about which: everything below was found by
a harness in this directory. The FAT allocation, the pieeprom underflow, the
xz memory limit, the config.txt carriage return, and the FAT directory loop.

Three more came later, and how each arrived says something about when to run
a fuzzer and when to change one:

- **`fuzz_blconfig`, two defects in the same reader.** Neither needed a new
  harness or a new seed -- only a longer slot than the target had ever been
  given, which a corpus that survives between runs made worth giving. It had
  been dismissed as plateaued at 100 edges; it was not plateaued, and found
  the first at two million executions.
- **`fuzz_fastboot`, one defect, two minutes after the harness was extended.**
  `readDeviceFile()` was the one entry point not driven, and the only one
  whose *result* a caller then edits rather than merely checks. A device
  answering `DATA93550679` -- twenty-four bytes, nothing sent after it --
  had four gigabytes reserved for it before a byte arrived.

The second is the more useful lesson. Three of the defects found in one day
came from extending a harness sideways to a neighbouring entry point; none
came from more runtime on a harness left as it was.

The last of those is the argument for keeping the template in the tree rather
than in a scratch directory. `fuzz_fatdir` had been unable to start for want
of one, and found the loop within an hour of being given it back.

Three more in the same shape were found by *reading* for it after the fuzzers
had gone quiet -- `BootloaderImage::getFile`, `Bootfiles::extractFromArchive`
and the ASN.1 length in `secureboot.cpp`. None of those three was reachable by
any harness that existed at the time, which is the useful part: two of them
now have one (`fuzz_bootfiles`, `fuzz_der`), written because the gap was what
let the defect live, not because reading is a better technique.

A fourth was read for in the same way, and this one *was* inside a harness's
reach the whole time. `BlockMap::serialize()` narrowed 64-bit block numbers
into the 32-bit fields the device is handed, with a plain cast, so block
4294967296 arrived as block 0 -- that range's data written at the start of
the card and the blocks it names left alone, with nothing said. `fuzz_bmap`
had been calling `serialize()` and discarding the result since it was
written. The call was coverage, not a question, and a target that asks
nothing finds nothing however long it runs. It now reads back what it
serialised.


- `devicewrapperfatpartition`: a subdirectory entry naming a cluster already
  on the path made listAllFilesRecursive() walk the loop forever, allocating
  a path and a list entry at every level. Eighty-eight bytes of partition
  table reached 2.2 GB.
- `pieeprom`: a FILE section shorter than its own header made contentSize()
  underflow to about 2^64, and readFile() built a vector from that range.
- `imagesizeparser`: the .xz index decoder was given UINT64_MAX as its memory
  limit, so a crafted 60-byte file asked liblzma for six petabytes.
- `config_txt_merge`: an item carrying the carriage return a CRLF config
  leaves on it never matched a stripped line, so the setting was appended
  again on every pass.
- `fatdir`: readFile() sized its buffer from the directory entry's declared
  file size, so an entry claiming four gigabytes allocated four gigabytes
  before a byte was read. Capped at the cluster chain and the partition.
- `eeprom_repo_override`: the value is trimmed as bytes, and a byte order
  mark is not whitespace -- so the trim stepped over it and stopped, and
  `fromUtf8()` then removed the mark and uncovered whatever stood behind it.
  A vertical tab arrived on the front of a repository URL.
- `eeprom_repo_override` again: a region is a fixed size and is padded to it,
  with nought or with the 0xFF of erased flash, and neither is whitespace --
  so the URL came back with the rest of the region on the end. The same
  shape was then found by reading in the two files the fastboot path reads
  back from the device, where it was silently discarding the whole of
  first-boot customisation.
- `fastboot_protocol`: the eight hex digits after DATA are how much the
  device says it will send, and the reserve happened before a byte arrived.
  0xFFFFFFFF asked for four gigabytes on a machine that may not have it.
- `bootloader_image`: a FILE section declaring fewer bytes than its own
  12-byte name and 4 metadata bytes made getFile() subtract past zero, and
  mid() reads a negative length as "to the end" -- the remainder of the
  EEPROM came back as bootsys, to be counter-signed and flashed.

`fuzz_fatdir` exercises writeFile() and deleteFile() as well as reading,
because writing is what actually happens to a card: config.txt, cmdline.txt
and firstrun.sh go on after the image does, so on a damaged filesystem
allocateCluster() and updateDirEntry() work from the same corrupt structures
the read path refuses. That took it from 713 edges to 1866. Subdirectory
writes are not among them: writeFile() refuses any path with a slash, and the
implementation that would have handled it is unreachable behind that throw.

It also mutates the boot sector, but only one case in eight. Every field the
constructor validates lives there -- bytes per sector, sectors per cluster,
FAT size -- so holding it at the template's values meant those checks were
never reached, including the two divide-by-zero guards and the sector-size
bound. Doing it on every case would be worse than not doing it: almost any
mutation makes the constructor throw, and the chain and directory walking
stop being exercised at all. Any case that
writes restores the template first, or the next one inherits its edits and a
crash will not reproduce on its own.

Four of the harnesses assert a property rather than waiting for a signal:
merging a config.txt item twice must equal merging it once, shell quoting
must survive being read back by a shell, the sparse encoder's output must
decode to what went in, and what `BootloaderImage::updateFile()` accepts
`getFile()` must give back. None of those would have been found by waiting
for a crash.

The defects above are one shape between them: a length taken from a number
the input chose, and used before it was weighed against anything. Worth
looking for in the rest of this tree.
