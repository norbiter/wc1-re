#!/usr/bin/env python3
"""Convert the point-cloud relighting data into the sidecars the SDL port reads.

For every ship3d/<NAME>.npz this writes ship3d/<NAME>.light next to <NAME>.ply.
Only the Python standard library is needed.  See ship3d/README.md, "Relighting".

    python3 bin/ship3dLight.py [ship3d-directory] [GAME.PAL]

Two colour models are accepted, told apart by the fields of the .npz:

* quantised RGB (exports since 2026-11-08: albedo_rgb, kappa, c, used):
      rgb = albedo_rgb * max(1 + kappa * (n.L - c), 0.05)
  drawn with the nearest palette entry (squared RGB distance) among `used`;
* hue bank (earlier exports: bank, albedo, b, c):
      lum = albedo + b[bank] * (n.L - c)
  drawn with the palette entry of bank `bank` nearest in luminance.

Under either model a point's colour depends only on n.L, clipped to 0..1, so
the sidecar stores each point's palette index at STEPS evenly spaced values of
n.L and the renderer needs no knowledge of the model.  The sidecar (all
little-endian) holds:

    char[4]   "S3DL"
    uint32    version (2), uint32 point count, uint32 steps
    uint8     shade[count][steps]  palette index at n.L = step / (steps - 1)

Points no sprite saw keep their stored grey under any light.  Because the
renderer emits palette indices rather than RGB, the game's own palette fades
act on the ships unchanged.
"""
import ast
import os
import struct
import sys
import zipfile

LUM = (0.30, 0.59, 0.11)
LUT_SIZE = 1024  # quarter-unit brightness steps, 0..255.75 (hue-bank model)
STEPS = 64


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


def shade_banks(arrays, count, palette, fixed, stored):
    """Shade tables for the hue-bank model."""
    luts = [nearest_lut(palette, bank) for bank in range(16)]
    banks = arrays["bank"][1]
    albedo = arrays["albedo"][1]
    slope = arrays["b"][1]
    reference = arrays["c"][1][0]
    shade = bytearray()
    wrong = 0
    for i in range(count):
        bank = banks[i]
        if bank < 0 or bank >= 16:
            shade += bytes([fixed[i]]) * STEPS
            continue
        index = luts[bank][min(LUT_SIZE - 1, max(0, int(albedo[i] * 4 + 0.5)))]
        if palette[index] != stored[i]:
            wrong += 1
        for step in range(STEPS):
            lum = albedo[i] + slope[bank] * (step / (STEPS - 1.0) - reference)
            shade.append(luts[bank][min(LUT_SIZE - 1, max(0, int(lum * 4 + 0.5)))])
    return shade, wrong


def nearest_along_ray(albedo, scales, entries, norms):
    """Nearest entry to albedo * s for each s of the non-decreasing list scales.

    |sA - P|^2 = |sA|^2 + |P|^2 - 2 s A.P, and the first term is the same for
    every entry, so each entry is the line |P|^2 - 2 s (A.P) in s and the
    answer is the lower envelope of those lines.  Walking it from the first s
    only needs one scan of the entries per switch of colour."""
    ar, ag, ab = albedo
    dots = [ar * r + ag * g + ab * b for r, g, b in entries]
    s = scales[0]
    best = min(range(len(entries)), key=lambda j: (norms[j] - 2.0 * s * dots[j], j))
    out = []
    at = 0
    while True:
        # The next line to cross below `best` is a steeper one (larger A.P).
        switch = None
        nxt = best
        for j in range(len(entries)):
            delta = dots[j] - dots[best]
            if delta <= 0.0:
                continue
            cross = (norms[j] - norms[best]) / (2.0 * delta)
            if switch is None or cross < switch or (cross == switch and j < nxt):
                switch = cross
                nxt = j
        while at < len(scales) and (switch is None or scales[at] <= switch):
            out.append(best)
            at += 1
        if at == len(scales):
            return out
        best = nxt


def shade_rgb(arrays, count, palette, fixed, stored):
    """Shade tables for the quantised-RGB model."""
    albedo = arrays["albedo_rgb"][1]
    kappa = arrays["kappa"][1][0]
    reference = arrays["c"][1][0]
    used = arrays["used"][1]
    entries = [tuple(float(v) for v in palette[i]) for i in used]
    norms = [r * r + g * g + b * b for r, g, b in entries]
    scales = [max(1.0 + kappa * (step / (STEPS - 1.0) - reference), 0.05)
              for step in range(STEPS)]
    if any(b < a for a, b in zip(scales, scales[1:])):
        raise ValueError("kappa is negative")
    shade = bytearray()
    wrong = 0
    for i in range(count):
        colour = albedo[i * 3:i * 3 + 3]
        if colour[0] < 0:
            shade += bytes([fixed[i]]) * STEPS
            continue
        for j in nearest_along_ray(colour, scales, entries, norms):
            shade.append(used[j])
        unlit = used[nearest_along_ray(colour, [1.0], entries, norms)[0]]
        if palette[unlit] != stored[i]:
            wrong += 1
    return shade, wrong


def convert(npz_path, palette):
    arrays = {}
    with zipfile.ZipFile(npz_path) as archive:
        for name in archive.namelist():
            arrays[name[:-4]] = read_npy(archive.read(name))
    count = arrays["pts"][0][0]
    colours = arrays["col"][1]

    # The sidecar is indexed by PLY vertex, so the two must agree point for point.
    ply_count, ply = read_ply_points(npz_path[:-4] + ".ply")
    if ply_count != count:
        raise ValueError("point count differs from the PLY")
    for i in (0, count // 3, count // 2, count - 1):
        for axis in range(3):
            if abs(ply[i][axis] - arrays["pts"][1][i * 3 + axis]) > 1e-3:
                raise ValueError("point order differs from the PLY")

    # At n.L = c the model reproduces the stored (PLY) colour; `wrong`
    # counts the points where it does not, which should be none.
    stored = [tuple(colours[i * 3:i * 3 + 3]) for i in range(count)]
    fixed = bytearray(nearest_index(palette, colour) for colour in stored)
    if "albedo_rgb" in arrays:
        model = "quantised RGB"
        shade, wrong = shade_rgb(arrays, count, palette, fixed, stored)
    else:
        model = "hue bank"
        shade, wrong = shade_banks(arrays, count, palette, fixed, stored)

    out = bytearray(b"S3DL")
    out += struct.pack("<III", 2, count, STEPS)
    out += shade
    with open(npz_path[:-4] + ".light", "wb") as f:
        f.write(out)
    return model, count, wrong


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "ship3d"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    palette_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        root, "data", "full", "GAMEDAT", "GAME.PAL")
    palette = read_palette(palette_path)
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".npz"):
            continue
        model, count, wrong = convert(os.path.join(directory, name), palette)
        print("%s: %s model, %d points, %d stored colours not reproduced at n.L = c"
              % (name, model, count, wrong))


if __name__ == "__main__":
    main()
