#!/usr/bin/env python3
"""Generate the BMP simple-uppercase table used for archive path collision checks.

The table approximates how case-insensitive Windows filesystems (NTFS, exFAT)
compare names: each UTF-16 code unit is upcased on its own. Mappings come from
Python's unicodedata. A character whose full uppercase is several characters
(such as U+00DF) takes its single-character titlecase if it has one (U+1F80 ->
U+1F88), otherwise it maps to itself, which matches UnicodeData.txt.

Usage:
  generate_upcase_table.py [--format cpp|rust]          print the table body
  generate_upcase_table.py --check FILE [--format ...]   verify FILE's table

The checked-in tables were generated with Unicode 15.1 (Python 3.13). --check
skips (exit 0 with a note) under a different Unicode version, since newer data
may legitimately add mappings.
"""

import argparse
import re
import sys
import unicodedata

EXPECTED_UNICODE_VERSION = "15.1.0"


def simple_upper(cp):
    ch = chr(cp)
    upper = ch.upper()
    if len(upper) == 1:
        return ord(upper)
    title = ch.title()
    return ord(title) if len(title) == 1 else cp


def ranges():
    pairs = [(cp, simple_upper(cp)) for cp in range(0x80, 0x10000)
             if not 0xD800 <= cp <= 0xDFFF and simple_upper(cp) != cp]
    out = []
    i = 0
    while i < len(pairs):
        cp, upper = pairs[i]
        delta = upper - cp
        best_count, best_stride = 1, 1
        # Greedily extend a run with a constant delta and a stride of 1 or 2.
        for stride in (1, 2):
            j = i
            while (j + 1 < len(pairs) and pairs[j + 1][0] == pairs[j][0] + stride
                   and pairs[j + 1][1] - pairs[j + 1][0] == delta):
                j += 1
            if j - i + 1 > best_count:
                best_count, best_stride = j - i + 1, stride
        out.append((cp, cp + (best_count - 1) * best_stride, delta, best_stride))
        i += best_count
    return out


def render(fmt):
    lines = []
    for first, last, delta, stride in ranges():
        if fmt == "cpp":
            lines.append(f"\t{{0x{first:04X}, 0x{last:04X}, {delta}, {stride}}},")
        else:
            lines.append(f"    UpcaseRange {{ first: 0x{first:04X}, last: 0x{last:04X}, "
                         f"delta: {delta}, stride: {stride} }},")
    return "\n".join(lines) + "\n"


def extract(text, fmt):
    pattern = (r"BMP_UPCASE_RANGES\[\] = \{\n(.*?)^\};" if fmt == "cpp"
               else r"BMP_UPCASE_RANGES: &\[UpcaseRange\] = &\[\n(.*?)^\];")
    match = re.search(pattern, text, re.S | re.M)
    if not match:
        sys.exit("BMP_UPCASE_RANGES table not found")
    return match.group(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--format", choices=("cpp", "rust"), default="cpp")
    parser.add_argument("--check", metavar="FILE")
    args = parser.parse_args()

    if not args.check:
        sys.stdout.write(render(args.format))
        return
    if unicodedata.unidata_version != EXPECTED_UNICODE_VERSION:
        print(f"skipped: Python has Unicode {unicodedata.unidata_version}, "
              f"table was generated from {EXPECTED_UNICODE_VERSION}")
        return
    with open(args.check, encoding="utf-8") as source:
        if extract(source.read(), args.format) != render(args.format):
            sys.exit(f"{args.check}: BMP_UPCASE_RANGES differs from the generated table")
    print(f"{args.check}: BMP_UPCASE_RANGES matches the generated table")


if __name__ == "__main__":
    main()
