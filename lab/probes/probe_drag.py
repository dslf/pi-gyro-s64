"""Drag-direction check on real GL renders: does the silhouette move the way the
mouse moved? Compares foreground centroid x/y between two --yaw / --pitch shots."""
import struct, sys

def fg_centroid(path, thresh=None):
    d = open(path, "rb").read()
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<ii", d[18:26])
    rows = []
    for y in range(h):
        row = d[off + (h - 1 - y) * w * 3:off + (h - 1 - y) * w * 3 + w * 3]
        rows.append(row)
    corner = max((rows[0][0] + rows[0][1] + rows[0][2]),
                 (rows[0][w * 3 - 3] + rows[0][w * 3 - 2] + rows[0][w * 3 - 1])) / 3.0
    t = thresh if thresh is not None else corner + 30
    sx = sy = n = 0
    ymin = ymax = None
    for y in range(h):
        row = rows[y]
        for x in range(w):
            i = x * 3
            lum = (row[i] + row[i + 1] + row[i + 2]) / 3.0
            if lum > t:
                sx += x; sy += y; n += 1
                if ymin is None: ymin = y
                ymax = y
    if n == 0:
        return None
    return (sx / n, sy / n, n, ymax - ymin + 1)

def main():
    a, b, label = sys.argv[1], sys.argv[2], sys.argv[3]
    ca, cb = fg_centroid(a), fg_centroid(b)
    if not ca or not cb:
        print(f"{label}: no foreground")
        return
    print(f"{label}: centroid ({ca[0]:6.1f},{ca[1]:6.1f}) n={ca[2]:5d} h={ca[3]} -> "
          f"({cb[0]:6.1f},{cb[1]:6.1f}) n={cb[2]:5d} h={cb[3]}  shift dx={cb[0]-ca[0]:+.1f} dy={cb[1]-ca[1]:+.1f}")

main()
