#!/usr/bin/env python3
"""Roll a gcovr --json-summary up per production/ area, optionally vs. a base.

Usage:
    coverage_summary.py SUMMARY.json [--base BASE_SUMMARY.json]

Prints a Markdown table (line and branch coverage per area, plus the headline)
to stdout -- readable in a terminal and in a GitHub job summary. With --base,
each cell also shows the delta in percentage points against the base build.

The headline is shipped first-party code only (app, proto, services). The
driver fakes are listed for information but kept out of it: they are test
doubles, and their numbers would flatter the total (issue #53). This script
reports; it never fails on a coverage value.
"""

import argparse
import json
import sys

# (area label, path prefix relative to the repo root, counts toward headline)
AREAS = (
    ("app", "production/app/", True),
    ("proto", "production/proto/", True),
    ("services", "production/services/", True),
    ("drivers (fakes, informational)", "production/drivers/", False),
)
HEADLINE = "**headline (app + proto + services)**"


def rollup(path):
    """Return {label: (line_cov, line_tot, br_cov, br_tot)} for one summary."""
    with open(path, encoding="utf-8") as handle:
        files = json.load(handle)["files"]
    totals = {label: [0, 0, 0, 0] for label, _, _ in AREAS}
    totals[HEADLINE] = [0, 0, 0, 0]
    for entry in files:
        name = entry["filename"].replace("\\", "/")
        for label, prefix, in_headline in AREAS:
            if name.startswith(prefix):
                counts = (entry["line_covered"], entry["line_total"],
                          entry["branch_covered"], entry["branch_total"])
                targets = [label, HEADLINE] if in_headline else [label]
                for target in targets:
                    totals[target] = [a + b for a, b in zip(totals[target], counts)]
                break
    return totals


def percent(covered, total):
    return None if total == 0 else 100.0 * covered / total


def cell(covered, total, base=None):
    pct = percent(covered, total)
    if pct is None:
        text = "n/a (0)"
    else:
        text = f"{pct:.1f}% ({covered}/{total})"
    if base is not None:
        base_pct = percent(*base)
        if pct is not None and base_pct is not None:
            text += f" {pct - base_pct:+.1f}pp"
        elif pct != base_pct:
            text += " (new)" if base_pct is None else " (gone)"
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("summary", help="gcovr --json-summary of this build")
    parser.add_argument("--base", help="gcovr --json-summary of the base build")
    args = parser.parse_args()

    head = rollup(args.summary)
    base = rollup(args.base) if args.base else None

    print("| area | lines | branches |")
    print("|---|---|---|")
    for label in [a[0] for a in AREAS] + [HEADLINE]:
        lc, lt, bc, bt = head[label]
        if base is None:
            print(f"| {label} | {cell(lc, lt)} | {cell(bc, bt)} |")
        else:
            blc, blt, bbc, bbt = base[label]
            print(f"| {label} | {cell(lc, lt, (blc, blt))} "
                  f"| {cell(bc, bt, (bbc, bbt))} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
