"""Package/check the eGON image required by Allwinner BROM (no dependencies)."""
import argparse
from pathlib import Path
import struct

STAMP = 0x5F0A6C39
IMAGE_SIZE = 8192


def verify(data):
    if len(data) != IMAGE_SIZE:
        raise ValueError("image must contain exactly 8192 bytes")
    branch, magic, checksum, length = struct.unpack_from("<I8sII", data)
    if branch != 0xEA000016 or magic != b"eGON.BT0" or length != len(data):
        raise ValueError("invalid branch, magic or length")
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    words[3] = STAMP
    if sum(words) & 0xFFFFFFFF != checksum:
        raise ValueError("invalid eGON checksum")
    print(f"eGON valid: {length} bytes, entry 0x60, checksum 0x{checksum:08x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    data = bytearray(args.source.read_bytes())
    if args.output:
        if not 96 <= len(data) <= IMAGE_SIZE:
            raise ValueError("raw payload must be between 96 and 8192 bytes")
        data.extend(bytes(IMAGE_SIZE - len(data)))
        struct.pack_into("<II", data, 12, STAMP, len(data))
        checksum = sum(struct.unpack(f"<{len(data) // 4}I", data)) & 0xFFFFFFFF
        struct.pack_into("<I", data, 12, checksum)
        verify(data)
        args.output.write_bytes(data)
    else:
        verify(data)


if __name__ == "__main__":
    main()
