#!/usr/bin/env python3
"""
Write a seed corpus for the ECP fuzz target: one valid request for each command.

Usage: make_seeds.py <output directory>

See src/main.c for the input format. CRCs are left as zero, with the flag that makes
the target fill them in.
"""

import struct
import sys
from pathlib import Path

FIX_CRCS = 0x01
PROTO_VERSION = 2

# (name, command ID, command version, payload)
REQUESTS = [
    ("proto_version", 0x0000, 2, b""),
    ("ident", 0x0001, 1, struct.pack("<BBHI16s", 1, 2, 3, 4, b"45116-560")),
    ("enum", 0x0002, 1, struct.pack("<BBB", 0, 1, 2)),
    ("hello", 0x0003, 1, struct.pack("<I", 0x12345678)),
    ("echo", 0x0004, 1, bytes(range(16))),
    ("features", 0x0006, 1, b""),
    ("estop", 0x0008, 1, b""),
    ("io_read", 0x1000, 2, struct.pack("<B", 0)),
    ("io_read_all", 0x1000, 3, b""),
    ("io_write", 0x1001, 1, struct.pack("<Bi", 1, 5000)),
    ("io_get_attrib", 0x1002, 1, struct.pack("<BB", 0, 0)),
    ("io_set_attrib", 0x1003, 1, struct.pack("<BBI", 1, 1, 250)),
    ("io_pause", 0x1004, 1, struct.pack("<BB", 0x01, 0x03)),
    ("io_clear_fault", 0x1005, 1, struct.pack("<B", 1)),
    ("led_color", 0x1100, 1, bytes([0, 3, 0x10, 0x20, 0x30])),
    ("led_brightness", 0x1101, 1, bytes([0xFF, 128])),
    ("gpio_config", 0x1200, 1, struct.pack("<HH", 0x0003, 0x003F)),
    ("gpio", 0x1201, 1, struct.pack("<HH", 0x0004, 0x003F)),
    ("comm_stats", 0xFF02, 1, b""),
    ("uptime", 0xFF03, 1, b""),
]


def request(command: int, version: int, payload: bytes, seq: int = 0, dup: bool = False) -> bytes:
    flags = PROTO_VERSION | (seq << 5) | (int(dup) << 7)
    data_len = len(payload) | (version << 10)
    header = struct.pack("<BHHB", flags, command, data_len, 0)
    return header + (payload + b"\0\0" if payload else b"")


def record(frame: bytes) -> bytes:
    return struct.pack("<BH", FIX_CRCS, len(frame)) + frame


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    for name, command, version, payload in REQUESTS:
        (out / name).write_bytes(record(request(command, version, payload)))

    # A request, then a duplicate of it
    _, command, version, payload = next(r for r in REQUESTS if r[0] == "hello")
    (out / "hello_dup").write_bytes(
        record(request(command, version, payload, seq=1))
        + record(request(command, version, payload, seq=1, dup=True))
    )


if __name__ == "__main__":
    main()
