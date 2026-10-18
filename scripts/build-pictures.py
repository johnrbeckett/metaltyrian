#!/usr/bin/env python3
"""Prepares the regenerated full-screen pictures (Art/Incoming/screens, from
ChatGPT) for the app: App/Pictures/picNN.png, one per tyrian.pic picture.

The art is used as delivered except where the game has to fit on top of it:
- HUD (3, 6): the painted labels come out (crisp text draws them in
  Orbitron), and in the one-player HUD the slots the game draws its gun-power
  bars and lights into keep the original's empty recess, so the game's
  drawing doesn't land on painted detail (a gem under the power bar).
- 11: the two bars are fitted to the original's exact boxes.
- 13: the grid is warped back onto the original's lines.
Picture 4, the title, is John's own art (App/Title), so it is left out.

Usage: scripts/build-pictures.py  (needs Research/pics, from Tests/linux/dump_hud.c,
and Research/screens-check/live_mask.npy, from a demo run; see HANDOFF.md)."""
import numpy as np, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
SRC = 'Art/Incoming/screens/screens-regenerated/screen-%02d.png'
OUT = 'App/Pictures'
os.makedirs(OUT, exist_ok=True)

def load(n): return np.asarray(Image.open(SRC % n).convert('RGB')).astype(float)
def orig(n): return np.asarray(Image.open('Research/pics/pic%02d_320x200.png' % n).convert('RGB')).astype(float)
def save(n, a): Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save('%s/pic%02d.png' % (OUT, n), optimize=True)

def dil(m, n=1):
    for _ in range(n):
        o = m.copy(); o[1:] |= m[:-1]; o[:-1] |= m[1:]; o[:, 1:] |= m[:, :-1]; o[:, :-1] |= m[:, 1:]; m = o
    return m

def scale2x(a):
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode='edge')
    B = p[:-2, 1:-1]; D = p[1:-1, :-2]; E = a; F = p[1:-1, 2:]; H = p[2:, 1:-1]
    eq = lambda x, y: (x == y).all(2)
    c = (~eq(B, H)) & (~eq(D, F))
    e0 = np.where((c & eq(D, B))[..., None], D, E); e1 = np.where((c & eq(B, F))[..., None], F, E)
    e2 = np.where((c & eq(D, H))[..., None], D, E); e3 = np.where((c & eq(H, F))[..., None], F, E)
    h, w, _ = a.shape; o = np.zeros((h * 2, w * 2, 3), a.dtype)
    o[0::2, 0::2] = e0; o[0::2, 1::2] = e1; o[1::2, 0::2] = e2; o[1::2, 1::2] = e3
    return o

def smooth4(a):
    o = scale2x(scale2x(a))
    k = np.pad(o, ((1, 1), (1, 1), (0, 0)), mode='edge')
    return (k[1:-1, 1:-1] * 4 + k[:-2, 1:-1] + k[2:, 1:-1] + k[1:-1, :-2] + k[1:-1, 2:]) / 8

def remove_labels(hd, rects):
    """Paints out the green lettering inside each label box (game pixels)."""
    r, g, b = hd[..., 0], hd[..., 1], hd[..., 2]
    core = (g > r + 12) & (g > b + 8) & (g > 30)
    bronze = (r > g + 4) & (hd.sum(2) > 90)
    out = hd.copy()
    H, W = hd.shape[:2]
    for x0, y0, x1, y1 in rects:
        box = np.zeros((H, W), bool); box[y0 * 4 - 4:y1 * 4 + 4, x0 * 4 - 4:x1 * 4 + 4] = True
        m = dil(core & box, 4) & box & ~bronze
        bg = box & ~m & (hd.sum(2) < 60)
        out[m] = np.median(hd[bg], axis=0) if bg.any() else 0
    return out

# Pictures used as delivered.
for n in (1, 2, 5, 7, 8, 9, 10, 12):
    save(n, load(n))

# One-player HUD.
labels3 = [(284, 5, 310, 10), (284, 11, 300, 16), (284, 26, 304, 31), (284, 32, 300, 37),
           (283, 54, 304, 59), (284, 138, 291, 174), (295, 138, 302, 168)]
hd = remove_labels(load(3), labels3)
ref = orig(3)
live = np.load('Research/screens-check/live_mask.npy')
live[186:198, 10:260] = False           # the message strip: the game counts it as picture
small = hd.reshape(200, 4, 320, 4, 3).mean((1, 3))
diff = np.abs(small - ref).sum(2)
dark = ref.sum(2) < 90
red = (ref[..., 0] > 100) & (ref[..., 1] < 60)
patch = live & (diff > 90)
for _ in range(3):
    patch |= dil(patch) & dark & (diff > 60)
for y0, y1 in ((16, 21), (37, 42)):     # gun-power bars: always drawn, keep the original slot
    box = np.zeros_like(patch); box[y0:y1, 283:302] = True
    patch |= box & (dark | red)
P = np.kron(patch, np.ones((4, 4))).astype(bool)
hd[P] = smooth4(ref)[P]
save(3, hd)
print('pic03: patched', int(patch.sum()), 'game pixels')

# Two-player HUD.
labels6 = [(284, 17, 291, 52), (295, 17, 302, 46), (284, 151, 291, 186), (295, 151, 302, 180)]
save(6, remove_labels(load(6), labels6))

# 11: both bars into the original's boxes.
hd = load(11)
out = np.zeros_like(hd)
for (sx0, sx1), (dx0, dx1) in (((9, 586), (8, 584)), ((692, 1269), (688, 1264))):
    crop = Image.fromarray(hd[0:50, sx0:sx1].astype(np.uint8)).resize((dx1 - dx0, 48), Image.LANCZOS)
    out[0:48, dx0:dx1] = np.asarray(crop)
save(11, out)

# 13: grid lines back onto the original's (HD centre of game line c is 4c + 1.5).
hd = load(13)
nx = [0] + [49, 85, 121, 158, 194, 230, 265, 301, 337, 372, 408, 444, 478, 515, 550, 585, 621, 657, 691,
            727, 763, 798, 834, 870, 905, 941, 977, 1011, 1047, 1083, 1118, 1154, 1190, 1225] + [1279]
ox = [0] + [4 * c + 1.5 for c in range(12, 310, 9)] + [1279]
ny = [0, 33, 65, 98, 130, 162, 195, 228, 261, 293, 326, 359, 391, 424, 456, 489, 522.5, 530.5]
oy = [0] + [4 * r + 1.5 for r in range(8, 129, 8)] + [529.5]
assert len(nx) == len(ox) and len(ny) == len(oy)
top = 530
X = np.interp(np.arange(1280), ox, nx)
Y = np.interp(np.arange(top), oy, ny)
x0 = np.clip(np.floor(X).astype(int), 0, 1278); fx = (X - x0)[None, :, None]
y0 = np.clip(np.floor(Y).astype(int), 0, 798); fy = (Y - y0)[:, None, None]
a = hd[y0][:, x0]; b = hd[y0][:, x0 + 1]; c = hd[y0 + 1][:, x0]; d = hd[y0 + 1][:, x0 + 1]
out = hd.copy()
out[:top] = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
save(13, out)
print('done:', sorted(os.listdir(OUT)))
