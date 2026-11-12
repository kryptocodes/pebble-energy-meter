"""Renders the meter plate textures and dithers them to Pebble colours.

    uv run --with pillow --with numpy python tools/make_plates.py

Positions use the same 144x168 design grid as src/c/main.c, so keep the
two in sync when moving the register, disc or screws.
"""
import os
import numpy as np
from PIL import Image, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), '..', 'resources', 'images')
SS = 4  # supersampling factor

# greys only, so the plate stays crisp on the reflective screen
PALETTE = [0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000]


class Plate:
    def __init__(self, w, h, content=None, round_=False):
        self.W, self.H = w, h
        self.cx, self.cy, self.cw, self.ch = content or (0, 0, w, h)
        self.round = round_
        self.w, self.h = w * SS, h * SS
        self.yy, self.xx = np.mgrid[0:self.h, 0:self.w].astype(np.float32)
        self.lum = np.ones((self.h, self.w), np.float32)

    # design grid -> screen pixels (matches X()/Y() in main.c)
    def X(self, v):
        return self.cx + v * self.cw // 144

    def Y(self, v):
        return self.cy + v * self.ch // 168

    def blur(self, a, r):
        im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
        return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), dtype=np.float32) / 255

    def vignette(self, amount):
        if self.round:
            r = self.w / 2
            d = np.hypot(self.xx - r, self.yy - r) / r
        else:
            dx = np.abs(self.xx - self.w / 2) / (self.w / 2)
            dy = np.abs(self.yy - self.h / 2) / (self.h / 2)
            d = np.maximum(dx, dy)
        self.lum -= np.clip(d - 0.82, 0, 1) / 0.18 * amount

    def recess(self, x, y, w, h, depth=0.35, spread=2.5):
        """Soft shadow below and right of a window cut into the plate."""
        m = np.zeros_like(self.lum)
        x0, y0 = self.X(x) * SS, self.Y(y) * SS
        x1, y1 = self.X(x + w) * SS, self.Y(y + h) * SS
        m[y0 + 2 * SS:y1 + 3 * SS, x0 + 2 * SS:x1 + 3 * SS] = 1
        self.lum -= self.blur(m, spread * SS) * depth

    def ring_shadow(self, cx, cy, r, depth=0.3):
        d = np.hypot(self.xx - cx * SS - 1.5 * SS, self.yy - cy * SS - 2 * SS) / SS
        self.lum -= np.clip(1 - (d - r) / 3.5, 0, 1) * (d > r - 1) * depth

    def screw(self, x, y, r):
        cx, cy, sr = x * SS, y * SS, r * SS
        d = np.hypot(self.xx - cx, self.yy - cy)
        # shadow under the head
        sd = np.hypot(self.xx - cx - SS, self.yy - cy - 1.5 * SS)
        self.lum -= np.clip(1 - (sd - sr) / (2.5 * SS), 0, 1) * (sd > sr) * 0.35
        inside = d <= sr
        nx, ny = (self.xx - cx) / sr, (self.yy - cy) / sr
        shade = np.clip(0.85 - 0.25 * (nx + ny) - 0.15 * (nx * nx + ny * ny), 0.3, 1)
        self.lum = np.where(inside, shade, self.lum)
        ang = np.deg2rad(-28)
        u = (self.xx - cx) * np.cos(ang) + (self.yy - cy) * np.sin(ang)
        v = -(self.xx - cx) * np.sin(ang) + (self.yy - cy) * np.cos(ang)
        self.lum[inside & (np.abs(v) < max(1.0, sr * 0.16)) & (np.abs(u) < sr * 0.85)] = 0.12
        self.lum[(d > sr - 0.8 * SS) & (d <= sr + 0.3 * SS)] = 0.15

    def rim(self):
        if self.round:
            r = self.w / 2
            d = np.hypot(self.xx - r, self.yy - r)
            e = (r - d) / SS
            self.lum -= np.clip(1 - e / 3.0, 0, 1) * 0.45
            self.lum[d > r] = 0
        else:
            e = np.minimum.reduce([self.xx, self.w - 1 - self.xx, self.yy, self.h - 1 - self.yy]) / SS
            self.lum -= np.clip(1 - e / 3.0, 0, 1) * 0.45

    def save(self, name):
        rgb = np.clip(np.stack([self.lum] * 3, -1), 0, 1)
        img = Image.fromarray((rgb * 255).astype(np.uint8)).resize((self.W, self.H), Image.LANCZOS)
        pal = Image.new('P', (1, 1))
        flat = []
        for c in PALETTE:
            flat += [(c >> 16) & 255, (c >> 8) & 255, c & 255]
        pal.putpalette(flat + [0] * (768 - len(flat)))
        img.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG).save(os.path.join(OUT, name), optimize=True)
        print(name, img.size)


def india(w, h, name, round_=False):
    content = (w * 28 // 180, h * 18 // 180, w * 124 // 180, h * 146 // 180) if round_ else None
    p = Plate(w, h, content, round_)
    p.vignette(0.06)
    p.recess(3, 24, 138, 38)                                   # register window
    p.recess(4, 77, 136, 11, depth=0.3, spread=1.5)            # disc slot
    if round_:
        p.recess(116, 128, 26, 26, depth=0.25, spread=1.5)     # seal sticker
    else:
        p.recess(114, 139, 26, 26, depth=0.25, spread=1.5)
    p.rim()
    p.save(name)


def euro(w, h, name):
    p = Plate(w, h)
    p.vignette(0.06)
    p.recess(6, 30, 104, 24)                                   # register window
    r = p.X(15)
    p.ring_shadow(p.X(126), p.Y(1) + r, r)                     # test dial
    for sx, sy in [(9, 21), (8, 95), (136, 95)]:
        p.screw(p.X(sx), p.Y(sy), p.X(4))
    p.rim()
    p.save(name)


if __name__ == '__main__':
    india(144, 168, 'plate_in_144.png')
    india(180, 180, 'plate_in_180.png', round_=True)
    india(200, 228, 'plate_in_200.png')
    euro(144, 168, 'plate_144.png')
    euro(200, 228, 'plate_200.png')
