"""Assets for Meteora NAM Bus Compressor from the final artwork (1680 x 637).
Usage: python3 make_assets.py <panel.jpg> <outdir>
Removes the painted knob pointers and the meter needle (drawn live by the
plugin) and cuts per-knob sprites, the power-off and clip-LED-on sprites.
"""
import sys, os, math
import numpy as np, cv2

SRC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
img = cv2.imread(SRC)                       # BGR
H, W = img.shape[:2]
S = W / 1356.0                              # scale from the previous layout

# name: (cx, cy, sprite radius, pointer angle painted in the artwork, pointer length)
KNOBS = {
    'threshold': (312.0, 497.0, 66, 1.5, 62),
    'ratio':     (580.0, 497.0, 66, 1.5, 62),
    'attack':    (846.0, 497.0, 66, 25.0, 62),
    'release':   (1117.0, 497.0, 66, 120.5, 62),
    'makeup':    (1380.4, 497.0, 66, -77.5, 62),
    'mix':       (1323.5, 212.0, 60, 150.5, 48),
}
PIVOT = (849.1, 297.35)                     # fitted on the painted needle
FACE = (521.0 * S, 66.0 * S, 329.0 * S, 163.0 * S)
NEEDLE_ANGLE = 9.97                           # fitted (GR ~3 dB in the artwork)


def band_mask(c, ang, r0, r1, half):
    m = np.zeros((H, W), np.uint8)
    a = math.radians(ang)
    p0 = (c[0] + r0 * math.sin(a), c[1] - r0 * math.cos(a))
    p1 = (c[0] + r1 * math.sin(a), c[1] - r1 * math.cos(a))
    cv2.line(m, (int(round(p0[0] * 8)), int(round(p0[1] * 8))), (int(round(p1[0] * 8)), int(round(p1[1] * 8))),
             255, thickness=max(1, int(round(2 * half))), lineType=cv2.LINE_AA, shift=3)
    return m


def polar_fill(src, c, ang, half, r0, r1, clip=None):
    """Remove a thin radial line (pointer / needle) by interpolating across it
    along the circle around its centre: rings, cap shading and scale arcs are
    concentric, so they continue seamlessly."""
    out = src.copy()
    srcf = src.astype(np.float32)
    a0 = math.radians(ang)
    ys, xs = np.mgrid[int(c[1] - r1 - 2):int(c[1] + r1 + 3), int(c[0] - r1 - 2):int(c[0] + r1 + 3)]
    ys = ys.ravel(); xs = xs.ravel()
    ok = (xs >= 0) & (xs < W) & (ys >= 0) & (ys < H)
    xs, ys = xs[ok], ys[ok]
    dx, dy = xs - c[0], ys - c[1]
    r = np.hypot(dx, dy)
    th = np.arctan2(dx, -dy)                                  # 0 = up, clockwise
    dth = (th - a0 + np.pi) % (2 * np.pi) - np.pi
    perp = r * np.sin(dth)
    sel = (np.abs(perp) <= half) & (np.cos(dth) > 0) & (r >= r0) & (r <= r1)
    if clip is not None:
        sel &= clip[ys, xs] > 0
    xs, ys, r = xs[sel], ys[sel], np.maximum(r[sel], 1.0)
    perp = perp[sel]
    w = (half + 1.0) / r                                      # angular half width
    def sample(theta):
        px = (c[0] + r * np.sin(theta)).astype(np.float32)
        py = (c[1] - r * np.cos(theta)).astype(np.float32)
        return cv2.remap(srcf, px.reshape(-1, 1), py.reshape(-1, 1), cv2.INTER_LINEAR).reshape(-1, 3)
    left, right = sample(a0 - w), sample(a0 + w)
    t = ((perp / r) + w) / (2 * w)
    out[ys, xs] = np.clip(left * (1 - t[:, None]) + right * t[:, None], 0, 255).astype(np.uint8)
    return out


clean = img.copy()

# ---- knob pointers
for name, (cx, cy, sr, ang, ln) in KNOBS.items():
    clean = polar_fill(clean, (cx, cy), ang, 5.0, 0.0, ln + 4)

