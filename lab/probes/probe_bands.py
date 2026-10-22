import struct, collections, sys

def read_bmp(path):
    d = open(path, "rb").read()
    print(f"read {len(d)} bytes from {path}")
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<II", d[18:26])
    rows = []
    for y in range(h):
        rows.append(d[off + y * w * 3: off + (y + 1) * w * 3])
    return w, h, rows

def lum(row, x):
    return 0.2126 * row[3 * x] + 0.7152 * row[3 * x + 1] + 0.0722 * row[3 * x + 2]

def covered(rows, y, thresh):
    return [x for x in range(len(rows[y]) // 3) if lum(rows[y], x) > thresh]

def band_width(rows, ys, thresh):
    best = 0
    for y in ys:
        xs = covered(rows, y, thresh)
        if xs:
            best = max(best, max(xs) - min(xs) + 1)
    return best

def main():
    path = sys.argv[1]
    w, h, rows = read_bmp(path)
    corner = max(lum(rows[0], 0), lum(rows[0], w - 1), lum(rows[h - 1], 0), lum(rows[h - 1], w - 1))
    thresh = int(sys.argv[2]) if len(sys.argv) > 2 else corner + 30
    print(f"corner luminance {corner:.1f}, threshold {thresh}")

    pal = collections.Counter()
    for y in range(0, h, 5):
        for x in range(0, w, 5):
            pal[(rows[y][3 * x], rows[y][3 * x + 1], rows[y][3 * x + 2])] += 1
    print("palette:", pal.most_common(6))
    print(f"mean luminance {sum(lum(r, x) for y, r in enumerate(rows) for x in range(w)) / (w * h):.1f}")

    first = next(y for y in range(h) if covered(rows, y, thresh))
    last = next(y for y in range(h - 1, -1, -1) if covered(rows, y, thresh))
    print(f"silhouette rows {first}..{last} ({last - first + 1} px tall)")

    for band in (8, 16, 32):
        top = band_width(rows, range(first, first + band), thresh)
        bot = band_width(rows, range(last - band + 1, last + 1), thresh)
        print(f"band {band}: top width {top}, bottom width {bot}, base/top {bot / top:.2f}")

main()
