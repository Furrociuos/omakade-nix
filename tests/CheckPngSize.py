#!/usr/bin/env python3
"""Check the actual exported card size without image library dependencies."""
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
expected = tuple(map(int, sys.argv[2:4]))
with path.open("rb") as image:
    header = image.read(24)
if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
    raise SystemExit(f"{path} is not a PNG")
size = struct.unpack(">II", header[16:24])
if size != expected:
    raise SystemExit(f"{path}: expected {expected}, got {size}")
print(f"{path}: {size[0]}x{size[1]}")
