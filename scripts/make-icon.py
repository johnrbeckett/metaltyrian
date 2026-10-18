# MetalTyrian app icon: a Talon inside a macOS-style rounded tile with a
# bevelled gold border.
#
# Usage: python3 scripts/make-icon.py SHIP.png App/AppIcon-1024.png [--pixel]
# --pixel: SHIP is original pixel art on black (a ship-screen picture, e.g. the
# silver ship, OPTION_SHAPES 32: Research/fonts/raw/opt_32.ppm, from Tests/linux/dump_hud.c);
# it is cut out exactly and scaled by a whole number with no smoothing.
# Without it, SHIP is a render on near-black (Research/ai-renders/r4.png).
# then:  python3 -c "from PIL import Image; Image.open('App/AppIcon-1024.png').save('App/AppIcon.icns')"
import sys
import numpy as np
from PIL import Image, ImageFilter

S = 1024                     # final size
SS = 2                       # supersampling
N = S * SS
src = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)
PIXEL = '--pixel' in sys.argv[3:]

def superellipse_mask(n, inset, size, k=5.0):
    """macOS-like continuous-corner tile: |x|^k + |y|^k <= 1, soft edge."""
    y, x = np.mgrid[0:n, 0:n].astype(np.float32) + 0.5
    c = n / 2
    h = size / 2
    u = np.abs(x - c) / h
    v = np.abs(y - c) / h
    d = (u ** k + v ** k) ** (1 / k)
    return d

tile = 824 * SS / 1024 * S / S  # Apple grid: 824 of 1024
tile = 824 * SS
d = superellipse_mask(N, 0, tile)
px = 1.0 / (tile / 2)        # one pixel in d units (approx)

def band(lo, hi):
    a = np.clip((d - lo) / px + 0.5, 0, 1)
    b = np.clip((hi - d) / px + 0.5, 0, 1)
    return a * b

outer = np.clip((1.0 - d) / px + 0.5, 0, 1)           # whole tile
gold_w = 36 * SS / (tile / 2)                           # gold border width in d units
inner_edge = 1.0 - gold_w
inner = np.clip((inner_edge - d) / px + 0.5, 0, 1)     # inside the gold

y, x = np.mgrid[0:N, 0:N].astype(np.float32) + 0.5
cx = cy = N / 2

# --- Background: deep space, a soft blue-violet glow behind the ship, faint stars.
r = np.sqrt((x - cx) ** 2 + (y - cy * 0.92) ** 2) / (tile / 2)
bg = np.zeros((N, N, 3), np.float32)
base = np.array([6, 8, 16], np.float32)
glow = np.array([34, 42, 78], np.float32)
t = np.clip(1 - r, 0, 1) ** 1.6
bg[:] = base + (glow - base) * t[..., None]
rng = np.random.default_rng(7)
for _ in range(90):
    sx, sy = rng.uniform(0.12, 0.88) * N, rng.uniform(0.12, 0.88) * N
    br = rng.uniform(60, 200) * (0.5 if rng.random() < 0.7 else 1)
    rad = rng.uniform(0.6, 1.6) * SS
    dd = ((x - sx) ** 2 + (y - sy) ** 2) / (rad ** 2)
    m = np.exp(-dd)
    m[m < 0.01] = 0
    bg += (br * m)[..., None] * np.array([0.85, 0.9, 1.0], np.float32)

# --- Ship: cut from its background, scaled into the tile.
if PIXEL:
    # Background: the black connected to the edges (black inside the ship stays).
    blk = (src.sum(2) == 0)
    h0, w0 = blk.shape
    outside = np.zeros_like(blk)
    stack = [(yy, xx) for yy in range(h0) for xx in (0, w0 - 1)] + [(yy, xx) for xx in range(w0) for yy in (0, h0 - 1)]
    while stack:
        yy, xx = stack.pop()
        if 0 <= yy < h0 and 0 <= xx < w0 and blk[yy, xx] and not outside[yy, xx]:
            outside[yy, xx] = True
            stack += [(yy + 1, xx), (yy - 1, xx), (yy, xx + 1), (yy, xx - 1)]
    alpha = np.where(outside, 0.0, 1.0)
    ship = Image.fromarray(np.dstack([src, alpha * 255]).astype(np.uint8), 'RGBA')
    ship = ship.crop(ship.getbbox())
    # Whole-number scale chosen at the final size, so every game pixel is the same size.
    # As large as fits comfortably inside the gold ring (752 of 824 across).
    k = max(1, int(752 * 0.90 / max(ship.width, ship.height)))
    ship = ship.resize((ship.width * k * SS, ship.height * k * SS), Image.NEAREST)
