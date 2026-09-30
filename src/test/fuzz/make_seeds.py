#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
"""Build the seed corpora the harnesses need to reach their subjects.

Three of them cannot get started on their own:

  fuzz_parttable  needs a partition table that points at the filesystem sitting
                  64 KiB into the image. The mutator will not produce a start
                  LBA landing exactly there, so without this seed the FAT
                  parser behind the table is never reached at all.

  fuzz_fastboot   needs replies shaped like the protocol -- a length byte then
                  the packet -- or nothing gets past readResponse()'s first
                  four-byte check.

  fuzz_sparse_roundtrip
                  needs large inputs. libFuzzer sizes its input limit from the
                  largest file in the corpus, and the harness allows sixty-four
                  blocks: seeded with small files it never builds an image big
                  enough to span segments, which is the boundary behaviour the
                  round-trip exists to check.

  fuzz_imagesize  does better with real containers than without, though it is
                  the one case here where the mutator can manage on its own:
                  106 edges unseeded against 125 seeded.

  fuzz_bmap       needs a document that parses. A .bmap is XML, and a mutator
                  will not invent well-formed XML with the four elements the
                  parser insists on: from an empty corpus it reported 25 edges
                  in a twenty-five second run and never reached past parse()
                  at all, which is where serialize() and the two lookup paths
                  live -- the half the harness exists for.

It also builds the FAT template that fuzz_fatdir and fuzz_parttable read
through FUZZ_FAT_TEMPLATE, because nothing else in the tree did and both
harnesses refuse to start without it. Losing a scratch directory was enough to
take the two of them out of service entirely.

Generated rather than committed because the sparse images are a megabyte and
the template is thirty-four. The rest are tiny, and are written here too so
that one command produces everything.

    python3 src/test/fuzz/make_seeds.py <corpus-root>

Each corpus lands in <corpus-root>/corpus_<name>/, which is what the run
commands in README.md expect, and the template in <corpus-root>/template.img
unless FUZZ_FAT_TEMPLATE names somewhere else.
"""

import gzip
import io
import lzma
import os
import random
import shutil
import struct
import subprocess
import sys
import tarfile

BLK = 4096
TABLE_AREA = 64 * 1024


def write(root, corpus, name, data):
    d = os.path.join(root, corpus)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)


def oslist_seeds(root):
    """Repository documents whose icons the parser has to route.

    The shapes that matter are nested: parseOSJson turns an entry's subitems
    into a JSON string, and the picker flattens that string itself, so an icon
    down there reaches Image.source without passing the model that routes the
    top level.
    """
    docs = {
        "nested_remote_icon.json":
            '{"os_list":[{"name":"Cat","subitems":['
            '{"name":"A","icon":"https://example.com/a.png"}]}]}',
        "nested_two_deep.json":
            '{"os_list":[{"name":"Top","subitems":[{"name":"Mid","subitems":['
            '{"name":"Leaf","icon":"http://example.com/l.png"}]}]}]}',
        "mixed_icon_forms.json":
            '{"os_list":[{"name":"Cat","icon":"icons/cat.png","subitems":['
            '{"name":"A","icon":"HTTPS://example.com/a.png"},'
            '{"name":"B","icon":"file://host/share/b.png"},'
            '{"name":"C","icon":"qrc:/icons/c.png"},'
            '{"name":"D","icon":""}]}]}',
        "bom_icon_key.json":
            '{"os_list":[{"name":"Cat","subitems":['
            '{"name":"A","\ufeffic' 'on":"https://example.invalid/a.png"}]}]}',
        "devices_and_list.json":
            '{"imager":{"latest_version":"1.9.0","url":"","devices":['
            '{"name":"Pi 5","icon":"https://example.com/p5.png",'
            '"matching_type":"exclusive"}]},'
            '"os_list":[{"name":"Cat","subitems":[{"name":"A"}]}]}',
    }
    for name, body in docs.items():
        write(root, "corpus_oslist", name, body.encode())


