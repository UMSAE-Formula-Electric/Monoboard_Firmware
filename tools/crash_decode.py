#!/usr/bin/env python3
"""Resolve a Monoboard crash dump's PC/LR to file:line (issue #45).

The firmware's boot report (crash_format_report() in
production/drivers/crash_record.c) prints lines such as

    CRASH pc=0x08000456 lr=0x08000123 xpsr=0x01000000 sp=0x20001F80
    CRASH cfsr=0x01000000 hfsr=0x00000000 reason=UNALIGNED: unaligned load/store

Feed that text (or bare addresses) together with the .elf that was running
when it crashed -- the exact build, or the lines will be wrong:

    tools/crash_decode.py --elf build/mcu-debug/Monoboard.elf report.txt
    tools/crash_decode.py --elf build/mcu-debug/Monoboard.elf < report.txt
    tools/crash_decode.py --elf build/mcu-debug/Monoboard.elf --pc 0x08000456 --lr 0x08000123

Uses arm-none-eabi-addr2line (override with --addr2line).
"""

import argparse
import re
import shutil
import subprocess
import sys

FIELD_RE = re.compile(r"\b(pc|lr)=0x([0-9A-Fa-f]{1,8})\b")
# Values the core loads into LR on exception entry -- not code addresses.
EXC_RETURN_MIN = 0xFFFFFFE0


def parse_report(text):
    """Return {'pc': int, 'lr': int} for whichever fields the text carries."""
    found = {}
    for key, value in FIELD_RE.findall(text):
        found.setdefault(key, int(value, 16))
    return found


def lookup_address(key, value):
    """Address to hand addr2line, or None if the value is not code.

    The Thumb bit is cleared. The stacked LR is a *return* address -- the
    instruction after the call -- so step back one byte to land on the call
    itself; the stacked PC is the faulting instruction exactly.
    """
    if value >= EXC_RETURN_MIN or value == 0:
        return None
    addr = value & ~1
    if key == "lr":
        addr -= 1
    return addr


def addr2line(tool, elf, addr):
    out = subprocess.run(
        [tool, "-e", elf, "-f", "-C", "-i", "-p", hex(addr)],
        check=True,
        capture_output=True,
        text=True,
    )
    return out.stdout.strip()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", required=True, help="the exact .elf that produced the dump")
    parser.add_argument("--pc", type=lambda s: int(s, 0), help="stacked PC (overrides report)")
    parser.add_argument("--lr", type=lambda s: int(s, 0), help="stacked LR (overrides report)")
    parser.add_argument("--addr2line", default="arm-none-eabi-addr2line",
                        help="addr2line binary (default: %(default)s)")
    parser.add_argument("report", nargs="?",
                        help="file holding the boot report text (default: stdin, "
                             "unless --pc/--lr are given)")
    args = parser.parse_args(argv)

    fields = {}
    if args.report is not None:
        with open(args.report, encoding="utf-8", errors="replace") as handle:
            fields = parse_report(handle.read())
    elif args.pc is None and args.lr is None:
        fields = parse_report(sys.stdin.read())
    if args.pc is not None:
        fields["pc"] = args.pc
    if args.lr is not None:
        fields["lr"] = args.lr
    if not fields:
        parser.error("no pc=/lr= values found; pass a report or --pc/--lr")

    if shutil.which(args.addr2line) is None:
        parser.error(f"{args.addr2line} not found on PATH")

    labels = {"pc": "PC (faulting instruction)", "lr": "LR (caller / return site)"}
    for key in ("pc", "lr"):
        if key not in fields:
            continue
        value = fields[key]
        addr = lookup_address(key, value)
        if addr is None:
            print(f"{labels[key]}: 0x{value:08X} -> not a code address "
                  "(EXC_RETURN or empty: faulted in a handler, or a software crash record)")
            continue
        print(f"{labels[key]}: 0x{value:08X} -> {addr2line(args.addr2line, args.elf, addr)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