else:
    bgc = np.array([10.5, 9.5, 14.5], np.float32)
    diff = np.max(np.abs(src - bgc), axis=2)
    alpha = np.clip((diff - 6) / 40, 0, 1)
    ship = Image.fromarray(np.dstack([src, alpha * 255]).astype(np.uint8), 'RGBA')
    ship = ship.crop(ship.getbbox())
    target_h = int(tile * 0.80)
    scale = target_h / ship.height
    ship = ship.resize((int(ship.width * scale), target_h), Image.LANCZOS)
layer = Image.new('RGBA', (N, N), (0, 0, 0, 0))
ox = (N - ship.width) // 2
oy = (N - ship.height) // 2 + int(0.01 * tile)
if PIXEL:
    ox -= ox % SS; oy -= oy % SS   # land on whole final pixels
# Soft shadow beneath the ship for depth.
sh = Image.new('RGBA', (N, N), (0, 0, 0, 0))
sh_alpha = ship.split()[3].point(lambda v: int(v * 0.75))
sh.paste(Image.new('RGBA', ship.size, (0, 0, 0, 255)), (ox + 6 * SS, oy + 14 * SS), sh_alpha)
sh = sh.filter(ImageFilter.GaussianBlur(14 * SS))
layer.paste(ship, (ox, oy), ship)

img = bg.copy()
sha = np.asarray(sh).astype(np.float32)[..., 3:4] / 255
img = img * (1 - sha)
la = np.asarray(layer).astype(np.float32)
a = la[..., 3:4] / 255
img = img * (1 - a) + la[..., :3] * a

# Inner vignette along the gold edge.
vig = np.clip((d - (inner_edge - 0.10)) / 0.10, 0, 1) ** 2
img *= (1 - 0.55 * vig)[..., None]

# --- Gold border: metallic gradient around the ring, with a bevel.
ang = np.arctan2(y - cy, x - cx)
light = 0.5 + 0.5 * np.cos(ang + 2.3)          # light from the top left
ring_t = np.clip((d - inner_edge) / gold_w, 0, 1)   # 0 inner edge .. 1 outer edge
bevel = np.sin(ring_t * np.pi)                 # rounded profile
dark = np.array([96, 64, 16], np.float32)
mid = np.array([196, 150, 52], np.float32)
hi = np.array([255, 228, 140], np.float32)
shade = np.clip(0.10 + 0.75 * light + 0.45 * bevel - 0.40 * (1 - bevel), 0, 1)
gold = np.where(shade[..., None] < 0.5,
                dark + (mid - dark) * (shade[..., None] / 0.5),
                mid + (hi - mid) * ((shade[..., None] - 0.5) / 0.5))
# Fine sheen bands for a brushed look.
gold *= (0.94 + 0.06 * np.cos(ang * 9 + 0.5))[..., None]
# Thin dark lines at both edges of the ring.
edge_line = np.exp(-((ring_t - 0.0) / 0.07) ** 2) + np.exp(-((ring_t - 1.0) / 0.07) ** 2)
gold *= (1 - 0.55 * np.clip(edge_line, 0, 1))[..., None]

ring = outer * (1 - inner)
img = img * (1 - ring[..., None]) + gold * ring[..., None]

out = np.dstack([np.clip(img, 0, 255), outer * 255]).astype(np.uint8)
tile_img = Image.fromarray(out, 'RGBA')

# --- macOS drop shadow under the tile.
canvas = Image.new('RGBA', (N, N), (0, 0, 0, 0))
shadow = Image.fromarray((outer * 255 * 0.5).astype(np.uint8), 'L')
shadow = shadow.filter(ImageFilter.GaussianBlur(10 * SS))
canvas.paste(Image.new('RGBA', (N, N), (0, 0, 0, 255)), (0, 12 * SS), shadow)
canvas = Image.alpha_composite(canvas, tile_img)
canvas = canvas.resize((S, S), Image.BOX if PIXEL else Image.LANCZOS)
canvas.save(sys.argv[2])
print('saved', sys.argv[2])
