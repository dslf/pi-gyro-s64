import numpy as np, math
from PIL import Image

BG = (128, 133, 143)

def load_mesh(path):
    P, tris = [], []
    for l in open(path):
        if l.startswith('v '):
            q = l.split(); P.append([float(q[1]), float(q[2]), float(q[3])])
        elif l.startswith('f '):
            idx = [int(t.split('/')[0]) - 1 for t in l.split()[1:]]
            for i in range(1, len(idx) - 1): tris.append((idx[0], idx[i], idx[i + 1]))
    P = np.array(P)
    mn, mx = P.min(axis=0), P.max(axis=0)
    ext = mx - mn
    asym = np.abs(mn) / np.maximum(ext, 1e-6)
    resting = [k for k in range(3) if asym[k] < 0.15 and ext[k] > 1e-6]
    up = resting[-1] + 1 if resting else 1
    scale = 1.0 / ext.max()
    Q = (P - (mn + mx) / 2) * scale
    if up == 3: Q = np.stack([Q[:, 0], Q[:, 2], -Q[:, 1]], axis=1)
    elif up == 1: Q = np.stack([Q[:, 2], Q[:, 0], Q[:, 1]], axis=1)
    Q -= (Q.min(axis=0) + Q.max(axis=0)) / 2
    fit = np.sqrt((Q * Q).sum(axis=1)).max()
    return Q, fit, np.array(tris)

def mul(a, b):                       # mirrors m4_mul: m[col*4+row] = sum_k a[col*4+k]*b[k*4+row]
    c = np.zeros(16)
    for col in range(4):
        for row in range(4):
            c[col * 4 + row] = sum(a[col * 4 + k] * b[k * 4 + row] for k in range(4))
    return c

def tp(m, p):                        # mirrors m4_transform_point
    return np.array([sum(p[i] * m[i * 4 + j] for i in range(4)) for j in range(4)])

def view_flat(yaw, pitch, dist):
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    eye = np.array([dist * cp * sy, dist * sp, dist * cp * cy], float)
    fwd = -eye / np.linalg.norm(eye)
    upv = np.array([0.0, 1.0, 0.0])
    x = np.cross(fwd, upv); x /= np.linalg.norm(x)
    y = np.cross(x, fwd)
    z = -fwd
    V = np.zeros(16)
    for row, b in enumerate((x, y, z)):
        for col in range(3): V[col * 4 + row] = b[col]
        V[3 * 4 + row] = -b @ eye
    V[15] = 1.0
    return eye, V

def proj_flat(fov, aspect, ortho, fit, dist):
    zn, zf = fit * 0.02, fit * 20.0
    M = np.zeros(16); M[15] = 1.0
    if ortho:
        half = min(fit, dist) * math.tan(math.radians(fov) / 2) * aspect
        M[0] = 1 / half; M[5] = 1 / (half / aspect)
        M[10] = -2 / (zf - zn); M[14] = -(zf + zn) / (zf - zn)
    else:
        f = 1 / math.tan(math.radians(fov) / 2)
        M[0] = f / aspect; M[5] = f; M[11] = -1
        M[10] = -(zf + zn) / (zf - zn); M[14] = -2 * zf * zn / (zf - zn)
    return M

def edge(p, q, gx, gy):
    return (q[0] - p[0]) * (gy - p[1]) - (q[1] - p[1]) * (gx - p[0])

def raster(Q, tris, VP, W, H):
    pts4 = np.array([tp(VP, [q[0], q[1], q[2], 1.0]) for q in Q])
    mask = np.zeros((H, W), bool)
    for t in tris:
        pts = []
        ok = True
        for v in (pts4[t[0]], pts4[t[1]], pts4[t[2]]):
            w = abs(v[3])          # 1 for orthographic, eye depth for perspective
            if w <= 0: ok = False; break
            pts.append(((v[0] / w + 1) / 2 * W, (1 - v[1] / w) / 2 * H, v[2] / w))
        if not ok: continue
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        x0, x1 = int(max(0, math.floor(min(xs)) - 1)), int(min(W - 1, math.ceil(max(xs)) + 1))
        y0, y1 = int(max(0, math.floor(min(ys)) - 1)), int(min(H - 1, math.ceil(max(ys)) + 1))
        if x1 <= x0 or y1 <= y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        s1 = edge(pts[0], pts[1], gx, gy); s2 = edge(pts[1], pts[2], gx, gy); s3 = edge(pts[2], pts[0], gx, gy)
        inside = ((s1 >= 0) & (s2 >= 0) & (s3 >= 0)) | ((s1 <= 0) & (s2 <= 0) & (s3 <= 0))
        mask[y0:y1 + 1, x0:x1 + 1] |= inside
    return mask

def bands(mask):
    rows = np.where(mask.any(axis=1))[0]; cols = np.where(mask.any(axis=0))[0]
    if rows.size == 0: return 0, 0
    sub = mask[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]
    k = max(1, sub.shape[0] // 8)
    w = lambda b: np.mean([int(r.nonzero()[0][-1] - r.nonzero()[0][0] + 1) for r in b if r.any()])
    return w(sub[:k]), w(sub[-k:])

Q, fit, tris = load_mesh('utah_teapot.obj')
print('fit %.3f up-remapped span %s' % (fit, Q.max(axis=0) - Q.min(axis=0)))
for name, ortho, fov in (('ortho60', True, 60), ('persp60', False, 60), ('persp80', False, 80)):
    dist = fit / math.sin(math.radians(fov) / 2) / (0.92 if ortho else 1.08)
    eye, V = view_flat(0.6, 0.25, dist)
    VP = mul(proj_flat(fov, 400 / 300, ortho, fit, dist), V)
    m = raster(Q, tris, VP, 400, 300)
    lid, base = bands(m)
    print('%-8s lid %5.1f base %5.1f ratio %.2f coverage %.1f%%' % (name, lid, base, base / lid, m.mean() * 100))

for p in ('out_teapot_ortho.bmp', 'out_teapot.bmp', 'out_teapot_fov80.bmp'):
    a = np.asarray(Image.open(p)).astype(int)
    m = (np.abs(a - np.array(BG)).sum(axis=2) > 8)
    lid, base = bands(m)
    print('%-22s lid %5.1f base %5.1f ratio %.2f coverage %.1f%%' % (p, lid, base, base / lid, m.mean() * 100))
