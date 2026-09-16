#!/usr/bin/env python3
"""Convert the client's screenshot TGAs to PNG so they can be viewed.

The client writes uncompressed TGA because that is what the reference writes and because it needs no
encoder in the client. Nothing in the viewing path reads TGA, so convert here. Stdlib only -- zlib
is all a PNG encoder needs, and Pillow is not installed on this machine.

    python tools/tga2png.py shot.tga [out.png]
"""

import struct
import sys
import zlib


def read_tga(path):
    with open(path, "rb") as handle:
        data = handle.read()

    id_len, _, image_type = data[0], data[1], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    depth, descriptor = data[16], data[17]

    if image_type not in (2, 3):
        raise SystemExit("only uncompressed TGA is supported, got image type %d" % image_type)

    offset = 18 + id_len
    pixel_bytes = depth // 8
    rows = []

    for y in range(height):
        row = bytearray()
        start = offset + y * width * pixel_bytes

        for x in range(width):
            pixel = data[start + x * pixel_bytes:start + (x + 1) * pixel_bytes]

            if depth == 8:
                row += bytes(pixel) * 3
            else:
                # TGA stores BGR; PNG wants RGB.
                row += bytes((pixel[2], pixel[1], pixel[0]))

        rows.append(bytes(row))

    # Bit 5 of the descriptor set means the first row in the file is the top row.
    if not (descriptor & 0x20):
        rows.reverse()

    return width, height, rows


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        handle.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    source = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else source.rsplit(".", 1)[0] + ".png"

    width, height, rows = read_tga(source)
    write_png(target, width, height, rows)

    print("%s -> %s (%dx%d)" % (source, target, width, height))


if __name__ == "__main__":
    main()
