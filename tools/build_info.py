#!/usr/bin/env python3
"""Print the BuildInfo record embedded in a Monoboard firmware image (issue #49).

Reads the image -- it never runs it. Accepts:

  * .elf  -- the record is taken from the `.build_info` section;
  * .hex  -- Intel HEX is decoded, then searched for the magic word;
  * .bin, or anything else -- searched for the magic word as raw bytes.

The layout mirrors production/interfaces/build_info_types.h (layout version 1,
224 bytes, little-endian). production/tests/suites/test_fw_info.c pins the C
side of the same offsets.

Examples:

  tools/build_info.py build/mcu-debug/Monoboard.elf
  tools/build_info.py Monoboard.bin --json
  tools/build_info.py Monoboard.bin --field version
  tools/build_info.py Monoboard.elf --release-name     # Corpus-<version>-yy-mm-dd
  tools/build_info.py Monoboard.elf --require-clean    # exit 2 if dirty / no VCS

Exit status: 0 ok, 1 no valid record found, 2 --require-clean failed.
Stdlib only; no third-party deps.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = 0x4946424D  # "MBFI" little-endian; BUILD_INFO_MAGIC
LAYOUT_VERSION = 1
SECTION_NAME = ".build_info"

FLAG_DIRTY = 0x01
FLAG_NO_VCS = 0x02
FLAG_TAGGED = 0x04

# (name, capacity) in order, starting at offset 16 -- see build_info_types.h.
HEADER = struct.Struct("<IHHII")
STRINGS = (
    ("version", 32),
    ("git_sha", 48),
    ("git_branch", 32),
    ("git_tag", 32),
    ("build_date", 16),
    ("build_type", 16),
    ("toolchain", 32),
)
SIZE = HEADER.size + sum(cap for _, cap in STRINGS)  # 224


def decode(blob: bytes, offset: int) -> dict | None:
    """Decode a record at blob[offset:], or None if it is not a valid one."""
    if offset < 0 or offset + SIZE > len(blob):
        return None
    magic, layout, size, sha32, flags = HEADER.unpack_from(blob, offset)
    if magic != MAGIC or layout != LAYOUT_VERSION or size != SIZE:
        return None
    info: dict = {"git_sha32": f"{sha32:08x}", "flags": flags}
    pos = offset + HEADER.size
    for name, cap in STRINGS:
        raw = blob[pos : pos + cap]
        end = raw.find(b"\0")
        if end < 0:
            return None
        try:
            text = raw[:end].decode("ascii")
        except UnicodeDecodeError:
            return None
        if not text.isprintable():
            return None
        info[name] = text
        pos += cap
    info["dirty"] = bool(flags & FLAG_DIRTY)
    info["no_vcs"] = bool(flags & FLAG_NO_VCS)
    info["tagged"] = bool(flags & FLAG_TAGGED)
    return info


def scan(blob: bytes) -> dict | None:
    """First valid record anywhere in blob (the magic may also occur in code)."""
    needle = struct.pack("<I", MAGIC)
    pos = blob.find(needle)
    while pos >= 0:
        info = decode(blob, pos)
        if info is not None:
            return info
        pos = blob.find(needle, pos + 1)
    return None


def elf_section(blob: bytes, wanted: str) -> bytes | None:
    """Contents of a named section in a little-endian ELF32/ELF64 file."""
    if blob[:4] != b"\x7fELF" or blob[5] != 1:  # EI_DATA must be ELFDATA2LSB
        return None
    if blob[4] == 1:  # ELFCLASS32
        shoff, = struct.unpack_from("<I", blob, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 0x2E)
        sh = struct.Struct("<IIIIIIIIII")
    elif blob[4] == 2:  # ELFCLASS64
        shoff, = struct.unpack_from("<Q", blob, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 0x3A)
        sh = struct.Struct("<IIQQQQIIQQ")
    else:
        return None
    headers = [sh.unpack_from(blob, shoff + i * shentsize) for i in range(shnum)]
    if shstrndx >= len(headers):
        return None
    names_off, names_size = headers[shstrndx][4], headers[shstrndx][5]
    names = blob[names_off : names_off + names_size]
    for hdr in headers:
        name_end = names.find(b"\0", hdr[0])
        if names[hdr[0] : name_end].decode("ascii", "replace") == wanted:
            sh_type, offset, size = hdr[1], hdr[4], hdr[5]
            if sh_type == 8:  # SHT_NOBITS
                return None
            return blob[offset : offset + size]
    return None


def intel_hex(text: str) -> bytes:
    """Flatten an Intel HEX file to a contiguous image (gaps zero-filled)."""
    data: dict[int, int] = {}
    base = 0
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        count, addr, rtype = rec[0], (rec[1] << 8) | rec[2], rec[3]
        payload = rec[4 : 4 + count]
        if rtype == 0x00:
            for i, byte in enumerate(payload):
                data[base + addr + i] = byte
        elif rtype == 0x02:
            base = int.from_bytes(payload, "big") << 4
        elif rtype == 0x04:
            base = int.from_bytes(payload, "big") << 16
        elif rtype == 0x01:
            break
    if not data:
        return b""
    lo = min(data)
    image = bytearray(max(data) - lo + 1)
    for addr, byte in data.items():
        image[addr - lo] = byte
    return bytes(image)


def read_info(path: Path) -> dict | None:
    blob = path.read_bytes()
    if blob[:4] == b"\x7fELF":
        section = elf_section(blob, SECTION_NAME)
        if section is not None:
            info = decode(section, 0)
            if info is not None:
                return info
        return scan(blob)
    if path.suffix.lower() in (".hex", ".ihex"):
        return scan(intel_hex(blob.decode("ascii", "replace")))
    return scan(blob)


def release_name(info: dict) -> str:
    """Artifact name per issue #8: Corpus-<version>-yy-mm-dd (build date)."""
    return f"Corpus-{info['version']}-{info['build_date'][2:]}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=Path, help=".elf, .bin or .hex firmware image")
    out = parser.add_mutually_exclusive_group()
    out.add_argument("--json", action="store_true", help="print the record as JSON")
    out.add_argument("--field", help="print one field only (e.g. version, git_sha)")
    out.add_argument("--release-name", action="store_true",
                     help="print the release artifact name for this image")
    parser.add_argument("--require-clean", action="store_true",
                        help="exit 2 unless built from a clean git tree")
    args = parser.parse_args(argv)

    info = read_info(args.image)
    if info is None:
        print(f"{args.image}: no BuildInfo record found", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(info, indent=2))
    elif args.field:
        if args.field not in info:
            print(f"unknown field '{args.field}'; one of: {', '.join(info)}", file=sys.stderr)
            return 1
        print(info[args.field])
    elif args.release_name:
        print(release_name(info))
    else:
        width = max(len(k) for k in info)
        for key, value in info.items():
            if key == "flags":
                value = f"0x{value:02x}"
            print(f"{key:<{width}}  {value}")
        if info["dirty"]:
            print("WARNING: built from a DIRTY tree (uncommitted changes)", file=sys.stderr)
        if info["no_vcs"]:
            print("WARNING: built without git -- commit unknown", file=sys.stderr)

    if args.require_clean and (info["dirty"] or info["no_vcs"]):
        print(f"{args.image}: not a clean build ({info['version']})", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
