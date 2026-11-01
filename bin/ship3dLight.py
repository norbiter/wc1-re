#!/usr/bin/env python3
"""Convert the point-cloud relighting data into the sidecars the SDL port reads.

For every ship3d/<NAME>.npz this writes ship3d/<NAME>.light next to <NAME>.ply.
Only the Python standard library is needed.  See ship3d/README.md, "Relighting".

    python3 bin/ship3dLight.py [ship3d-directory] [GAME.PAL]

The sidecar (all little-endian) holds:

    char[4]   "S3DL"
    uint32    version (1), uint32 point count
    float32   c            reference n.v of the ship's headlight fit
    float32   b[16]        luminance change per unit n.v, per hue bank
    uint8     lut[16][1024] brightness*4 -> palette index inside each hue bank
    int8      bank[count]  hue bank; -1 = grey fill, never shaded
    uint8     fixed[count] palette index used when bank == -1
    float32   albedo[count]

The renderer relights with  lum = albedo + b[bank] * (clip(n.L) - c)  and
draws palette index lut[bank][round(lum * 4)].  Because it emits palette indices
rather than RGB, the game's own palette fades act on the ships unchanged.
"""
import ast
import os
import struct
import sys
import zipfile

LUM = (0.30, 0.59, 0.11)
LUT_SIZE = 1024  # quarter-unit brightness steps, 0..255.75


def read_palette(path):
    """The 256 RGB triples (0-255) of an ILBM CMAP chunk."""
    data = open(path, "rb").read()
    at = data.index(b"CMAP")
    size = struct.unpack(">I", data[at + 4:at + 8])[0]
    cmap = data[at + 8:at + 8 + size]
    if len(cmap) < 768:
        raise ValueError("CMAP shorter than 256 entries")
    return [tuple(cmap[i * 3:i * 3 + 3]) for i in range(256)]


def read_npy(blob):
    version = blob[6]
    if version == 1:
        header_length = struct.unpack("<H", blob[8:10])[0]
        start = 10
    else:
        header_length = struct.unpack("<I", blob[8:12])[0]
        start = 12
    header = ast.literal_eval(blob[start:start + header_length].decode("latin1"))
    data = blob[start + header_length:]
    kinds = {"<f8": "d", "|u1": "B", "|i1": "b", "<f4": "f"}
    code = kinds[header["descr"]]
    shape = header["shape"]
    count = 1
    for n in shape:
        count *= n
    values = list(struct.unpack("<%d%s" % (count, code), data[:count * struct.calcsize(code)]))
    if len(shape) == 2 and header["fortran_order"]:
        rows, cols = shape
        values = [values[c * rows + r] for r in range(rows) for c in range(cols)]
    return shape, values


def read_ply_points(path):
    data = open(path, "rb").read()
    body = data.index(b"end_header\n") + 11
    count = int(data[:body].split(b"element vertex ")[1].split()[0])
    return count, [struct.unpack_from("<6f4B", data, body + 28 * i) for i in range(count)]


def nearest_lut(palette, bank):
    entries = [i for i in range(bank * 16, bank * 16 + 16) if i not in (0, 255)]
    lums = {i: sum(w * v for w, v in zip(LUM, palette[i])) for i in entries}
    lut = bytearray(LUT_SIZE)
    for step in range(LUT_SIZE):
        lum = step / 4.0
        lut[step] = min(entries, key=lambda i: (abs(lums[i] - lum), i))
    return lut


def nearest_index(palette, colour):
    return min((i for i in range(256) if i not in (0, 255)),
               key=lambda i: (sum((a - b) ** 2 for a, b in zip(palette[i], colour)), i))


def convert(npz_path, palette, luts):
    arrays = {}
    with zipfile.ZipFile(npz_path) as archive:
        for name in archive.namelist():
            arrays[name[:-4]] = read_npy(archive.read(name))
    count = arrays["bank"][0][0]
    banks = arrays["bank"][1]
    albedo = arrays["albedo"][1]
    colours = arrays["col"][1]
    slope = arrays["b"][1]
    reference = arrays["c"][1][0]

    # The sidecar is indexed by PLY vertex, so the two must agree point for point.
    ply_count, ply = read_ply_points(npz_path[:-4] + ".ply")
    if ply_count != count:
        raise ValueError("point count differs from the PLY")
    for i in (0, count // 3, count // 2, count - 1):
        for axis in range(3):
            if abs(ply[i][axis] - arrays["pts"][1][i * 3 + axis]) > 1e-3:
                raise ValueError("point order differs from the PLY")

    fixed = bytearray(count)
    wrong = 0
    for i in range(count):
        colour = tuple(colours[i * 3:i * 3 + 3])
        fixed[i] = nearest_index(palette, colour)
        if banks[i] >= 0:
            index = luts[banks[i]][min(LUT_SIZE - 1, max(0, int(albedo[i] * 4 + 0.5)))]
            if palette[index] != colour:
                wrong += 1

    out = bytearray(b"S3DL")
    out += struct.pack("<II", 1, count)
    out += struct.pack("<f", reference)
    out += struct.pack("<16f", *slope)
    for lut in luts:
        out += lut
    out += struct.pack("<%db" % count, *banks)
    out += fixed
    out += struct.pack("<%df" % count, *albedo)
    with open(npz_path[:-4] + ".light", "wb") as f:
        f.write(out)
    return count, wrong


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "ship3d"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    palette_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        root, "data", "full", "GAMEDAT", "GAME.PAL")
    palette = read_palette(palette_path)
    luts = [nearest_lut(palette, bank) for bank in range(16)]
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".npz"):
            continue
        count, wrong = convert(os.path.join(directory, name), palette, luts)
        print("%s: %d points, %d stored colours not reproduced by the index table"
              % (name, count, wrong))


if __name__ == "__main__":
    main()
