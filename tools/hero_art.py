"""Draws the title-screen art for rethinkify.com: deep space over the hull of something
enormous, the way the arcade shooters of the late 80s opened, with the ship and a wave of
enemies flying through it.

The world is drawn in C64 multicolour pixels (two screen pixels wide, one tall), the stars and
the ship in single pixels, all in the C64 palette as the site uses it. The background layers
tile sideways without a seam. The page scales everything up by the same whole number with
`image-rendering: pixelated` (see `.hero-scene` in styles.css) and drifts the layers past at
different speeds, like an arcade machine's attract mode.

The ship is the game's: web/static/game/art.js draws it from the same rows (SHIP_ART), and the
enemies with the same sculpting kit (Sculpt), so change them together. The wave here is silver
rather than its stage's red, so it stays quiet beside the page. The game draws its own art when
it runs; this script makes the title screen, which has to work without it.

    .venv/Scripts/python.exe tools/hero_art.py [--preview DIR]

Needs Pillow (tools/requirements.txt), a development dependency only; the site does not use it.
The output is the same on every run, because the randomness is seeded. To change the art,
change a seed or a parameter, and save it under new file names (FILES below): /static/images is
cached for 30 days, so a reused name keeps serving the old picture. Then update the url()s in
styles.css.
"""

from __future__ import annotations

import argparse
import math
import random
from pathlib import Path

from PIL import Image

IMAGES = Path(__file__).resolve().parent.parent / "web" / "static" / "images"

FILES = {
    "space": "hero-space.webp",
    "depths": "hero-depths.webp",
    "hull": "hero-hull.webp",
    "ship": "hero-ship.webp",
    "wave": "hero-wave.webp",
}

CLEAR = (0, 0, 0, 0)
BLACK = (0x00, 0x00, 0x00, 255)
WHITE = (0xFF, 0xFF, 0xFF, 255)
RED = (0xE0, 0x40, 0x40, 255)
CYAN = (0x70, 0xD8, 0xE0, 255)
BLUE = (0x40, 0x40, 0xE0, 255)
YELLOW = (0xFF, 0xE0, 0x80, 255)
AMBER = (0xE0, 0xA0, 0x40, 255)
DGREY = (0x44, 0x44, 0x44, 255)
GREY = (0x6C, 0x6C, 0x6C, 255)
LGREY = (0xA8, 0xA8, 0xA8, 255)
LBLUE = (0xA0, 0xA0, 0xFF, 255)
DBLUE = (0x28, 0x28, 0x78, 255)
NAVY = (0x17, 0x18, 0x3A, 255)
NIGHT = (0x0A, 0x0A, 0x0D, 255)

# The hull's shades, dark to highlight, and the wave's: silver, like the wordmark's chrome.
ROCK = (DGREY, GREY, LGREY, WHITE)
SILVER = (DGREY, GREY, LGREY, WHITE)

# Rows at the bottom of the hull that the wordmark covers (see `.hero-text` in styles.css).
SHADOW = 13

# The player's ship, nose to the right: the same rows as SHIP_ART in art.js.
SHIP_ART = [
    "....kkk.........................",
    "...kllwk........................",
    "...klllwkk......................",
    "..kdgllllwkkkkkk................",
    ".kddgglllllllwwwkkk.............",
    "kdddgggllcccbbbllwwwkkkk........",
    "krrrrggglccccbbllllllwwwwkkkk...",
    "kddggggggllllllllllllllllllwwkk.",
    "kdddgggggggggggggglllllkkkkkk...",
    ".kdddddgggggddddddkkkkkk........",
    "..kkkkdddkkkkkkkkk..............",
    "......kkk.......................",
]
SHIP_KEY = {
    ".": CLEAR,
    "k": BLACK,
    "w": WHITE,
    "l": LGREY,
    "g": GREY,
    "d": DGREY,
    "b": BLUE,
    "c": LBLUE,
    "r": RED,
}