def parttable_seeds(root, template_size):
    """An MBR whose first entry is the filesystem behind the table area."""
    mbr = bytearray(512)
    e = 446
    mbr[e + 0] = 0x80                       # bootable
    mbr[e + 1:e + 4] = b"\xff\xff\xff"      # CHS, ignored
    mbr[e + 4] = 0x0C                       # FAT32 LBA
    mbr[e + 5:e + 8] = b"\xff\xff\xff"
    mbr[e + 8:e + 12] = struct.pack("<I", TABLE_AREA // 512)
    mbr[e + 12:e + 16] = struct.pack("<I", template_size // 512)
    mbr[510], mbr[511] = 0x55, 0xAA
    write(root, "corpus_pt", "mbr_points_at_fs.bin", bytes(mbr))


def fastboot_seeds(root):
    """Replies framed the way ScriptedTransport reads them: length byte first.

    The length byte is len-1, because the transport does data[pos] + 1.
    """
    def frame(*packets):
        out = bytearray()
        for p in packets:
            b = p.encode()
            out.append(len(b) - 1)
            out += b
        return bytes(out)

    seeds = {
        "okay.bin":        frame("OKAY"),
        "fail.bin":        frame("FAILunknown command"),
        "info_okay.bin":   frame("INFOerasing", "INFOwriting", "OKAY"),
        "data_okay.bin":   frame("DATA00001000", "OKAY"),
        # A size no device should send, and two that are not hex at all --
        # the second is what readResponse() hands to std::stoul.
        "data_huge.bin":   frame("DATAffffffff", "OKAY"),
        "data_nonhex.bin": frame("DATAzzzzzzzz", "OKAY"),
        "data_neg.bin":    frame("DATA-0000001", "OKAY"),
        "text.bin":        frame("TEXTmore detail", "OKAY"),
        "mount_seq.bin":   frame("OKAY", "OKAY", "OKAY", "OKAY"),
        "truncated.bin":   frame("OK"),
        "bogus.bin":       frame("ZZZZnot a prefix"),
    }
    for name, body in seeds.items():
        write(root, "corpus_fb", name, body)


def sparse_seeds(root):
    """Images big enough to span segments, in the shapes the encoder splits on."""
    rng = random.Random(20260912)

    def image(blocks, fill):
        body = bytearray()
        for i in range(blocks):
            body += fill(i)
        # The first two bytes choose the segment geometry.
        return bytes([0x03, 0x00]) + bytes(body)

    zero = lambda i: b"\x00" * BLK
    rand = lambda i: bytes(rng.getrandbits(8) for _ in range(BLK))
    mixed = lambda i: (b"\x00" * BLK) if i % 3 else bytes([i & 0xFF]) * BLK
    runs = lambda i: bytes([0xFF if (i // 4) % 2 else 0x00]) * BLK
    mostly = lambda i: (b"\x00" * BLK) if i % 5 else (bytes([i & 0xFF]) + b"\x00" * (BLK - 1))

    for name, blocks, fill in [
        ("big_zeros_64.bin", 64, zero),
        ("big_random_64.bin", 64, rand),
        ("big_mixed_48.bin", 48, mixed),
        ("big_runs_32.bin", 32, runs),
        ("big_sparse_40.bin", 40, mostly),
    ]:
        write(root, "corpus_sparse", name, image(blocks, fill))


def imagesize_seeds(root):
    """Containers the parser recognises, so mutation starts at the fields.

    Worth less than the others -- the magic numbers are short enough that the
    mutator finds them unaided -- but it is a fifth of the coverage for three
    hundred bytes: 106 edges from nothing against 125 from these, over thirty
    seconds each. gzip and lzma are in the standard library; zstd is not, and
    is left to the mutator.
    """
    payload = b"raspberry pi imager test payload, repeated. " * 40

    seeds = {
        "plain.gz": gzip.compress(payload),
        "plain.xz": lzma.compress(payload, format=lzma.FORMAT_XZ),
        # A stream whose declared size is the interesting field, and one
        # holding nothing at all: both are shapes a truncated download takes.
        "empty.gz": gzip.compress(b""),
        "empty.xz": lzma.compress(b"", format=lzma.FORMAT_XZ),
        "lzma_alone.lzma": lzma.compress(payload, format=lzma.FORMAT_ALONE),
    }

    # Filters other than plain LZMA2, which is the only one the corpus held.
    # probeFormat() switches on the filter code and the branch for anything
    # else had never been taken: the mutator will not invent a valid filter
    # chain, so it has to be handed one.
    # Looked up by name rather than named directly: which BCJ filters a given
    # Python exposes varies, and one missing attribute would otherwise take
    # the whole seed set down with it.
    for name, attr, extra in (("delta.xz", "FILTER_DELTA", {"dist": 4}),
                              ("bcj_arm64.xz", "FILTER_ARM64", {}),
                              ("bcj_arm.xz", "FILTER_ARM", {}),
                              ("bcj_x86.xz", "FILTER_X86", {})):
        fid = getattr(lzma, attr, None)
        if fid is None:
            continue
        chain = [dict(id=fid, **extra), {"id": lzma.FILTER_LZMA2, "preset": 1}]
        try:
            seeds[name] = lzma.compress(payload, format=lzma.FORMAT_XZ, filters=chain)
        except (lzma.LZMAError, ValueError):
            pass        # this build will not take that chain; the rest stand

    # zstd is not in the standard library, so these come from the tool if it
    # is installed. Three shapes, because the parser treats them differently:
    # a size recorded in the frame, no size recorded at all (streaming), and a
    # frame declaring nothing in it -- the last of which had never been seen.
    if shutil.which("zstd"):
        def zstd(data, args=()):
            r = subprocess.run(["zstd", "-q", "-c", *args],
                               input=data, capture_output=True)
            return r.stdout if r.returncode == 0 else None

        # Compressing a named file lets zstd record the content size; reading
        # stdin it cannot know it, which is the streaming case.
        import tempfile
        with tempfile.NamedTemporaryFile(suffix=".bin") as tf:
            tf.write(payload)
            tf.flush()
            r = subprocess.run(["zstd", "-q", "-c", tf.name], capture_output=True)
            if r.returncode == 0:
                seeds["sized.zst"] = r.stdout
        for name, body in (("streaming.zst", zstd(payload)),
                           ("empty.zst", zstd(b""))):
            if body:
                seeds[name] = body

    # Too short to hold what the parser is about to read: an xz below the
    # stream header, and a gzip below its own trailer. Both are early returns
    # that a download cut off mid-transfer arrives as.
    seeds["stub.xz"] = lzma.compress(payload, format=lzma.FORMAT_XZ)[:8]
    seeds["stub.gz"] = gzip.compress(payload)[:6]

    # ISIZE at its maximum, which is the field the 4GB estimate is built on.
    full = bytearray(gzip.compress(payload))
    full[-4:] = b"\xff\xff\xff\xff"
    seeds["isize_max.gz"] = bytes(full)

    for name, body in seeds.items():
        write(root, "corpus_imagesize", name, body)


def bmap_seeds(root):
    """Documents that parse, so the mutator starts past the XML."""

    def doc(ranges, block_size="4096", blocks="100", mapped="20",
            version="2.0"):
        return (
            '<?xml version="1.0" ?>\n'
            '<bmap version="%s">\n'
            "  <BlockSize>%s</BlockSize>\n"
            "  <BlocksCount>%s</BlocksCount>\n"
            "  <MappedBlocksCount>%s</MappedBlocksCount>\n"
            "  <BlockMap>\n%s  </BlockMap>\n"
            "</bmap>\n" % (version, block_size, blocks, mapped, ranges)
        ).encode()

    chksum = "a" * 64
    seeds = {
        # The shapes the parser branches on: a span, a single block, ranges
        # out of order, a checksum attribute, and one with nothing mapped.
        "two_ranges.bmap": doc("    <Range>0-9</Range>\n"
                               "    <Range>20-29</Range>\n"),
        "single_block.bmap": doc("    <Range>42</Range>\n", mapped="1"),
        "out_of_order.bmap": doc("    <Range>50-59</Range>\n"
                                 "    <Range>0-9</Range>\n"
                                 "    <Range>20-29</Range>\n", mapped="30"),
        "with_chksum.bmap": doc('    <Range chksum="%s">0-9</Range>\n' % chksum,
                                mapped="10"),
        "empty_map.bmap": doc("", mapped="0"),
        # Counts that disagree with the ranges, and a whole-image map: both
        # are accepted, and both drive the lookups differently.
        "everything.bmap": doc("    <Range>0-99</Range>\n", mapped="100"),
        "overshoot.bmap": doc("    <Range>0-9</Range>\n", blocks="4",
                              mapped="10"),
    }
    for name, body in seeds.items():
        write(root, "corpus_bmap", name, body)


def fat_template(path):
    """A FAT32 filesystem with the files the FAT harness asks for by name.

    Thirty-four megabytes, which is the smallest a FAT32 can be: 512-byte
    sectors and one sector per cluster give about 69,000 of them, and below
    65,525 mkfs writes a FAT16 instead. Size is what sets this harness's
    throughput, because every iteration lays the template down again --
    measured at 100 executions a second here against 55 from a 64 MiB one,
    for the same coverage.
    """
    tools = ["mkfs.vfat", "mcopy", "mmd"]
    missing = [t for t in tools if shutil.which(t) is None]
    if missing:
        print(f"warning: no {', '.join(missing)} (dosfstools, mtools); "
              f"{path} not built, and fuzz_fatdir and fuzz_parttable cannot "
              f"run without it", file=sys.stderr)
        return False

    if os.path.exists(path):
        os.unlink(path)
    with open(path, "wb") as f:
        f.truncate(34 * 1024 * 1024)
    subprocess.run(["mkfs.vfat", "-F", "32", "-S", "512", "-s", "1",
                    "-n", "BOOT", path], check=True,
                   stdout=subprocess.DEVNULL)

    # The names the harness reads back by hand: a short one, a long one that
    # needs an LFN chain, and one in a subdirectory.
    files = {
        "config.txt": b"dtparam=audio=on\ndtoverlay=vc4-kms-v3d\narm_64bit=1\n",
        "cmdline.txt": b"console=serial0,115200 root=PARTUUID=deadbeef-02 "
                       b"rootfstype=ext4 rootwait\n",
        "a-very-long-filename-for-lfn-testing.conf":
            bytes(random.Random(20260912).getrandbits(8) for _ in range(700)),
    }
    tmp = path + ".stage"
    for name, body in files.items():
        with open(tmp, "wb") as f:
            f.write(body)
        subprocess.run(["mcopy", "-i", path, tmp, "::" + name], check=True)
    subprocess.run(["mmd", "-i", path, "::overlays"], check=True)
    with open(tmp, "wb") as f:
        f.write(bytes(random.Random(7).getrandbits(8) for _ in range(1200)))
    subprocess.run(["mcopy", "-i", path, tmp, "::overlays/vc4-kms-v3d.dtbo"],
                   check=True)
    os.unlink(tmp)
    return True


def fileserver_seeds(root):
    """The names a device might ask for: the ordinary ones, the ones that
    climb out of the served directory, and the two symlinks the harness
    plants inside it."""
    names = [
        "bootcode4.bin", "2712/bootcode5.bin", "start4.elf", "fixup4.dat",
        "*", "*BOARD", "",
        ".", "..", "./bootcode4.bin", "2712/../bootcode4.bin",
        "../secret.txt", "./../secret.txt", "sub/../../secret.txt",
        "../../../../../../etc/shadow", "/etc/shadow",
        "escape", "escape/.", "up/secret.txt", "up/./secret.txt",
    ]
    for name in names:
        safe = name.replace("/", "_").replace(".", "-").replace("*", "star")
        write(root, "corpus_fileserver", "name_%s" % (safe or "empty"),
              name.encode())


def bootfiles_seeds(root):
    """A TAR holding the names the firmware manager looks up.

    Without one the harness stops at the first header check and everything
    past it -- find(), the chip-prefixed lookup, the size the entry declares
    -- is never reached. A mutator will not invent a 512-byte header with a
    correct octal checksum in it, and over two minutes from cold it did not:
    one corpus entry, and the archive never opened.
    """
    buf = io.BytesIO()
    # No compression and a fixed mtime, so the seed is the same every run and
    # a finding replays against the file that produced it.
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.GNU_FORMAT) as tar:
        for name, body in [
            ("bootcode4.bin", b"\x00\x01\x02\x03" * 64),
            ("2712/bootcode5.bin", b"\xaa\xbb" * 128),
            ("2711/bootcode4.bin", b"\xcc\xdd" * 128),
            ("config.txt", b"[all]\narm_64bit=1\n"),
        ]:
            info = tarfile.TarInfo(name)
            info.size = len(body)
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            tar.addfile(info, io.BytesIO(body))
    write(root, "corpus_bootfiles", "bundle.tar", buf.getvalue())


def _der(tag, body):
    if len(body) < 0x80:
        return bytes([tag, len(body)]) + body
    length = len(body).to_bytes((len(body).bit_length() + 7) // 8, "big")
    return bytes([tag, 0x80 | len(length)]) + length + body


def _der_int(value):
    raw = value.to_bytes((value.bit_length() + 7) // 8 or 1, "big")
    # ASN.1 INTEGER is signed, so a leading high bit needs a zero in front.
    # The sign-byte strip on the way back out is one of the paths this covers.
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return _der(0x02, raw)


def der_seeds(root):
    """A SubjectPublicKeyInfo, which is four nested structures deep.

    Not a real key: the parser reads shape and lengths, never arithmetic, so
    a fixed modulus of the right size reaches everything a genuine one would
    -- the nested SEQUENCEs, the BIT STRING, the sign byte, the exponent.
    Fixed rather than generated, so the seed does not change between runs.
    """
    rnd = random.Random(0x5150)
    modulus = int.from_bytes(bytes([0x80]) + bytes(rnd.randrange(256)
                                                   for _ in range(255)), "big")
    rsa_oid = bytes([0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D,
                     0x01, 0x01, 0x01])
    algorithm = _der(0x30, rsa_oid + bytes([0x05, 0x00]))
    key = _der(0x30, _der_int(modulus) + _der_int(65537))
    spki = _der(0x30, algorithm + _der(0x03, b"\x00" + key))
    write(root, "corpus_der", "rsa2048_spki.der", spki)

    # The shorter key the same parser is handed on older images. It is
    # refused rather than parsed, which is a branch of its own.
    key1024 = _der(0x30, _der_int(int.from_bytes(
        bytes([0x80]) + bytes(rnd.randrange(256) for _ in range(127)), "big"))
        + _der_int(65537))
    write(root, "corpus_der", "rsa1024_spki.der",
          _der(0x30, algorithm + _der(0x03, b"\x00" + key1024)))


def sparse_bmap_seeds(root):
    """Images large enough for the block-map target to accept at all.

    libFuzzer takes its length limit from the largest file in the corpus, and
    from empty that is 4096 bytes -- one block, which is less than the ten
    header bytes plus a block that fuzz_sparse_bmap needs before it will look
    at anything. Unseeded it reached 2 edges over 39 million executions,
    having refused every input on the size check.

    The first ten bytes are the segment choice and the mapped-block bitmap;
    the rest is the image.
    """
    blk = 4096
    for name, blocks, bits, fill in [
        # Everything mapped, distinct content per block.
        ("all_mapped", 8, 0xFF, None),
        # Alternating, so ranges and gaps both appear.
        ("alternating", 8, 0xAA, None),
        # One long run, which is what a real bmap mostly looks like.
        ("one_run", 12, 0x0FF0, None),
        # Nothing mapped: every block DONT_CARE.
        ("none_mapped", 6, 0x00, None),
        # Fill blocks, which the encoder emits as FILL rather than RAW.
        ("fill_blocks", 8, 0xFF, 0x00),
        ("fill_nonzero", 8, 0x3C, 0xA5),
    ]:
        body = bytearray()
        body += bytes([0x03, 0x00])                 # segment choice
        body += bits.to_bytes(8, "little")          # mapped-block bitmap
        for b in range(blocks):
            if fill is None:
                body += bytes([(b * 7 + 1) & 0xFF]) * blk
            else:
                body += bytes([fill]) * blk
        write(root, "corpus_sparse_bmap", name, bytes(body))


def regression_seeds(root):
    """The inputs that actually found defects, kept where git keeps them.

    libFuzzer writes a crash to its findings directory and keeps working
    copies in the corpus, and both live under the build tree -- which is
    gitignored, so a clean of that tree loses them. These three found real
    defects, so they belong in the seeds, where they are replayed by every
    run from cold.
    """
    # fuzz_blconfig: a byte order mark is not whitespace, so the byte-level
    # trim stepped over it and stopped; fromUtf8() then removed the mark and
    # uncovered the vertical tab hiding behind it, on a repository URL.
    write(root, "corpus_blconfig", "regression_bom_hides_vtab",
          b"IMAGER_REPO_URL=\xef\xbb\xbf\x0bIMAGER_REPO_URL="
          b"\x02_REPO_URL=\x83")

    # fuzz_blconfig again: a region is padded to a fixed size with nought or
    # 0xFF, neither of which is whitespace, so the padding came back on the
    # end of the URL.
    write(root, "corpus_blconfig", "regression_region_padding",
          b"J\n2\x02\x7f\x00\n\x00\x00\x00\x00\x00\x00\x00\xb4RE\n*\n\n\x1a\nR\x1aER\x1f\x00M\n"
          b"IMAGER_REPO_URL=\xef\xbb\xbf\x0b" + b"\x00" * 11 + b"\n"
          b"IMAGER_REPO_URL=\xef\xbb\xbf\x0b\x00\x00\x00IM\n"
          b"IMAGER_REPO_URL=\r\xef\xbb\xbf\x0b\x00K_\n*\nL\nER_\n*\n_\n\x81\n\x00")

    # fuzz_fastboot: OKAY, then a DATA response declaring 0x93550679 bytes.
    # The reserve happened before a byte arrived, so twenty-four bytes asked
    # for two and a half gigabytes and never had to send any of it.
    write(root, "corpus_fb", "regression_declared_upload_size",
          b"\x03OKAY\x0bDATA93550679#i\nr0A")


def drivelist_seeds(root):
    """lsblk answers, in the two shapes its versions produce.

    The booleans arrive as JSON booleans on some versions and as the strings
    "0" and "1" on others, and the parser handles both -- so a corpus with
    only one shape covers half the branches. A mutator will not turn one into
    the other, because it would have to invent matching quotes.
    """
    both = [
        # Modern lsblk: real booleans, size as a number.
        '{"blockdevices":[{"kname":"/dev/sda","subsystems":"block:scsi:usb",'
        '"size":32010928128,"ro":false,"rm":true,"model":"Generic",'
        '"mountpoints":["/media/card"]}]}',
        # Older lsblk: everything as strings.
        '{"blockdevices":[{"kname":"/dev/mmcblk0","subsystems":"block:mmc",'
        '"size":"15931539456","ro":"0","rm":"1","model":"SD","hotplug":"1"}]}',
        # A system disk and a loop device beside a card: what the filter has
        # to tell apart.
        '{"blockdevices":['
        '{"kname":"/dev/nvme0n1","subsystems":"block:nvme:pci","size":512110190592,'
        '"ro":false,"rm":false,"mountpoints":["/"]},'
        '{"kname":"/dev/loop0","subsystems":"block","size":134217728,'
        '"ro":true,"rm":false},'
        '{"kname":"/dev/sdb","subsystems":"block:scsi:usb","size":64023257088,'
        '"ro":false,"rm":true}]}',
        # Nothing plugged in, which is a state the list has to draw.
        '{"blockdevices":[]}',
        # A device with no size, which the model drops unless it is fastboot.
        '{"blockdevices":[{"kname":"/dev/sdc","subsystems":"block:scsi:usb",'
        '"size":0,"ro":false,"rm":true}]}',
    ]
    for i, body in enumerate(both):
        write(root, "corpus_drivelist", "lsblk_%d" % i, body.encode())


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(root, exist_ok=True)
    template = os.environ.get("FUZZ_FAT_TEMPLATE",
                              os.path.join(root, "template.img"))
    if not os.path.exists(template):
        fat_template(template)
    template_size = (os.path.getsize(template) if os.path.exists(template)
                     else 34 * 1024 * 1024)
    if not os.path.exists(template):
        print(f"warning: {template} not found, assuming {template_size} bytes "
              f"for the partition seed", file=sys.stderr)

    parttable_seeds(root, template_size)
    fastboot_seeds(root)
    sparse_seeds(root)
    sparse_bmap_seeds(root)
    bmap_seeds(root)
    imagesize_seeds(root)
    fileserver_seeds(root)
    bootfiles_seeds(root)
    der_seeds(root)
    regression_seeds(root)
    drivelist_seeds(root)
    oslist_seeds(root)
    print(f"seeds written under {os.path.abspath(root)}")
    if os.path.exists(template):
        print(f"FUZZ_FAT_TEMPLATE={os.path.abspath(template)}")


if __name__ == "__main__":
    main()
