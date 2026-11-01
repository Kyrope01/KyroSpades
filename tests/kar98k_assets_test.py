"""Structural checks for the replacement Kar-98K skin (standard library only)."""
from pathlib import Path
import struct
import wave
import zlib
import zipfile

root = Path(__file__).resolve().parents[1]
base = root / 'resources.override'
name = 'Kar-98K'
data = (base / 'kv6' / f'semi({name}).kv6').read_bytes()
magic, xs, ys, zs, xp, yp, zp, n = struct.unpack_from('<4sIIIfffI', data)
assert (magic, xs, ys, zs, xp, yp, zp) == (b'Kvxl', 27, 144, 48, 12., 78.75, 29.25)
assert 3000 < n < 15000
index = 32 + n * 8
xcounts = struct.unpack_from(f'<{xs}I', data, index)
index += xs * 4
counts = struct.unpack_from(f'<{xs * ys}H', data, index)
assert index + xs * ys * 2 == len(data)
assert sum(xcounts) == sum(counts) == n
assert all(sum(counts[x * ys:(x + 1) * ys]) == xcounts[x] for x in range(xs))
offset = 0
for count in counts:
    previous_z = -1
    for _ in range(count):
        b, g, r, _, z, faces, normal = struct.unpack_from('<BBBBHBB', data, 32 + 8 * offset)
        assert z < zs and z > previous_z and faces and faces < 64
        assert (r, g, b) != (0, 0, 0)  # black is team-colored by the renderer
        previous_z = z
        offset += 1
png = (base / 'png' / f'semi({name}).png').read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n'
width, height, depth, colour = struct.unpack_from('>IIBB', png, 16)
assert (width, height, depth, colour) == (1024, 768, 8, 6)  # RGBA

# Decode RGBA scanlines with the standard library to check ADS alignment and
# transparent side strips (which the HUD stretches on widescreen displays).
pos, compressed = 8, bytearray()
while pos < len(png):
    size = struct.unpack_from('>I', png, pos)[0]
    kind = png[pos + 4:pos + 8]
    if kind == b'IDAT': compressed.extend(png[pos + 8:pos + 8 + size])
    pos += 12 + size
raw = zlib.decompress(compressed)
stride = width * 4
rows, prev = [], bytearray(stride)
for y in range(height):
    start = y * (stride + 1)
    filt = raw[start]
    row = bytearray(raw[start + 1:start + 1 + stride])
    for i in range(stride):
        left = row[i - 4] if i >= 4 else 0
        above = prev[i]
        corner = prev[i - 4] if i >= 4 else 0
        if filt == 1: predictor = left
        elif filt == 2: predictor = above
        elif filt == 3: predictor = (left + above) // 2
        elif filt == 4:
            p = left + above - corner
            pa, pb, pc = abs(p-left), abs(p-above), abs(p-corner)
            predictor = left if pa <= pb and pa <= pc else (above if pb <= pc else corner)
        else:
            assert filt == 0
            predictor = 0
        row[i] = (row[i] + predictor) & 255
    rows.append(row)
    prev = row
assert all(row[3] == 0 and row[-1] == 0 for row in rows)
assert rows[height // 2][(width // 2) * 4 + 3] == 255  # front blade tip
assert rows[378][512 * 4 + 3] == 0  # hollow opening above the post
assert rows[390][510 * 4 + 3] == 0  # hollow opening beside the post
assert rows[383][512 * 4 + 3] == 0  # the post starts at the aim point
assert rows[378][507 * 4 + 3] == 255  # rectangular frame edge
# Both upright edges are equally solid, even when the image is downscaled.
for y in range(374, 396):
    assert all(rows[y][x * 4 + 3] == 255 for x in (507, 508, 516, 517))
    assert rows[y][507 * 4:509 * 4] == rows[y][516 * 4:518 * 4]
assert rows[height // 2 - 90][(width // 2) * 4 + 3] == 0  # clear view above
assert rows[344][512 * 4 + 3] == 0  # clear view above the sight (no circular hood)
assert rows[410][475 * 4 + 3] == rows[410][549 * 4 + 3] == 255  # squared ears
# Post is a solid single-pixel line, not an outlined miniature rectangle.
assert all(rows[y][512 * 4 + 3] == 255 for y in range(384, 395))
assert all(rows[y][511 * 4 + 3] == 0 and rows[y][513 * 4 + 3] == 0
           for y in range(384, 393))
# Guard silhouettes are 20 pixels wide and start at y=402.
assert all(rows[410][x * 4 + 3] == 255 for x in range(467,487))
assert all(rows[410][x * 4 + 3] == 255 for x in range(538,558))
assert rows[400][470 * 4 + 3] == 0 and rows[400][554 * 4 + 3] == 0
# No crosspiece protrudes beyond either guard in the sight-base band.
assert all(rows[y][x * 4 + 3] == 0 for y in range(420,453)
           for x in list(range(450,467)) + list(range(558,575)))
assert rows[600][350 * 4 + 3] == 255 and rows[600][674 * 4 + 3] == 0  # bolt on left
with wave.open(str(base / 'wav' / f'semishoot({name}).wav')) as wav:
    assert wav.getnchannels() in (1, 2)
    assert wav.getsampwidth() == 2 and wav.getframerate() >= 22050
    assert wav.getnframes() > wav.getframerate() // 4
with zipfile.ZipFile(root / 'bsresources.zip') as archive:
    assert data != archive.read('kv6/semi(Kar-98K).kv6')
    assert (base / 'wav' / f'semishoot({name}).wav').read_bytes() != archive.read('wav/semishoot(Kar-98K).wav')
skins = (root / 'src/skins.c').read_text()
assert '"semi", 0.05F' in skins
assert 'kv6/%s(%s).kv6' in skins
print('Kar-98K asset structure and skin integration checks passed')
