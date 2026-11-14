#!/usr/bin/env python3
"""Embed the --ttf-text font file as a C array. Standard library only."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    data = arguments.source.read_bytes()
    rows = [", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 16])
            for offset in range(0, len(data), 16)]
    arguments.destination.write_text(
        f"/* Generated from {arguments.source.as_posix()} by "
        "bin/embedTtfTextFont.py. */\n"
        f"const unsigned int cbTtfTextFont = {len(data)}u;\n"
        "const unsigned char abTtfTextFont[] = {\n    " +
        ",\n    ".join(rows) + "\n};\n")


if __name__ == "__main__":
    main()
