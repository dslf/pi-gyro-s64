import struct

d = open('lab/probes/hud_frame.bmp', 'rb').read()
off = struct.unpack_from('<I', d, 10)[0]
w = abs(struct.unpack_from('<i', d, 18)[0])
h = struct.unpack_from('<i', d, 22)[0]
bands = []
for y in range(h):                       # y counted from the TOP of the image
    src = off + (h - 1 - y) * w * 3
    n = sum(1 for x in range(w)
            if abs(d[src + x * 3] - 30) > 30 or abs(d[src + x * 3 + 1] - 32) > 30
            or abs(d[src + x * 3 + 2] - 36) > 30)
    if n:
        if bands and y == bands[-1][1] + 1:
            bands[-1][1] = y
            bands[-1][2] += n
        else:
            bands.append([y, y, n])
print('size', w, 'x', h)
print('bands (top-down y, pixels):', bands)