# ---- meter needle (only inside the face)
fx, fy, fw, fh = FACE
face_m = np.zeros((H, W), np.uint8)
face_m[int(fy):int(fy + fh), int(fx):int(fx + fw)] = 255
# 1) the needle itself (narrow, so the digits and letters it crosses survive)
clean = polar_fill(clean, PIVOT, NEEDLE_ANGLE, 3.0, 3.0, 215.0, face_m)
# 2) soft painted shadow right of the needle: remove it as low-frequency shading
#    (text is untouched because the correction map is heavily smoothed)
blur = cv2.medianBlur(clean, 9)
blur = cv2.GaussianBlur(blur, (0, 0), 2.0)
flat = polar_fill(blur, PIVOT, NEEDLE_ANGLE + 2.0, 10.0, 3.0, 215.0, face_m)
ratio = (flat.astype(np.float32) + 1) / (blur.astype(np.float32) + 1)
ratio = cv2.GaussianBlur(ratio, (0, 0), 2.0)
ratio = np.clip(np.where(face_m[..., None] > 0, ratio, 1.0), 0.85, 1.25)
clean = np.clip(clean.astype(np.float32) * ratio, 0, 255).astype(np.uint8)

# 3) between the GR digits "4" and "2": fill with paper colour (median of the
#    surroundings) so the digits are not smeared by the interpolation
paper = cv2.medianBlur(clean, 15)
text_band = band_mask(PIVOT, NEEDLE_ANGLE, 3.0, 215.0, 3.0)
zone = np.zeros_like(text_band); zone[178:194, :] = 255
m = cv2.bitwise_and(text_band, zone) > 0
clean[m] = paper[m]
# 4) the needle runs exactly along the left stroke of the "D" of REDUCTION:
#    inside the letter keep the original pixels (needle = stroke there)
clean[242:253, 852:866] = img[242:253, 852:866]

# ---- sprites (circular alpha, cut from the cleaned artwork at their own position)
def sprite(cx, cy, r, src=None, feather=2.0):
    src = clean if src is None else src
    x0, y0 = int(round(cx - r)), int(round(cy - r))
    crop = src[y0:y0 + 2 * r, x0:x0 + 2 * r]
    ss = 4
    m = np.zeros((2 * r * ss, 2 * r * ss), np.uint8)
    cv2.circle(m, (r * ss, r * ss), (r - 1) * ss, 255, -1, cv2.LINE_AA)
    m = cv2.resize(m, (2 * r, 2 * r), interpolation=cv2.INTER_AREA)
    m = cv2.GaussianBlur(m, (0, 0), feather)
    out = np.dstack([crop, m])
    return out, (x0, y0)


meta = {}
for name, (cx, cy, sr, ang, ln) in KNOBS.items():
    spr, origin = sprite(cx, cy, sr)
    cv2.imwrite(os.path.join(OUT, f'knob_{name}.png'), spr)
    meta[name] = origin

# power button: artwork is lit -> make an "off" sprite
PWR = (1523.0, 481.0, 58)
spr, pwr_origin = sprite(*PWR, src=img, feather=2.5)
hsv = cv2.cvtColor(spr[..., :3], cv2.COLOR_BGR2HSV).astype(np.float32)
yy, xx = np.mgrid[0:2 * PWR[2], 0:2 * PWR[2]]
d = np.hypot(xx - PWR[2], yy - PWR[2])
lensw = np.clip((40.0 - d) / 6.0, 0, 1)                 # 1 inside the lens, fades over the bezel edge
redness = np.clip((hsv[..., 1] - 50) / 80, 0, 1) * (((hsv[..., 0] < 16) | (hsv[..., 0] > 160)).astype(np.float32))
k = 1 - lensw * 0.68 - (1 - lensw) * 0.35 * redness      # lens -68 % (hot spot included), red bezel reflections -35 %
hsv[..., 2] *= k
hsv[..., 1] *= 1 - 0.2 * redness * lensw
spr[..., :3] = cv2.cvtColor(np.clip(hsv, 0, 255).astype(np.uint8), cv2.COLOR_HSV2BGR)
cv2.imwrite(os.path.join(OUT, 'power_off.png'), spr)

