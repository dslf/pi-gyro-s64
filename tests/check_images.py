#!/usr/bin/env python3
"""Checks the renders produced by viewer.exe --render.

  check_images.py image <file.bmp> [cube|teapot]   one render: framing, shading, silhouette
  check_images.py projection <ortho> <f15> <f45> <f80>  same cube, fov must foreshorten
  check_images.py framing <file.bmp>               extreme aspect: nothing cropped

Exit 0 = all good, 1 = a check failed, 2 = bad usage / unreadable file.
"""
import sys

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("FAIL need pillow and numpy: python -m pip install pillow numpy")
    sys.exit(2)

BG = (128, 133, 143)          # gamma-encoded clear colour (see BG_COLOR in src/render.c)
fails = []


def fail(msg):
    fails.append(msg)
    print(f"FAIL {msg}")


def ok(msg):
    print(f"ok   {msg}")


def load(path):
    try:
        im = Image.open(path)
    except Exception as exc:
        fail(f"cannot open {path}: {exc}")
        return None
    if im.mode != "RGB":
        fail(f"{path}: mode {im.mode} != RGB")
        return None
    return np.asarray(im).astype(int)


def silhouette(a):
    """Boolean mask of non-background pixels cropped to its bounding box."""
    bg = np.abs(a - np.array(BG)).sum(axis=2) > 12
    rows = np.where(bg.any(axis=1))[0]
    cols = np.where(bg.any(axis=0))[0]
    if rows.size == 0 or cols.size == 0:
        return None, None
    return bg, (rows, cols)


def coverage(a):
    bg, bbox = silhouette(a)
    if bg is None:
        fail("image is entirely background (nothing was rendered)")
        return 0.0
    return bg.mean()


def inside_frame(a):
    bg, (rows, cols) = silhouette(a)
    h, w = a.shape[:2]
    touch = rows[0] == 0 or rows[-1] == h - 1 or cols[0] == 0 or cols[-1] == w - 1
    if touch:
        fail(f"silhouette touches the frame edge ({cols[-1] - cols[0] + 1}x{rows[-1] - rows[0] + 1} in {w}x{h})")
    else:
        ok("silhouette inside the frame")


def extent_at(a, frac):
    """Width of the silhouette row at `frac` of its height (0 = top)."""
    bg, (rows, cols) = silhouette(a)
    sub = bg[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]
    i = int(frac * (sub.shape[0] - 1))
    row = sub[i]
    if not row.any():
        return 0
    nz = row.nonzero()[0]
    return int(nz[-1] - nz[0] + 1)