class Grid:
    """A picture as rows of colours, with helpers for fat pixels and for wrapping sideways."""

    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.rows = [[CLEAR] * width for _ in range(height)]

    def get(self, x: int, y: int):
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.rows[y][x]
        return CLEAR

    def set(self, x: int, y: int, colour) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.rows[y][x] = colour

    def wrap(self, x: int, y: int, colour) -> None:
        """Sets a pixel, wrapping sideways, so the picture tiles without a seam."""
        if 0 <= y < self.height:
            self.rows[y][x % self.width] = colour

    def fat(self, x: int, y: int, colour) -> None:
        """Sets fat pixel x (screen pixels 2x and 2x + 1), wrapping sideways."""
        self.wrap(2 * x, y, colour)
        self.wrap(2 * x + 1, y, colour)

    def paste(self, other: Grid, ox: int, oy: int) -> None:
        for y in range(other.height):
            for x in range(other.width):
                if other.rows[y][x] != CLEAR:
                    self.set(ox + x, oy + y, other.rows[y][x])

    def image(self) -> Image.Image:
        img = Image.new("RGBA", (self.width, self.height), CLEAR)
        img.putdata([c for row in self.rows for c in row])
        return img


def in_triangle(x, y, a, b, c) -> bool:
    d1 = (x - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (y - b[1])
    d2 = (x - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (y - c[1])
    d3 = (x - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (y - a[1])
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


class Sculpt:
    """The game's kit for sculpting creatures (Sculpt in art.js), cut down to what the title
    screen needs: shaded ellipses, flat triangles and fixed-colour dots on fat pixels, most of
    them mirrored top to bottom like a face, then the edges lit and an outline round it.
    """

    FIXED = 9

    def __init__(self, w: int, h: int):
        self.cols = (w + 1) // 2
        self.h = h | 1
        self.mid = self.h // 2
        self.level = [[-1] * self.cols for _ in range(self.h)]
        self.ramp = [[None] * self.cols for _ in range(self.h)]

    def cells(self):
        for j in range(self.h):
            for i in range(self.cols):
                yield 2 * i + 1, j - self.mid, i, j

    def ellipse(self, cx, cy, rx, ry, ramp, mirror=True, rings=0) -> None:
        for sy in [cy, -cy] if mirror and cy else [cy]:
            for x, y, i, j in self.cells():
                dx, dy = (x - cx) / rx, (y - sy) / ry
                d2 = dx * dx + dy * dy
                if d2 > 1:
                    continue
                lum = -dx * 0.35 - dy * 0.55 + math.sqrt(1 - d2) * 0.65
                level = 3 if lum > 0.8 else 2 if lum > 0.45 else 1 if lum > 0.1 else 0
                if rings and (math.sqrt(d2) * rings) % 1 > 0.8:
                    level -= 1
                self.level[j][i] = max(0, min(3, level))
                self.ramp[j][i] = ramp

    def tri(self, a, b, c, ramp, level=2, mirror=True) -> None:
        for s in [1, -1] if mirror else [1]:
            p, q, t = [(x, y * s) for x, y in (a, b, c)]
            for x, y, i, j in self.cells():
                if in_triangle(x, y, p, q, t):
                    self.level[j][i] = level if s > 0 else max(0, level - 1)
                    self.ramp[j][i] = ramp

    def dot(self, x, y, colour, mirror=True) -> None:
        for sy in [y, -y] if mirror and y else [y]:
            i, j = int(x // 2), int(sy) + self.mid
            if 0 <= i < self.cols and 0 <= j < self.h:
                self.level[j][i] = self.FIXED
                self.ramp[j][i] = colour

    def picture(self, outline=BLACK) -> Grid:
        def filled(i, j):
            return 0 <= i < self.cols and 0 <= j < self.h and self.level[j][i] >= 0

        g = Grid((self.cols + 2) * 2, self.h + 2)
        for j in range(-1, self.h + 1):
            for i in range(-1, self.cols + 1):
                if filled(i, j):
                    if self.level[j][i] == self.FIXED:
                        colour = self.ramp[j][i]
                    else:
                        rim = (0 if filled(i, j - 1) else 1) - (
                            0 if filled(i, j + 1) else 1
                        )
                        colour = self.ramp[j][i][max(0, min(3, self.level[j][i] + rim))]
                elif any(
                    filled(i + a, j + b) for a, b in ((-1, 0), (1, 0), (0, -1), (0, 1))
                ):
                    colour = outline
                else:
                    continue
                g.set(2 * (i + 1), j + 1, colour)
                g.set(2 * (i + 1) + 1, j + 1, colour)
        return g


def space(width: int = 512, height: int = 136, seed: int = 7) -> Image.Image:
    """The farthest stars: faint and many, in single pixels."""
    rng = random.Random(seed)
    g = Grid(width, height)
    for _ in range(170):
        g.set(
            rng.randrange(width),
            rng.randrange(height),
            rng.choice([DGREY, DGREY, DGREY, GREY, DBLUE]),
        )
    return g.image()


def depths(width: int = 256, height: int = 136, seed: int = 1987) -> Image.Image:
    """Nearer stars, brighter, a few of them sparkling; and low down, the dark hulks of
    distant structures with the odd light on them, in fat pixels. `width` is in fat pixels.
    """
    rng = random.Random(seed)
    g = Grid(width * 2, height)
    for _ in range(44):
        g.set(
            rng.randrange(width * 2),
            rng.randrange(height - 34),
            rng.choice([GREY, LGREY, LGREY, WHITE, LBLUE, CYAN]),
        )
    for _ in range(5):
        x, y = rng.randrange(2, width * 2 - 2), rng.randrange(2, height - 50)
        g.set(x, y, WHITE)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            g.set(x + dx, y + dy, LBLUE)

    # Two ranks of silhouettes: the far one darker and taller, the near one with lights. Domes,
    # spires and masts rather than towers, so they read as hulks in space, not a city.
    for colour, tallest, gaps in ((NAVY, 58, (0, 3)), (DBLUE, 40, (2, 12))):
        x = rng.randrange(6)
        while x < width:
            kind = rng.choice(["spire", "dome", "dome", "dome", "mast"])
            w = (
                1
                if kind == "mast"
                else rng.randint(6, 16) if kind == "dome" else rng.randint(3, 8)
            )
            h = rng.randint(14, tallest)
            for i in range(w):
                col = h
                centre = (w - 1) / 2
                if kind == "spire":
                    col = round(h * (1 - abs(i - centre) / (w / 2 + 0.5)))
                elif kind == "dome":
                    col = round(
                        h / 2 * math.sqrt(max(0, 1 - ((i - centre) / (w / 2)) ** 2))
                    )
                for y in range(height - col, height):
                    g.fat(x + i, y, colour)
            if colour == DBLUE:
                for _ in range(w):
                    if rng.random() < 0.25:
                        g.fat(
                            x + rng.randrange(w),
                            height - rng.randint(3, max(4, h - 3)),
                            BLUE,
                        )
                if kind == "mast":
                    g.fat(x, height - h - 1, RED)
            x += w + rng.randint(*gaps)
    return g.image()


def hull(width: int = 256, height: int = 56, seed: int = 64) -> Image.Image:
    """The hull of something huge, scrolling past underneath: machinery lit from above, built of
    panels with pipes, ribs, grilles and vents in them, with domes, tanks, gun turrets and masts
    on top. The game draws its terrain the same way (terrain in art.js). `width` is in fat
    pixels; everything wraps, so the strip tiles.

    Its underside falls away into shadow, the bottom SHADOW rows, because the wordmark sits over
    them and its steel needs a dark ground to read against."""
    rng = random.Random(seed)
    dark, mid, light, hi = ROCK
    base = height - 26
    top = [base] * width

    # Raised decks, stepped like machinery.
    x = 6
    while x < width - 10:
        run = rng.randint(10, 28)
        rise = rng.choice([0, 3, 6, 6, 9])
        for i in range(run):
            top[(x + i) % width] = base - rise
        x += run + rng.randint(4, 14)

    domes, tanks, turrets, masts = [], [], [], []
    for x in range(8, width - 8, rng.randint(26, 34)):
        kind = rng.choice(["dome", "tank", "turret", "mast", "dome"])
        surface = top[x]
        if kind == "dome":
            r = rng.randint(4, 7)
            h = round(r * 1.6)
            domes.append((x, r, h, max(top[(x + i) % width] for i in range(-r, r + 1))))
            for i in range(-r, r + 1):
                rise = round(h * math.sqrt(max(0, 1 - (i / (r + 0.5)) ** 2)))
                top[(x + i) % width] = min(top[(x + i) % width], domes[-1][3] - rise)
        elif kind == "tank":
            w = rng.randint(4, 6)
            tall = rng.randint(10, 16)
            tanks.append((x, w, surface - tall))
            for i in range(w):
                top[(x + i) % width] = min(top[(x + i) % width], surface - tall)
        elif kind == "turret":
            turrets.append((x, surface))
        else:
            masts.append((x, surface, rng.randint(6, 12)))

    panel_of, panel_x, panel_w = [0] * width, [0] * width, []
    x = 0
    while x < width:
        w = rng.randint(5, 14)
        for i in range(min(w, width - x)):
            panel_of[x + i] = len(panel_w)
            panel_x[x + i] = i
        panel_w.append(w)
        x += w
    bands = (0, 9, 19, 31)
    patterns = [
        lambda i, j, w, h: (light, mid, dark, BLACK)[j % 4],
        lambda i, j, w, h: (
            BLACK
            if i % 6 == 0
            else (
                light
                if i % 6 == 1
                else (light, light, mid, mid, dark, dark, BLACK)[min(6, j * 7 // h)]
            )
        ),
        lambda i, j, w, h: (light, mid, dark)[i % 3],
        lambda i, j, w, h: BLACK if (i + j) % 2 else dark,
        lambda i, j, w, h: light if i % 3 == 1 and j % 3 == 1 else mid,
        lambda i, j, w, h: (
            (BLACK if j % 2 else dark) if 0 < j < h - 1 and 0 < i < w - 1 else mid
        ),
    ]
    choice = [rng.randrange(len(patterns)) for _ in range(len(panel_w) * 3)]

    def inside(x: int, d: int):
        if d >= bands[3]:
            return BLACK if d > 40 or (x + d) % 2 else dark
        band = 2 if d >= bands[2] else 1 if d >= bands[1] else 0
        j = d - bands[band]
        i = panel_x[x]
        if i == 0 or j == 0:
            return BLACK
        if j == 1:
            return light
        w = panel_w[panel_of[x]]
        h = bands[band + 1] - bands[band]
        return patterns[choice[panel_of[x] * 3 + band]](i - 1, j - 2, w - 1, h - 2)

    def surface(x: int) -> int:
        return top[x % width]

    g = Grid(width * 2, height)
    for x in range(width):
        t = top[x]
        for y in range(t, height):
            d_top, d_side, side = y - t, 9, 0
            for i in (1, 2):
                if surface(x - i) > y:
                    d_side, side = i - 1, -1
                    break
                if surface(x + i) > y:
                    d_side, side = i - 1, 1
                    break
            if side and d_side < d_top:
                colour = (
                    (mid if d_side else light)
                    if side < 0
                    else (dark if d_side else BLACK)
                )
            elif d_top < 4:
                colour = (hi, light, mid, BLACK)[d_top]
            else:
                colour = inside(x, d_top - 4)
            g.fat(x, y, colour)

    for x in range(5, width, rng.randint(14, 22)):
        if panel_x[x] > 1 and top[x] + 7 < height - 2:
            g.fat(x, top[x] + rng.randint(5, 8), CYAN if rng.random() < 0.7 else RED)

    for cx, r, h, floor in domes:
        for x in range(cx - r, cx + r + 1):
            for y in range(floor - h, floor):
                dx, dy = (x - cx) / (r + 0.5), (floor - y) / h
                d2 = dx * dx + dy * dy
                if d2 > 1:
                    continue
                lum = -dx * 0.5 + dy * 0.7 + math.sqrt(1 - d2) * 0.4
                colour = (
                    hi
                    if lum > 0.95
                    else light if lum > 0.6 else mid if lum > 0.25 else dark
                )
                if d2 > 0.82:
                    colour = BLACK
                elif (floor - y) % 4 == 0 and lum < 0.95:
                    colour = dark
                g.fat(x, y, colour)
        g.fat(cx - 1, floor - h + 2, CYAN)

    cylinder = (dark, mid, light, light, mid, dark, dark)
    for x0, w, crown in tanks:
        for x in range(x0, x0 + w):
            shade = cylinder[min(6, (x - x0) * 7 // w)]
            for y in range(crown, height):
                ring = (y - crown) % 7
                colour = (
                    BLACK
                    if ring == 0
                    else (mid if shade == dark else light) if ring == 1 else shade
                )
                g.fat(x, y, BLACK if x in (x0, x0 + w - 1) else colour)

    # Gun turrets: a small dome with its barrel raised towards the sky ahead.
    for x, surface_y in turrets:
        for i, row in enumerate(("..##..", ".#ll#.", "#lhhm#")):
            for j, ch in enumerate(row):
                if ch != ".":
                    colour = {"#": BLACK, "l": light, "h": hi, "m": mid}[ch]
                    g.wrap(2 * x + j, surface_y - 3 + i, colour)
        for k in range(1, 6):
            g.wrap(2 * x + 2 - k, surface_y - 3 - k, LGREY)

    for x, surface_y, tall in masts:
        for y in range(surface_y - tall, surface_y):
            g.fat(x, y, dark)
        g.fat(x, surface_y - tall - 1, RED)

    # The underside in shadow: a shade darker, then dithered, then the page's own night.
    darker = {hi: light, light: mid, mid: dark, dark: NIGHT, CYAN: dark, RED: dark}
    for k in range(SHADOW):
        y = height - SHADOW + k
        for x in range(width * 2):
            colour = g.rows[y][x]
            if colour == CLEAR:
                continue
            if k >= SHADOW - 5 or (k >= 4 and (x // 2 + y) % 2):
                g.rows[y][x] = NIGHT
            else:
                g.rows[y][x] = darker.get(colour, colour)
    return g.image()


def ship() -> Image.Image:
    """The ship in two frames, one above the other, its exhaust short then long, drawn as the
    game draws it (drawPlayer in game.js). Each frame is 40 x 12, with room for the exhaust.
    """
    g = Grid(40, 24)
    for frame, flame in enumerate((4, 7)):
        ox, oy = 8, frame * 12
        for y, row in enumerate(SHIP_ART):
            for x, ch in enumerate(row):
                if SHIP_KEY[ch] != CLEAR:
                    g.set(ox + x, oy + y, SHIP_KEY[ch])
        for i in range(flame):
            g.set(ox - flame + i, oy + 7, RED)
        for i in range(flame - 2):
            for y in (6, 7, 8):
                g.set(ox - flame + 2 + i, oy + y, AMBER)
        g.set(ox - 2, oy + 7, YELLOW)
        g.set(ox - 1, oy + 7, YELLOW)
    return g.image()


def drifter(down: bool) -> Grid:
    """The first stage's flapping thing (the drifter in art.js), wings up or down, in silver
    with a light blue eye."""
    s = Sculpt(24, 23)
    s.tri((7, -2), (15, -2), (21, -4) if down else (19, -10), SILVER, 2)
    s.tri((14, -2), (14, 2), (23, 0), SILVER, 1, mirror=False)
    s.ellipse(11, 0, 7, 4.5, SILVER, rings=3)
    s.ellipse(4.5, 0, 4, 3.5, SILVER)
    s.dot(2, 0, LBLUE)
    return s.picture()


def wave() -> Image.Image:
    """Five of them on a sine curve, in two frames one above the other, their wings out of
    step. Each frame is 140 x 48."""
    up, down = drifter(False), drifter(True)
    g = Grid(140, 96)
    for frame in range(2):
        for i in range(5):
            g.paste(
                up if (i + frame) % 2 == 0 else down,
                i * 28,
                frame * 48 + 11 + round(math.sin(i * 0.9) * 10),
            )
    return g.image()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--preview", type=Path, help="also write an enlarged preview PNG here"
    )
    args = parser.parse_args()

    art = {
        "space": space(),
        "depths": depths(),
        "hull": hull(),
        "ship": ship(),
        "wave": wave(),
    }
    for name, img in art.items():
        img.save(IMAGES / FILES[name], lossless=True, quality=100, method=6)
        print(f"wrote {FILES[name]} {img.size}")

    if args.preview:
        # The scene as the page draws it at three times, with every layer at its start.
        scene = Image.new("RGBA", (512, 136), NIGHT)
        scene.alpha_composite(art["space"], (0, 0))
        scene.alpha_composite(art["depths"], (0, 0))
        scene.alpha_composite(art["hull"], (0, scene.height - art["hull"].height))
        scene.alpha_composite(art["ship"].crop((0, 12, 40, 24)), (60, 50))
        scene.alpha_composite(art["wave"].crop((0, 0, 140, 48)), (330, 6))
        args.preview.mkdir(parents=True, exist_ok=True)
        path = args.preview / "hero-preview.png"
        scene.resize((scene.width * 3, scene.height * 3), Image.NEAREST).save(path)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