# clip LED: artwork is off -> make an "on" sprite with glow
LED = (1519.0, 149.0, 30)
spr, led_origin = sprite(*LED, src=img, feather=3.0)
hsv = cv2.cvtColor(spr[..., :3], cv2.COLOR_BGR2HSV).astype(np.float32)
yy, xx = np.mgrid[0:2 * LED[2], 0:2 * LED[2]]
d = np.hypot(xx - LED[2], yy - LED[2])
lens = d < 15
red = ((hsv[..., 0] < 14) | (hsv[..., 0] > 165)) & (hsv[..., 1] > 50) & lens
hsv[..., 2] = np.where(red, np.clip(hsv[..., 2] * 3.2 + 40, 0, 255), hsv[..., 2])
bgr = cv2.cvtColor(np.clip(hsv, 0, 255).astype(np.uint8), cv2.COLOR_HSV2BGR).astype(np.float32)
glow = np.clip(1 - d / 28, 0, 1) ** 2
hot = np.clip(1 - d / 7, 0, 1) ** 1.5
bgr += glow[..., None] * np.array([20, 40, 160]) + hot[..., None] * np.array([120, 170, 255]) * 0.6
spr[..., :3] = np.clip(bgr, 0, 255).astype(np.uint8)
cv2.imwrite(os.path.join(OUT, 'led_on.png'), spr)

cv2.imwrite(os.path.join(OUT, 'panel.png'), clean, [cv2.IMWRITE_PNG_COMPRESSION, 9])

print('size', W, H)
for k, (cx, cy, sr, ang, ln) in KNOBS.items():
    print(f'knob {k}: centre ({cx}, {cy}) sprite r {sr} pointer {ln}')
print('pivot', PIVOT, 'face', FACE)
print('power', PWR, 'led', LED)
print('vu', 113 * S, 391 * S, 'capture', 177 * S, 302 * S, 'eco', 291 * S, 302 * S)


# ---------------------------------------------------------------- VU glass overlay
# Drawn by the plugin above the needle. 2x resolution for HiDPI screens.
def make_glass(path, fw, fh, k=2):
    W2, H2 = int(round(fw * k)), int(round(fh * k))
    yy, xx = np.mgrid[0:H2, 0:W2].astype(np.float32)
    u, v = xx / W2, yy / H2
    rgb = np.ones((H2, W2, 3), np.float32) * 255
    a = np.zeros((H2, W2), np.float32)
    # 1) diagonal window reflection: crisp right edge, fading to the left
    edge1 = 0.60 - 0.34 * v
    s1 = np.clip((edge1 - u) / 0.012, 0, 1) * np.clip((u - (edge1 - 0.30)) / 0.30, 0, 1)
    a += s1 * (0.085 - 0.05 * v)
    # 2) bright thin streak on that edge (glass bevel catching the light)
    d2 = np.abs(u - (edge1 - 0.006))
    a += np.exp(-(d2 / 0.0045) ** 2) * (0.30 - 0.18 * v)
    # 3) second, narrower reflection further right
    e3 = 0.84 - 0.34 * v
    s3 = np.clip((e3 - u) / 0.008, 0, 1) * np.clip((u - (e3 - 0.06)) / 0.04, 0, 1)
    a += s3 * 0.05
    # 4) highlight along the top edge and a faint one along the bottom (curved glass)
    a += np.exp(-((v - 0.03) / 0.018) ** 2) * 0.22 * np.clip(1 - np.abs(u - 0.45) * 1.5, 0, 1)
    a += np.exp(-((v - 0.965) / 0.015) ** 2) * 0.10 * np.clip(1 - np.abs(u - 0.5) * 1.7, 0, 1)
    # 5) specular glints
    a += np.exp(-(((u - 0.93) / 0.03) ** 2 + ((v - 0.09) / 0.05) ** 2)) * 0.35
    a += np.exp(-(((u - 0.06) / 0.02) ** 2 + ((v - 0.88) / 0.04) ** 2)) * 0.12
    a = np.clip(a, 0, 1)
    # 6) dark inner shadow on every edge (glass thickness / bezel), separate dark layer
    dist = np.minimum.reduce([xx, W2 - 1 - xx, yy, H2 - 1 - yy]) / k
    sh = np.exp(-dist / 3.0) * 0.55 + np.exp(-dist / 16.0) * 0.16
    sh[:int(10 * k), :] = np.maximum(sh[:int(10 * k), :], 0)          # (top already shaded by the art)
    # composite white (a) over dark (sh): premultiplied combine
    out_a = a + sh * (1 - a)
    col = (rgb * a[..., None] + np.zeros_like(rgb) * (sh * (1 - a))[..., None]) / np.maximum(out_a[..., None], 1e-6)
    out = np.dstack([col[..., 2], col[..., 1], col[..., 0], out_a * 255]).astype(np.float32)
    # slight blur so nothing looks drawn
    out = cv2.GaussianBlur(out, (0, 0), 0.6 * k)
    cv2.imwrite(path, np.clip(out, 0, 255).astype(np.uint8))


make_glass(os.path.join(OUT, 'glass.png'), FACE[2], FACE[3])