def lid_base_ratio(a):
    """Mean width of the top band over the mean width of the bottom band."""
    bg, (rows, cols) = silhouette(a)
    sub = bg[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]
    h = sub.shape[0]
    k = max(1, h // 8)

    def band(b):
        rows_idx = np.where(b.any(axis=1))[0]
        if rows_idx.size == 0:
            return 0.0
        return float(np.mean([int(r.nonzero()[0][-1] - r.nonzero()[0][0] + 1) for r in b]))

    lid = band(sub[:k])
    base = band(sub[-k:])
    if lid <= 0 or base <= 0:
        fail("empty lid or base band in silhouette")
        return 0.0
    return base / lid


def shading(a, min_cov=0.05):
    lum = np.asarray(Image.fromarray(a.astype("uint8")).convert("L")).astype(int)
    hi = lum.max()
    lo = lum.min()
    if hi < 200:
        fail(f"no highlights (max luminance {hi})")
    elif lo > 160:
        fail(f"no shadowed side (min luminance {lo})")
    elif hi == 255 and lo == 0 and min_cov:
        fail(f"clipped highlights/blacks (max {hi}, min {lo})")
    else:
        ok(f"shading from {lo} to {hi}")


def check_image(path, kind):
    a = load(path)
    if a is None:
        return
    h, w = a.shape[:2]
    ok(f"{path}: size {w}x{h}")
    cov = coverage(a)
    inside_frame(a)
    bg, (rows, cols) = silhouette(a)
    sil_w = cols[-1] - cols[0] + 1
    sil_h = rows[-1] - rows[0] + 1
    aspect = sil_w / sil_h
    if kind == "cube":
        if cov < 0.25:
            fail(f"cube coverage {cov:.1%} < 25%")
        else:
            ok(f"cube coverage {cov:.1%}")
        if not (0.6 < aspect < 1.5):
            fail(f"cube silhouette aspect {aspect:.2f} not near square")
        else:
            ok(f"cube silhouette aspect {aspect:.2f}")
    elif kind == "teapot":
        if cov < 0.05:
            fail(f"teapot coverage {cov:.1%} < 5%")
        else:
            ok(f"teapot coverage {cov:.1%}")
        if not (1.2 < aspect < 2.4):
            fail(f"teapot silhouette aspect {aspect:.2f} outside (1.2, 2.4) -> not upright")
        else:
            ok(f"teapot silhouette aspect {aspect:.2f}")
        lid, belly = extent_at(a, 0.15), extent_at(a, 0.5)
        if lid >= belly * 0.8:
            fail(f"teapot lid (w {lid}) not narrower than belly (w {belly})")
        else:
            ok(f"teapot narrower near the lid ({lid} < {belly})")
    shading(a)


def check_framing(path):
    """Very wide / very tall windows must still show the whole object."""
    a = load(path)
    if a is None:
        return
    h, w = a.shape[:2]
    ok(f"{path}: size {w}x{h}")
    cov = coverage(a)
    if cov < 0.02:
        fail(f"coverage {cov:.1%} < 2% at extreme aspect")
    else:
        ok(f"coverage {cov:.1%}")
    inside_frame(a)


def check_projection(ortho_path, narrow_path, mid_path, wide_path):
    """Parallel rays keep proportions; perspective foreshortening grows with fov.

    Same cube, same camera angles, four projections. The silhouette's base/top band
    width ratio is the measurement: orthographic gives the reference value (depth
    does not scale), and each wider fov must deviate from it further. Deviation is
    taken in absolute terms so the check does not care which end of the cube faces
    the camera. That ordering cannot come out of a transposed view matrix or a
    flipped depth sign, and it does not depend on the model's own proportions (a
    teapot is wider at the base than at the lid for reasons that have nothing to do
    with projection).
    """
    paths = (ortho_path, narrow_path, mid_path, wide_path)
    imgs = [(p, load(p)) for p in paths]
    if any(a is None for _, a in imgs):
        return
    ratios = [lid_base_ratio(a) for _, a in imgs]
    if any(r <= 0 for r in ratios):
        return
    ro, r15, r45, r80 = ratios
    covs = [coverage(a) for _, a in imgs]
    for p, c in zip(paths, covs):
        if c < 0.03:
            fail(f"{p}: coverage {c:.1%} < 3%")
        else:
            ok(f"{p}: coverage {c:.1%}")
    for _, a in imgs:
        inside_frame(a)

    d15, d45, d80 = abs(r15 - ro), abs(r45 - ro), abs(r80 - ro)
    if d80 > 0.3:
        ok(f"wide fov deviates from ortho by {d80:.2f} (> 0.3): visible foreshortening")
    else:
        fail(f"fov 80 ratio {r80:.2f} deviates from ortho {ro:.2f} by only {d80:.2f}")

    if d15 < d45 < d80:
        ok(f"deviation from ortho grows with fov: {d15:.2f} < {d45:.2f} < {d80:.2f}")
    else:
        fail(f"deviation from ortho not increasing: {d15:.2f}, {d45:.2f}, {d80:.2f}")

    if d15 > 0.05:
        ok(f"even fov 15 deviates from ortho ({d15:.2f}): perspective is not faked by a huge eye distance")
    else:
        fail(f"fov 15 ratio {r15:.2f} equals ortho {ro:.2f} - perspective indistinguishable from ortho")

def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    mode = argv[1]
    if mode == "image":
        if len(argv) < 3:
            return 2
        check_image(argv[2], argv[3] if len(argv) > 3 else "cube")
    elif mode == "framing":
        if len(argv) < 3:
            return 2
        check_framing(argv[2])
    elif mode == "projection":
        if len(argv) < 6:
            print("usage: check_images.py projection <ortho> <f15> <f45> <f80>")
            return 2
        check_projection(argv[2], argv[3], argv[4], argv[5])
    else:
        print(f"unknown mode '{mode}'")
        return 2
    print("checks: PASS" if not fails else f"checks: {len(fails)} FAILED")
    return 0 if not fails else 1


sys.exit(main(sys.argv))
