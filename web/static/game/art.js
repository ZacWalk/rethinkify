/* The game's pictures, every one of them drawn by code.

   Everything here is a pure function of its arguments: given a seed it returns Bitmaps of
   palette indices, so a stage always draws the same way, and the art can be drawn and checked
   outside a browser. game.js turns Bitmaps into canvases.

   The C64's multicolour pixels are twice as wide as they are tall. The world (enemies, bosses,
   terrain, the skyline) is drawn in those fat pixels and widened at the end; the ship, shots,
   pods and text use single pixels, the way C64 games laid a hi-res player sprite over a
   multicolour world. */

// ---------- palette ----------

/* The C64's colours as the rest of the site has them (the --c64-* properties in styles.css),
   with the C64's greys, cyan, purple and green added, the site's navy and night for the sky,
   and the beiges and browns of old computer cases. Index 0 is transparent. */
export const PALETTE = [
  null,
  [0x00, 0x00, 0x00],
  [0xff, 0xff, 0xff],
  [0xe0, 0x40, 0x40],
  [0x70, 0xd8, 0xe0],
  [0xa0, 0x58, 0xc8],
  [0x58, 0xa8, 0x48],
  [0x40, 0x40, 0xe0],
  [0xff, 0xe0, 0x80],
  [0xe0, 0xa0, 0x40],
  [0x9c, 0x74, 0x48],
  [0xff, 0xa0, 0xa0],
  [0x44, 0x44, 0x44],
  [0x6c, 0x6c, 0x6c],
  [0x9a, 0xe2, 0x9b],
  [0xa0, 0xa0, 0xff],
  [0xa8, 0xa8, 0xa8],
  [0x28, 0x28, 0x78],
  [0x17, 0x18, 0x3a],
  [0x0a, 0x0a, 0x0d],
  [0xe4, 0xd8, 0xb8],
  [0xb8, 0xa8, 0x88],
  [0x88, 0x78, 0x5c],
  [0x50, 0x40, 0x2c],
];

export const CLEAR = 0;
export const BLACK = 1;
export const WHITE = 2;
export const RED = 3;
export const CYAN = 4;
export const PURPLE = 5;
export const GREEN = 6;
export const BLUE = 7;
export const YELLOW = 8;
export const AMBER = 9;
export const BROWN = 10;
export const PINK = 11;
export const DGREY = 12;
export const GREY = 13;
export const LGREEN = 14;
export const LBLUE = 15;
export const LGREY = 16;
export const DBLUE = 17;
export const NAVY = 18;
export const NIGHT = 19;
export const BEIGE = 20;
export const TAN = 21;
export const KHAKI = 22;
export const DBROWN = 23;

/* The playfield, in pixels. The HUD sits in a strip below it. */
export const PLAY_W = 320;
export const PLAY_H = 168;

// ---------- randomness ----------

/* A small seeded generator (mulberry32), so a seed always makes the same stage and the same
   tune. Returns floats in [0, 1), with a few helpers hung off it. */
export function rng(seed) {
  let s = seed >>> 0 || 1;
  const next = () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  next.int = (lo, hi) => lo + Math.floor(next() * (hi - lo + 1));
  next.range = (lo, hi) => lo + next() * (hi - lo);
  next.pick = (list) => list[Math.floor(next() * list.length)];
  next.chance = (p) => next() < p;
  return next;
}

/* Smooth one-dimensional value noise in [0, 1], with lattice points every `cell` units. The
   lattice grows in order whatever order it is sampled in, so it stays deterministic. */
export function noise1(r, cell) {
  const lattice = [r()];
  return (x) => {
    const g = Math.max(0, x) / cell;
    const i = Math.floor(g);
    while (lattice.length <= i + 1) lattice.push(r());
    const t = (1 - Math.cos((g - i) * Math.PI)) / 2;
    return lattice[i] + (lattice[i + 1] - lattice[i]) * t;
  };
}

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

// ---------- bitmaps ----------

export class Bitmap {
  constructor(w, h) {
    this.w = w;
    this.h = h;
    this.px = new Uint8Array(w * h);
  }

  /* Rows of characters, one per pixel, each looked up in `key`. */
  static fromArt(rows, key) {
    const b = new Bitmap(Math.max(...rows.map((row) => row.length)), rows.length);
    rows.forEach((row, y) => {
      for (let x = 0; x < row.length; x++) b.px[y * b.w + x] = key[row[x]] ?? CLEAR;
    });
    return b;
  }

  get(x, y) {
    return x < 0 || y < 0 || x >= this.w || y >= this.h ? CLEAR : this.px[y * this.w + x];
  }

  set(x, y, c) {
    if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.px[y * this.w + x] = c;
  }

  fill(x, y, w, h, c) {
    for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) this.set(i, j, c);
  }

  blit(src, dx, dy) {
    for (let y = 0; y < src.h; y++) {
      for (let x = 0; x < src.w; x++) {
        const c = src.px[y * src.w + x];
        if (c) this.set(dx + x, dy + y, c);
      }
    }
  }

  /* Every opaque pixel in one colour: the white flash when something is hit. */
  silhouette(c) {
    const b = new Bitmap(this.w, this.h);
    for (let i = 0; i < this.px.length; i++) b.px[i] = this.px[i] ? c : CLEAR;
    return b;
  }

  /* Each pixel twice as wide: C64 multicolour pixels. */
  widen() {
    const b = new Bitmap(this.w * 2, this.h);
    for (let y = 0; y < this.h; y++) {
      for (let x = 0; x < this.w; x++) {
        const c = this.px[y * this.w + x];
        b.px[y * b.w + 2 * x] = c;
        b.px[y * b.w + 2 * x + 1] = c;
      }
    }
    return b;
  }

  /* A one-pixel outline all round, which makes the bitmap two pixels bigger each way. */
  outlined(c) {
    const b = new Bitmap(this.w + 2, this.h + 2);
    for (let y = -1; y <= this.h; y++) {
      for (let x = -1; x <= this.w; x++) {
        const own = this.get(x, y);
        if (own) b.set(x + 1, y + 1, own);
        else if (this.get(x - 1, y) || this.get(x + 1, y) || this.get(x, y - 1) || this.get(x, y + 1))
          b.set(x + 1, y + 1, c);
      }
    }
    return b;
  }
}

// ---------- font ----------

/* A 5x7 capital font, drawn for this game. Bold text doubles every stroke sideways, which
   gives the two-pixel verticals of the C64's own characters. */
export const GLYPH_W = 5;
export const GLYPH_H = 7;

const GLYPHS = {
  A: '.###. #...# #...# ##### #...# #...# #...#',
  B: '####. #...# #...# ####. #...# #...# ####.',
  C: '.###. #...# #.... #.... #.... #...# .###.',
  D: '####. #...# #...# #...# #...# #...# ####.',
  E: '##### #.... #.... ####. #.... #.... #####',
  F: '##### #.... #.... ####. #.... #.... #....',
  G: '.###. #...# #.... #.### #...# #...# .####',
  H: '#...# #...# #...# ##### #...# #...# #...#',
  I: '.###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.',
  J: '..### ...#. ...#. ...#. ...#. #..#. .##..',
  K: '#...# #..#. #.#.. ##... #.#.. #..#. #...#',
  L: '#.... #.... #.... #.... #.... #.... #####',
  M: '#...# ##.## #.#.# #.#.# #...# #...# #...#',
  N: '#...# ##..# #.#.# #..## #...# #...# #...#',
  O: '.###. #...# #...# #...# #...# #...# .###.',
  P: '####. #...# #...# ####. #.... #.... #....',
  Q: '.###. #...# #...# #...# #.#.# #..#. .##.#',
  R: '####. #...# #...# ####. #.#.. #..#. #...#',
  S: '.#### #.... #.... .###. ....# ....# ####.',
  T: '##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..',
  U: '#...# #...# #...# #...# #...# #...# .###.',
  V: '#...# #...# #...# #...# #...# .#.#. ..#..',
  W: '#...# #...# #...# #.#.# #.#.# ##.## #...#',
  X: '#...# #...# .#.#. ..#.. .#.#. #...# #...#',
  Y: '#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..',
  Z: '##### ....# ...#. ..#.. .#... #.... #####',
  0: '.###. #...# #..## #.#.# ##..# #...# .###.',
  1: '..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.',
  2: '.###. #...# ....# ...#. ..#.. .#... #####',
  3: '##### ...#. ..#.. ...#. ....# #...# .###.',
  4: '...#. ..##. .#.#. #..#. ##### ...#. ...#.',
  5: '##### #.... ####. ....# ....# #...# .###.',
  6: '..##. .#... #.... ####. #...# #...# .###.',
  7: '##### ....# ...#. ..#.. .#... .#... .#...',
  8: '.###. #...# #...# .###. #...# #...# .###.',
  9: '.###. #...# #...# .#### ....# ...#. .##..',
  ' ': '..... ..... ..... ..... ..... ..... .....',
  '.': '..... ..... ..... ..... ..... .##.. .##..',
  ',': '..... ..... ..... ..... .##.. ..#.. .#...',
  '!': '..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..',
  '?': '.###. #...# ....# ...#. ..#.. ..... ..#..',
  '-': '..... ..... ..... .###. ..... ..... .....',
  ':': '..... .##.. .##.. ..... .##.. .##.. .....',
  '/': '....# ....# ...#. ..#.. .#... #.... #....',
  "'": '..#.. ..#.. .#... ..... ..... ..... .....',
  '"': '.#.#. .#.#. ..... ..... ..... ..... .....',
  '(': '...#. ..#.. .#... .#... .#... ..#.. ...#.',
  ')': '.#... ..#.. ...#. ...#. ...#. ..#.. .#...',
  '+': '..... ..#.. ..#.. ##### ..#.. ..#.. .....',
  '=': '..... ..... ##### ..... ##### ..... .....',
  '>': '.#... ..#.. ...#. ....# ...#. ..#.. .#...',
  '<': '...#. ..#.. .#... #.... .#... ..#.. ...#.',
  '%': '##..# ##..# ...#. ..#.. .#... #..## #..##',
  '#': '.#.#. .#.#. ##### .#.#. ##### .#.#. .#.#.',
  '*': '..... #.#.# .###. ##### .###. #.#.# .....',
  _: '..... ..... ..... ..... ..... ..... #####',
  // A solid block: the C64's cursor.
  '@': '##### ##### ##### ##### ##### ##### #####',
};

/* The characters in a font atlas, in order. */
export const CHARSET = Object.keys(GLYPHS).join('');

export function drawGlyph(b, ch, x, y, colour, bold = false) {
  const rows = (GLYPHS[ch] ?? GLYPHS['?']).split(' ');
  for (let j = 0; j < GLYPH_H; j++) {
    for (let i = 0; i < GLYPH_W; i++) {
      if (rows[j][i] !== '#') continue;
      b.set(x + i, y + j, colour);
      if (bold) b.set(x + i + 1, y + j, colour);
    }
  }
}

/* How far a line of text advances per character: one pixel between characters, two when
   bold. */
export const advance = (bold) => GLYPH_W + (bold ? 2 : 1);

export function drawText(b, text, x, y, colour, bold = false) {
  for (const ch of text.toUpperCase()) {
    drawGlyph(b, ch, x, y, colour, bold);
    x += advance(bold);
  }
}

/* Every glyph in one colour, side by side in CHARSET order, for game.js to cut up. */
export function fontAtlas(colour, bold = false) {
  const cell = advance(bold);
  const b = new Bitmap(cell * CHARSET.length, GLYPH_H);
  [...CHARSET].forEach((ch, i) => drawGlyph(b, ch, i * cell, 0, colour, bold));
  return b;
}

/* A 3x5 font, just big enough for what the old computers' screens show (see writeScreens):
   DOS 4.0z's prompt, READY. and the ZX81's K. */
const MICRO_W = 3;

const MICRO = {
  A: '.#. #.# ### #.# #.#',
  C: '.## #.. #.. #.. .##',
  D: '##. #.# #.# #.# ##.',
  E: '### #.. ##. #.. ###',
  K: '#.# #.# ##. #.# #.#',
  O: '.#. #.# #.# #.# .#.',
  R: '##. #.# ##. #.# #.#',
  S: '.## #.. .#. ..# ##.',
  Y: '#.# #.# .#. .#. .#.',
  Z: '### ..# .#. #.. ###',
  0: '### #.# #.# #.# ###',
  4: '#.# #.# ### ..# ..#',
  '.': '... ... ... ... .#.',
  ':': '... .#. ... .#. ...',
  '\\': '#.. #.. .#. ..# ..#',
  '>': '#.. .#. ..# .#. #..',
};

// ---------- the ship ----------

/* The player's fighter, nose to the right. tools/hero_art.py draws the same ship on the
   title screen from the same rows, so change both together. */
export const SHIP_ART = [
  '....kkk.........................',
  '...kllwk........................',
  '...klllwkk......................',
  '..kdgllllwkkkkkk................',
  '.kddgglllllllwwwkkk.............',
  'kdddgggllcccbbbllwwwkkkk........',
  'krrrrggglccccbbllllllwwwwkkkk...',
  'kddggggggllllllllllllllllllwwkk.',
  'kdddgggggggggggggglllllkkkkkk...',
  '.kdddddgggggddddddkkkkkk........',
  '..kkkkdddkkkkkkkkk..............',
  '......kkk.......................',
];

export const SHIP_KEY = {
  '.': CLEAR,
  k: BLACK,
  w: WHITE,
  l: LGREY,
  g: GREY,
  d: DGREY,
  b: BLUE,
  c: LBLUE,
  r: RED,
};

export function ship() {
  return Bitmap.fromArt(SHIP_ART, SHIP_KEY);
}

// ---------- creatures ----------

/* Marks a sculpted fat pixel as one fixed colour rather than a shade on a ramp. */
const FIXED = 9;

const inTriangle = (x, y, [ax, ay], [bx, by], [cx, cy]) => {
  const d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by);
  const d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy);
  const d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay);
  return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
};

/* A small sculpting kit for the world's creatures. Shapes are placed in screen pixels with the
   nose at x = 0 and the centre line at y = 0, and most are mirrored top to bottom like a face.
   Each fat pixel takes whatever covers its centre last, so later shapes sit in front. Shading
   is kept as a level on a colour ramp (0 dark to 3 highlight) until the end, when top edges
   catch the light, bottom edges fall into shadow and a black outline goes round. */
export class Sculpt {
  constructor(w, h) {
    this.cols = Math.ceil(w / 2);
    this.h = h | 1;
    this.mid = this.h >> 1;
    this.level = new Int8Array(this.cols * this.h).fill(-1);
    this.ramp = new Array(this.cols * this.h).fill(null);
  }

  each(fn) {
    for (let j = 0; j < this.h; j++) {
      for (let i = 0; i < this.cols; i++) fn(2 * i + 1, j - this.mid, j * this.cols + i);
    }
  }

  /* A shaded ellipse. Rings darken bands across it, for a segmented, ribbed look. */
  ellipse(cx, cy, rx, ry, ramp, { mirror = true, rings = 0, shade = 0 } = {}) {
    for (const sy of mirror && cy ? [cy, -cy] : [cy]) {
      this.each((x, y, k) => {
        const dx = (x - cx) / rx;
        const dy = (y - sy) / ry;
        const d2 = dx * dx + dy * dy;
        if (d2 > 1) return;
        const lum = -dx * 0.35 - dy * 0.55 + Math.sqrt(1 - d2) * 0.65;
        let level = lum > 0.8 ? 3 : lum > 0.45 ? 2 : lum > 0.1 ? 1 : 0;
        if (rings && (Math.sqrt(d2) * rings) % 1 > 0.8) level -= 1;
        this.level[k] = clamp(level + shade, 0, 3);
        this.ramp[k] = ramp;
      });
    }
    return this;
  }

  /* A flat triangle, for fins, wings, spikes and jaws. Its mirrored twin is a shade darker. */
  tri(a, b, c, ramp, level = 2, mirror = true) {
    for (const s of mirror ? [1, -1] : [1]) {
      const [p, q, t] = [a, b, c].map(([x, y]) => [x, y * s]);
      this.each((x, y, k) => {
        if (!inTriangle(x, y, p, q, t)) return;
        this.level[k] = s > 0 ? level : Math.max(0, level - 1);
        this.ramp[k] = ramp;
      });
    }
    return this;
  }

  /* An ellipse of one fixed colour: a mouth, a window. */
  fill(cx, cy, rx, ry, colour, mirror = true) {
    for (const sy of mirror && cy ? [cy, -cy] : [cy]) {
      this.each((x, y, k) => {
        if (((x - cx) / rx) ** 2 + ((y - sy) / ry) ** 2 > 1) return;
        this.level[k] = FIXED;
        this.ramp[k] = colour;
      });
    }
    return this;
  }

  /* One fat pixel of a fixed colour: an eye, a light, a tooth. */
  dot(x, y, colour, mirror = true) {
    for (const sy of mirror && y ? [y, -y] : [y]) {
      const i = Math.floor(x / 2);
      const j = Math.round(sy) + this.mid;
      if (i < 0 || i >= this.cols || j < 0 || j >= this.h) continue;
      this.level[j * this.cols + i] = FIXED;
      this.ramp[j * this.cols + i] = colour;
    }
    return this;
  }

  /* Where a point in sculpting coordinates lands on the finished bitmap. */
  at(x, y) {
    return { x: 2 * Math.floor(x / 2) + 2, y: Math.round(y) + this.mid + 1 };
  }

  bitmap(outline = BLACK) {
    const { cols, h } = this;
    const filled = (i, j) => i >= 0 && j >= 0 && i < cols && j < h && this.level[j * cols + i] >= 0;
    const b = new Bitmap(cols + 2, h + 2);
    for (let j = -1; j <= h; j++) {
      for (let i = -1; i <= cols; i++) {
        if (filled(i, j)) {
          const k = j * cols + i;
          if (this.level[k] === FIXED) {
            b.set(i + 1, j + 1, this.ramp[k]);
            continue;
          }
          const rim = (filled(i, j - 1) ? 0 : 1) - (filled(i, j + 1) ? 0 : 1);
          b.set(i + 1, j + 1, this.ramp[k][clamp(this.level[k] + rim, 0, 3)]);
        } else if (filled(i - 1, j) || filled(i + 1, j) || filled(i, j - 1) || filled(i, j + 1)) {
          b.set(i + 1, j + 1, outline);
        }
      }
    }
    return b.widen();
  }
}

/* A shaded ball in single pixels, lit from the top left, in four shades from dark to
   highlight. */
export function ball(rx, ry, ramp, outline = BLACK) {
  const b = new Bitmap(rx * 2, ry * 2);
  for (let y = 0; y < ry * 2; y++) {
    for (let x = 0; x < rx * 2; x++) {
      const dx = (x + 0.5 - rx) / rx;
      const dy = (y + 0.5 - ry) / ry;
      const d2 = dx * dx + dy * dy;
      if (d2 > 1) continue;
      const lum = -dx * 0.45 - dy * 0.55 + Math.sqrt(1 - d2) * 0.6;
      b.set(x, y, ramp[lum > 0.8 ? 3 : lum > 0.45 ? 2 : lum > 0.1 ? 1 : 0]);
    }
  }
  return outline ? b.outlined(outline) : b;
}

/* The cast of one stage, in its colours, with a little variety from the stage's seed. Every
   entry is a list of frames. */
export function enemySet(seed, look) {
  const r = rng(seed);
  const { flesh, armour, eye, glow } = look;
  const metal = [DGREY, GREY, LGREY, WHITE];

  // A flapping thing, all wings, with one eye.
  const tip = [r.range(17, 21), r.range(-11, -9)];
  const drifter = [0, 1].map((down) => {
    const s = new Sculpt(24, 23);
    s.tri([7, -2], [15, -2], down ? [tip[0] + 2, -4] : tip, flesh, 2);
    s.tri([14, -2], [14, 2], [23, 0], flesh, 1, false);
    s.ellipse(11, 0, 7, 4.5, flesh, { rings: 3 });
    s.ellipse(4.5, 0, 4, 3.5, flesh);
    s.dot(2, 0, eye);
    return s.bitmap();
  });

  // A dart: a long armoured hull with swept fins and a canopy.
  const finX = r.range(19, 23);
  const darter = (() => {
    const s = new Sculpt(30, 17);
    s.tri([finX - 4, -2], [finX + 4, -2], [finX + 8, -8], armour, 2);
    s.ellipse(15, 0, 14, 3.5, armour);
    s.ellipse(12, -2, 5, 2.2, flesh, { mirror: false });
    s.dot(2, 0, eye, false);
    s.dot(29, 0, glow, false);
    return [s.bitmap()];
  })();

  // A spore: a ribbed ball with spikes, which bursts when it is shot.
  const spikes = r.int(6, 9);
  const spore = [0, 1].map((turn) => {
    const s = new Sculpt(22, 21);
    for (let k = 0; k < spikes; k++) {
      const a = ((k + turn / 2) / spikes) * Math.PI * 2;
      const p = (rad, da) => [11 + Math.cos(a + da) * rad, Math.sin(a + da) * rad];
      s.tri(p(5, -0.3), p(5, 0.3), p(10, 0), flesh, 1, false);
    }
    s.ellipse(11, 0, 6.5, 6.5, flesh, { mirror: false, rings: 2 });
    s.dot(9, -2, glow, false);
    return s.bitmap();
  });

  // A cruiser: a long gunship that takes a lot of shooting.
  const cruiser = (() => {
    const s = new Sculpt(58, 27);
    s.ellipse(31, 0, 27, 6.5, armour);
    s.ellipse(33, 7, 17, 3.5, armour, { mirror: false, shade: -1 });
    s.ellipse(34, -6, 15, 4, armour, { mirror: false });
    s.ellipse(39, -10, 6, 3, armour, { mirror: false });
    for (const gx of [18, 50]) s.ellipse(gx, -6, 3, 2, flesh, { mirror: false });
    s.ellipse(56, 0, 3, 5, flesh, { shade: 1 });
    s.dot(4, 0, eye, false);
    for (let x = 12; x < 52; x += 8) s.dot(x, 3, BLACK, false);
    return [s.bitmap()];
  })();

  // A snake's head, with mandibles; its body is a chain of balls.
  const head = (() => {
    const s = new Sculpt(22, 19);
    s.tri([0, -1], [10, -8], [11, -2], flesh, 2);
    s.ellipse(12, 0, 9, 6.5, flesh, { rings: 2 });
    s.dot(7, -3, eye);
    return [s.bitmap()];
  })();

  // A carrier: slow, harmless, and full of something useful.
  const carrier = [AMBER, YELLOW].map((light) => {
    const s = new Sculpt(28, 17);
    s.tri([16, -4], [24, -4], [26, -8], metal, 2);
    s.ellipse(14, 0, 13, 6, metal);
    s.fill(14, 0, 5, 3, BLACK, false);
    s.fill(14, 0, 3.5, 2, light, false);
    return s.bitmap();
  });

  return {
    drifter,
    darter,
    spore,
    cruiser,
    head,
    segment: [ball(5, 4, flesh)],
    carrier,
    turret: [ball(6, 5, armour)],
  };
}

// ---------- the endoskeletons ----------

/* The robots that walk the hull: chrome skeletons with red eyes and a plasma rifle, after the
   machines of the future war in Terminator 2 (1991). Bones need single pixels, so unlike the
   rest of the world they are drawn hi-res, like the ship. They face left; game.js mirrors them
   to face right.

   Each frame is posed, then painted: limbs are capsules shaded as chrome cylinders lit from the
   top left, the far limbs a shade darker, and the near limbs and the rifle edged in black where
   they cross what is behind them. */

export const ENDO_W = 26;
export const ENDO_H = 32;
/* The rifle's aims, in radians above level; and the poses through a stride, after which comes
   the standing pose. */
export const ENDO_AIMS = [0, 0.35, 0.7, 1.05];
export const ENDO_POSES = 6;
const THIGH = 5.3;
const SHIN = 5.3;
const UPPER_ARM = 3.9;
const FOREARM = 3.9;
const SWING = 0.36;
/* How far a robot walks in one stride, so that its planted foot stays put. */
export const ENDO_STRIDE = 4 * (THIGH + SHIN) * Math.sin(SWING);
/* Where the planted ankle sits, so the feet rest on the bottom row but one; the last row is
   their outline, which lands on the ground. */
const ANKLE_Y = ENDO_H - 2.4;

const CHROME = [DGREY, GREY, LGREY, WHITE];
const CHROME_FAR = [BLACK, DGREY, GREY, LGREY];
const GUNMETAL = [BLACK, BLACK, DGREY, GREY];

/* Chrome lit from the top left: a ramp level from a surface normal. */
function chrome(nx, ny, nz) {
  const lum = -0.55 * nx - 0.55 * ny + 0.55 * nz;
  return lum > 0.75 ? 3 : lum > 0.4 ? 2 : lum > 0.05 ? 1 : 0;
}

/* Paints shaded shapes in single pixels, each over the ones before, in groups. A group begun
   with `edge` gets a black line wherever it crosses an earlier group, so a near limb stands
   clear of the body behind it. bitmap() adds the black outline round the whole. */
class Rig {
  constructor(w, h) {
    this.w = w;
    this.h = h;
    this.colour = new Uint8Array(w * h);
    this.group = new Int16Array(w * h).fill(-1);
    this.edges = [];
  }

  begin(edge = false) {
    this.edges.push(edge);
  }

  paint(x, y, c) {
    if (!c || x < 0 || y < 0 || x >= this.w || y >= this.h) return;
    this.colour[y * this.w + x] = c;
    this.group[y * this.w + x] = this.edges.length - 1;
  }

  dot([x, y], c) {
    this.paint(Math.floor(x), Math.floor(y), c);
  }

  /* Paints every pixel in a box with what `shade` returns for its centre, if anything. */
  cover(x0, y0, x1, y1, shade) {
    for (let y = Math.floor(y0); y <= Math.ceil(y1); y++) {
      for (let x = Math.floor(x0); x <= Math.ceil(x1); x++) this.paint(x, y, shade(x + 0.5, y + 0.5, x, y));
    }
  }

  /* A chrome cylinder from a to b, rounded at the ends. */
  capsule([ax, ay], [bx, by], r, ramp) {
    const dx = bx - ax;
    const dy = by - ay;
    const len2 = dx * dx + dy * dy || 1e-9;
    this.cover(Math.min(ax, bx) - r, Math.min(ay, by) - r, Math.max(ax, bx) + r, Math.max(ay, by) + r, (px, py) => {
      const t = clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0, 1);
      const nx = (px - ax - t * dx) / r;
      const ny = (py - ay - t * dy) / r;
      const d2 = nx * nx + ny * ny;
      return d2 > 1 ? 0 : ramp[chrome(nx, ny, Math.sqrt(1 - d2))];
    });
  }

  /* A chrome ellipsoid; `detail` may change the colour of any pixel of it. */
  ellipse([cx, cy], rx, ry, ramp, detail = null) {
    this.cover(cx - rx, cy - ry, cx + rx, cy + ry, (px, py, x, y) => {
      const nx = (px - cx) / rx;
      const ny = (py - cy) / ry;
      const d2 = nx * nx + ny * ny;
      if (d2 > 1) return 0;
      const c = ramp[chrome(nx, ny, Math.sqrt(1 - d2))];
      return detail ? detail(x, y, c) : c;
    });
  }

  bitmap() {
    const { w, h, colour, edges } = this;
    const at = (x, y) => (x < 0 || y < 0 || x >= w || y >= h ? -1 : this.group[y * w + x]);
    const b = new Bitmap(w, h);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const own = at(x, y);
        const around = [at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1)];
        if (own < 0) {
          if (around.some((o) => o >= 0)) b.set(x, y, BLACK);
        } else b.set(x, y, around.some((o) => o > own && edges[o]) ? BLACK : colour[y * w + x]);
      }
    }
    return b;
  }
}

/* The skull, centred on (cx, cy): a chrome cranium over a face and jaw, a band of dark sockets
   with two red eyes, the nose below them, and a grin of teeth. */
function skull(rig, cx, cy) {
  rig.ellipse([cx + 0.4, cy - 0.9], 2.9, 2.7, CHROME);
  rig.ellipse([cx - 0.6, cy + 0.9], 2.3, 1.8, CHROME);
  rig.capsule([cx - 1.9, cy + 2.5], [cx + 1.1, cy + 2.3], 0.85, CHROME);
  const x = Math.floor(cx);
  const y = Math.floor(cy);
  for (const [dx, dy] of [
    [-2, 0],
    [-1, 0],
    [0, 0],
    [-1, 1],
  ])
    rig.paint(x + dx, y + dy, BLACK);
  rig.paint(x - 2, y, RED);
  rig.paint(x, y, RED);
  for (let dx = -2; dx <= 0; dx++) rig.paint(x + dx, y + 2, dx % 2 ? BLACK : WHITE);
}

/* An elbow, from where the shoulder and the hand are, hanging below the line between them. */
function elbow([sx, sy], [hx, hy]) {
  const dx = hx - sx;
  const dy = hy - sy;
  const d = clamp(Math.hypot(dx, dy), Math.abs(UPPER_ARM - FOREARM) + 0.01, UPPER_ARM + FOREARM - 0.01);
  const base = Math.atan2(dy, dx);
  const bend = Math.acos((UPPER_ARM * UPPER_ARM + d * d - FOREARM * FOREARM) / (2 * UPPER_ARM * d));
  const [a, b] = [base + bend, base - bend].map((t) => [sx + Math.cos(t) * UPPER_ARM, sy + Math.sin(t) * UPPER_ARM]);
  return a[1] > b[1] ? a : b;
}

/* One frame: `aim` indexes ENDO_AIMS, and `phase` runs from 0 to 1 through a stride, or is
   null for standing. Returns the bitmap and where the rifle's muzzle is on it. */
function endoFrame(aim, phase) {
  const rig = new Rig(ENDO_W, ENDO_H);
  // Legs hang from the hip at (0, 0), a thigh swung forward (to the left) by `swing` and a
  // knee bent by `flex`. The swing runs back and forth in straight lines, so a planted foot
  // moves back at a steady pace, matching the robot's walk (see ENDO_STRIDE); each knee bends
  // while its foot swings through, and is straight while the foot is planted.
  const leg = (side, swing, flex) => {
    const knee = [side - Math.sin(swing) * THIGH, Math.cos(swing) * THIGH];
    const shin = swing - flex;
    return { hip: [side, 0], knee, ankle: [knee[0] - Math.sin(shin) * SHIN, knee[1] + Math.cos(shin) * SHIN] };
  };
  const triangle = (p) => {
    const q = ((p % 1) + 1) % 1;
    return q < 0.25 ? 4 * q : q < 0.75 ? 2 - 4 * q : 4 * q - 4;
  };
  const walk = (side, p) => leg(side, SWING * triangle(p), 0.9 * Math.max(0, Math.cos(2 * Math.PI * p)));
  const near = phase === null ? leg(-1, 0.12, 0.08) : walk(-1, phase);
  const far = phase === null ? leg(1, -0.14, 0.04) : walk(1, phase + 0.5);
  // The body rides as high as the lower foot lets it.
  const hx = ENDO_W / 2;
  const hy = ANKLE_Y - Math.max(near.ankle[1], far.ankle[1]);
  const at = ([x, y]) => [hx + x, hy + y];
  const limb = (l, ramp, r) => {
    const [ax, ay] = at(l.ankle);
    rig.capsule(at(l.hip), at(l.knee), r, ramp);
    rig.capsule(at(l.knee), at(l.ankle), r - 0.1, ramp);
    rig.ellipse(at(l.knee), r + 0.1, r + 0.1, ramp);
    rig.capsule([ax + 0.9, ay + 0.7], [ax - 2.2, ay + 0.7], 0.7, ramp);
  };

  // The rifle points along d, and rises towards the shoulder as it aims higher.
  const a = ENDO_AIMS[aim];
  const d = [-Math.cos(a), -Math.sin(a)];
  const under = [d[1], -d[0]];
  const lift = a / ENDO_AIMS[ENDO_AIMS.length - 1];
  const grip = at([2.1 - 3.9 * lift, -5.6 - 2.8 * lift]);
  const along = (k, j = 0) => [grip[0] + d[0] * k + under[0] * j, grip[1] + d[1] * k + under[1] * j];
  const nearShoulder = at([-3.9, -9.5]);
  const farShoulder = at([3.9, -9.5]);
  const hand = along(4.5);
  const nearElbow = elbow(nearShoulder, hand);
  const farElbow = elbow(farShoulder, grip);

  // The far side, in shadow.
  rig.begin();
  limb(far, CHROME_FAR, 1);
  rig.capsule(farShoulder, farElbow, 0.85, CHROME_FAR);
  // The body: pelvis, spine and its pistons, ribcage, shoulder, neck and skull.
  rig.begin();
  rig.capsule(at([-1.8, -0.4]), at([1.8, -0.4]), 1.05, CHROME);
  rig.capsule(at([-1.5, -1.4]), at([-1.5, -3.6]), 0.5, CHROME_FAR);
  rig.capsule(at([1.5, -1.4]), at([1.5, -3.6]), 0.5, CHROME_FAR);
  rig.capsule(at([0, -1]), at([0, -3.9]), 0.85, CHROME);
  const ribs = at([0, -6.9]);
  const top = Math.floor(ribs[1] - 3.5);
  rig.ellipse(ribs, 3.6, 3.5, CHROME, (x, y, c) => {
    const row = y - top;
    return row >= 2 && row % 2 === 0 && Math.abs(x + 0.5 - ribs[0]) > 0.9 ? BLACK : c;
  });
  rig.ellipse(farShoulder, 1.35, 1.35, CHROME);
  rig.capsule(at([-0.35, -9.8]), at([-0.35, -11.6]), 0.65, CHROME);
  skull(rig, ...at([-0.4, -14.1]));
  // The near leg.
  rig.begin(true);
  limb(near, CHROME, 1.1);
  // The far forearm, reaching round to the rifle's grip.
  rig.begin(true);
  rig.capsule(farElbow, grip, 0.8, CHROME_FAR);
  // The rifle: stock and receiver, magazine, barrel, energy cell and emitter, and the hand on
  // the grip.
  rig.begin(true);
  rig.capsule(along(-3.5), along(3.5), 1.35, GUNMETAL);
  rig.capsule(along(0.35, 1.05), along(1.05, 2.8), 0.6, GUNMETAL);
  rig.capsule(along(3.5), along(10.5), 0.8, GUNMETAL);
  rig.dot(along(1.4, -0.7), LBLUE);
  rig.dot(along(10.5), CYAN);
  rig.ellipse(grip, 0.85, 0.85, CHROME_FAR);
  // The near arm, last of all, holding the barrel.
  rig.begin(true);
  rig.ellipse(nearShoulder, 1.5, 1.5, CHROME);
  rig.capsule(nearShoulder, nearElbow, 0.9, CHROME);
  rig.capsule(nearElbow, hand, 0.8, CHROME);
  rig.ellipse(hand, 0.9, 0.9, CHROME);

  const [mx, my] = along(11.5);
  return { bitmap: rig.bitmap(), muzzle: { x: Math.round(mx), y: Math.round(my) } };
}

/* Every frame of the robot, as frames[aim][pose] with the standing pose last, each with where
   its muzzle is; and its skull on its own, which comes off when the robot is destroyed. */
export function endoskeleton() {
  const frames = ENDO_AIMS.map((_, aim) => [
    ...Array.from({ length: ENDO_POSES }, (_, k) => endoFrame(aim, k / ENDO_POSES)),
    endoFrame(aim, null),
  ]);
  const head = new Rig(9, 9);
  head.begin();
  skull(head, 4, 4.6);
  return { frames, skull: head.bitmap() };
}

// ---------- bosses ----------

/* The weak spot: an eye that opens and shuts. Frames run closed, half open, open. */
function eyeFrames(size, lid, iris) {
  const open = ball(size, size, [LGREY, LGREY, WHITE, WHITE]);
  const c = open.w / 2;
  for (let y = 0; y < open.h; y++) {
    for (let x = 0; x < open.w; x++) {
      if (!open.get(x, y) || open.get(x, y) === BLACK) continue;
      const d = Math.hypot(x + 0.5 - c, y + 0.5 - c);
      if (d < size * 0.62) open.set(x, y, iris);
      if (d < size * 0.3) open.set(x, y, BLACK);
    }
  }
  open.set(Math.floor(c - size * 0.3), Math.floor(c - size * 0.3), WHITE);
  const shut = (gap) => {
    const b = new Bitmap(open.w, open.h);
    b.blit(open, 0, 0);
    for (let y = 1; y < open.h - 1; y++) {
      const dy = Math.abs(y + 0.5 - c);
      if (dy < gap) continue;
      for (let x = 1; x < open.w - 1; x++) {
        if (b.get(x, y) && b.get(x, y) !== BLACK) b.set(x, y, dy < gap + 1 ? BLACK : lid[y < c ? 2 : 1]);
      }
    }
    return b;
  };
  return [shut(0), shut(size * 0.4), open];
}

/* A boss, from a seed and a kind: a long head with a tail, a bloated disc with two arms, or a
   winged thing. Its weak spot is an eye in its mouth. Positions are in the body bitmap's
   pixels. */
export function boss(seed, look, kind) {
  if (kind === 'c64') return c64Boss(look);
  const r = rng(seed);
  const ramp = look.bossRamp ?? look.armour;
  const s = new Sculpt(106, 75);
  let mouth;
  let guns;
  let limbs;
  if (kind === 'head') {
    const spikes = r.int(3, 5);
    for (let i = 0; i < spikes; i++) {
      const x = 46 + (i * 46) / spikes + r.range(-3, 3);
      s.tri([x - 6, -24], [x + 5, -24], [x + r.range(5, 11), -36 + r.range(0, 3)], ramp, 2);
    }
    s.ellipse(66, -17, 32, 12, ramp, { rings: 3 });
    s.ellipse(60, 0, 40, 24, ramp, { rings: 4 });
    s.ellipse(97, 0, 7, 16, ramp, { shade: -1 });
    s.tri([4, -5], [34, -22], [38, -5], look.flesh, 2);
    s.fill(24, 0, 17, 7, BLACK, false);
    for (let x = 10; x < 36; x += 5) s.dot(x, -5, WHITE);
    mouth = [24, 0];
    guns = [
      [46, -21],
      [46, 21],
    ];
    limbs = [{ at: [64, 24], angle: 2.0, length: 10 }];
  } else if (kind === 'disc') {
    s.ellipse(56, 0, 42, 32, look.flesh, { mirror: false, rings: 5 });
    for (let i = 0; i < 7; i++) {
      const a = r.range(-2.7, -0.4);
      const d = r.range(0.55, 0.88);
      s.ellipse(56 + Math.cos(a) * 42 * d, Math.sin(a) * 32 * d, r.range(5, 9), r.range(4, 7), look.flesh, {
        rings: 2,
      });
    }
    s.fill(26, 0, 10, 10, BLACK, false);
    mouth = [26, 0];
    guns = [
      [36, -24],
      [36, 24],
    ];
    limbs = [
      { at: [58, -31], angle: -2.4, length: 7 },
      { at: [58, 31], angle: 2.4, length: 7 },
    ];
  } else {
    const sweep = r.range(31, 36);
    s.tri([40, -8], [74, -8], [100, -sweep], ramp, 1);
    s.tri([52, -10], [82, -10], [104, -sweep + 12], ramp, 2);
    s.ellipse(62, 0, 26, 12, ramp, { rings: 3 });
    s.ellipse(34, 0, 10, 6, ramp);
    s.ellipse(18, 0, 11, 10, ramp);
    s.tri([0, 0], [9, -4], [9, 4], [BROWN, AMBER, YELLOW, WHITE], 2, false);
    mouth = [18, 0];
    guns = [
      [92, -sweep + 4],
      [92, sweep - 4],
    ];
    limbs = [];
  }
  const body = s.bitmap();
  const core = eyeFrames(7, ramp, look.eye);
  const m = s.at(...mouth);
  return {
    body: [body],
    core,
    coreAt: { x: m.x - core[0].w / 2, y: m.y - core[0].h / 2 },
    guns: guns.map(([x, y]) => s.at(x, y)),
    limbs: limbs.map(({ at, angle, length }) => ({ ...s.at(...at), angle, length })),
    segment: ball(5, 5, look.flesh),
  };
}

/* The last boss is a C64 screen with something growing on its frame. Its weak spot is the
   cursor, which can only be hit while it is showing. */
function c64Boss(look) {
  const r = rng(64);
  const w = 104;
  const h = 78;
  const pad = 6;
  const framed = new Bitmap(w + 2 * pad, h + 2 * pad);
  framed.fill(pad, pad, w, h, BLACK);
  framed.fill(pad + 1, pad + 1, w - 2, h - 2, LBLUE);
  framed.fill(pad + 9, pad + 9, w - 18, h - 18, BLUE);
  drawText(framed, '?SYNTAX  ERROR', pad + 11, pad + 14, LBLUE);
  drawText(framed, 'READY.', pad + 11, pad + 26, LBLUE);
  // Growths on the frame: the bugs have got into the hardware.
  for (let i = 0; i < 22; i++) {
    const g = ball(r.int(2, 5), r.int(2, 4), look.flesh);
    const onSide = r.chance(0.5);
    const x = onSide ? pad + (r.chance(0.5) ? 0 : w) : pad + r.int(0, w);
    const y = onSide ? pad + r.int(0, h) : pad + (r.chance(0.5) ? 0 : h);
    framed.blit(g, Math.round(x - g.w / 2), Math.round(y - g.h / 2));
  }
  const cursor = (on) => {
    const c = new Bitmap(8, 10);
    c.fill(0, 0, 8, 10, BLUE);
    if (on) c.fill(1, 1, 6, 8, LBLUE);
    return c;
  };
  return {
    body: [framed],
    core: [cursor(false), cursor(false), cursor(true)],
    coreAt: { x: pad + 10, y: pad + 36 },
    guns: [
      { x: pad + 4, y: pad + 12 },
      { x: pad + 4, y: pad + h - 12 },
    ],
    limbs: [],
    segment: ball(5, 5, look.flesh),
  };
}

// ---------- old computers ----------

/* The machines this all started on, standing on the floor of every stage as obstacles: an IBM
   PC, a Commodore 64 and a ZX81, each with the screen it would show (writeScreens writes on
   them, and game.js blinks their cursors). They are drawn in fat pixels, like the terrain, and
   each column is solid from its top down, because the stage raises its floor to their outline
   for the ship to crash into (see terrainLevel in stage.js). */

/* A box lit from the top left: a highlight along its top and left, shadow along its bottom
   and right, inside a black outline. The ramp runs dark to highlight. */
function box(b, x, y, w, h, [dark, mid, light, hi]) {
  b.fill(x, y, w, h, BLACK);
  b.fill(x + 1, y + 1, w - 2, h - 2, mid);
  b.fill(x + 1, y + 1, w - 2, 1, hi);
  b.fill(x + 1, y + 2, 1, h - 3, light);
  b.fill(x + 1, y + h - 2, w - 2, 1, dark);
  b.fill(x + w - 2, y + 2, 1, h - 3, dark);
}

/* A CRT's screen, its corners rounded off into the bezel. */
function crt(b, x, y, w, h, c, bezel) {
  b.fill(x, y, w, h, c);
  for (const [cx, cy] of [
    [x, y],
    [x + w - 1, y],
    [x, y + h - 1],
    [x + w - 1, y + h - 1],
  ])
    b.set(cx, cy, bezel);
}

/* Fills any gap under the top of each column with black, so what the ship sees is what it can
   hit, and measures the outline: each column's height above the base, 0 where there is
   nothing. Screens are in pixels, relative to the widened picture. */
function finish(b, screens) {
  const profile = new Int16Array(b.w);
  for (let x = 0; x < b.w; x++) {
    let top = 0;
    while (top < b.h && !b.get(x, top)) top++;
    if (top === b.h) continue;
    profile[x] = b.h - top;
    for (let y = top; y < b.h; y++) if (!b.get(x, y)) b.set(x, y, BLACK);
  }
  return { bitmap: b, profile, screens };
}

/* The IBM PC 5150, with its 5151 monitor on top, its two full-height floppy drives, the big red
   switch on its side, and the Model F keyboard in front, all in IBM's pale beige. */
function ibmPc() {
  const b = new Bitmap(40, 34);
  const CASE = [KHAKI, TAN, BEIGE, WHITE];
  box(b, 7, 0, 24, 22, CASE);
  b.fill(9, 2, 20, 16, DGREY);
  crt(b, 10, 3, 18, 14, BLACK, DGREY);
  b.set(26, 19, DGREY);
  b.set(28, 19, DGREY);
  box(b, 4, 21, 36, 13, CASE);
  for (const dx of [20, 29]) {
    box(b, dx, 23, 8, 7, [BLACK, DGREY, DGREY, GREY]);
    b.fill(dx + 1, 25, 6, 1, BLACK);
    b.fill(dx + 3, 25, 2, 1, LGREY);
  }
  b.fill(39, 24, 1, 2, RED);
  box(b, 0, 28, 27, 6, CASE);
  for (let y = 30; y <= 31; y++) {
    for (let x = 2; x <= 24; x++) b.set(x, y, (x + y) % 2 ? TAN : KHAKI);
  }
  return finish(b, [{ kind: 'ibm', x: 20, y: 3, w: 36, h: 14 }]);
}

/* The Commodore 64 in its breadbin case, with a joystick, and its 1702 monitor on a 1541 disk
   drive, in Commodore's browny beiges. */
function commodore64() {
  const b = new Bitmap(54, 28);
  const CASE = [KHAKI, TAN, BEIGE, WHITE];
  const BREAD = [DBROWN, KHAKI, TAN, BEIGE];
  // The monitor shows the C64's blue screen inside its light blue border.
  box(b, 27, 0, 22, 20, BREAD);
  b.fill(29, 2, 18, 15, DGREY);
  crt(b, 30, 3, 16, 13, LBLUE, DGREY);
  b.fill(31, 4, 14, 11, BLUE);
  b.set(42, 17, DGREY);
  b.set(44, 17, DGREY);
  // The 1541 under it: its slot with the door lever across it, and its two lights.
  box(b, 27, 19, 22, 9, CASE);
  b.fill(30, 22, 16, 1, BLACK);
  b.fill(37, 22, 2, 1, LGREY);
  b.set(30, 25, GREEN);
  b.set(32, 25, RED);
  // The breadbin: its keys, space bar, function keys and power light.
  box(b, 0, 20, 26, 8, BREAD);
  for (let y = 22; y <= 24; y++) {
    for (let x = 2; x <= 19; x++) b.set(x, y, (x + y) % 2 ? DBROWN : KHAKI);
  }
  b.fill(6, 25, 9, 1, DBROWN);
  b.fill(21, 22, 2, 1, BEIGE);
  b.fill(21, 24, 2, 1, BEIGE);
  b.set(22, 21, RED);
  // A joystick, its stick topped in red, with its fire button.
  box(b, 50, 24, 4, 4, [BLACK, DGREY, GREY, LGREY]);
  b.fill(51, 18, 1, 6, GREY);
  b.fill(51, 16, 1, 2, RED);
  b.set(52, 25, RED);
  return finish(b, [{ kind: 'c64', x: 62, y: 4, w: 28, h: 11 }]);
}

/* The Sinclair ZX81, small and black, with the 16K RAM pack on its back, in front of the
   wooden television it was plugged into, with its telescopic aerial. */
function zx81() {
  const b = new Bitmap(38, 26);
  box(b, 13, 4, 25, 22, [DBROWN, BROWN, TAN, TAN]);
  for (let y = 7; y < 23; y += 3) {
    for (let x = 15; x < 36; x++) if ((x * 7 + y * 3) % 11 < 6) b.set(x, y, DBROWN);
  }
  b.fill(15, 6, 16, 16, BLACK);
  crt(b, 16, 7, 14, 13, LGREY, BLACK);
  for (let y = 8; y <= 12; y += 2) b.fill(32, y, 4, 1, DBROWN);
  b.set(33, 16, LGREY);
  b.set(33, 19, LGREY);
  b.fill(34, 0, 1, 4, LGREY);
  b.set(34, 0, WHITE);
  box(b, 9, 17, 4, 5, [BLACK, DGREY, GREY, LGREY]);
  box(b, 0, 21, 13, 5, [BLACK, DGREY, GREY, LGREY]);
  for (let x = 2; x <= 10; x++) {
    b.set(x, 22, x % 2 ? WHITE : DGREY);
    b.set(x, 23, x % 2 ? RED : DGREY);
  }
  return finish(b, [{ kind: 'zx81', x: 32, y: 7, w: 28, h: 13 }]);
}

export const PROPS = { ibm: ibmPc, c64: commodore64, zx81 };

const drawnProps = new Map();

/* One of PROPS, drawn once and kept: { bitmap, profile, screens }. */
export function prop(kind) {
  if (!drawnProps.has(kind)) drawnProps.set(kind, PROPS[kind]());
  return drawnProps.get(kind);
}

/* The cursors that blink on the old computers' screens (game.js draws them): an underline on
   the IBM, a block on the C64. In pixels from the screen's top left. The ZX81's K cursor did
   not blink. */
export const SCREEN_CURSORS = {
  ibm: { x: 18, y: 12, w: 3, h: 1, colour: LGREEN, blink: 16 },
  c64: { x: 2, y: 7, w: 3, h: 4, colour: LBLUE, blink: 20 },
};

/* Writes what each old computer shows onto the widened terrain, in single pixels: DOS 4.0z's
   prompt on the IBM's green screen, READY. on the C64's blue one, and the ZX81's inverse K in
   the corner of its white one. `screens` are in the terrain's pixels (see terrainLevel in
   stage.js). */
export function writeScreens(b, screens) {
  const write = (str, x, y, colour) => {
    for (const ch of str) {
      MICRO[ch]?.split(' ').forEach((row, j) => {
        for (let i = 0; i < MICRO_W; i++) if (row[i] === '#') b.set(x + i, y + j, colour);
      });
      x += MICRO_W + 1;
    }
  };
  for (const s of screens) {
    if (s.kind === 'ibm') {
      write('DOS 4.0Z', s.x + 2, s.y + 1, LGREEN);
      write('C:\\>', s.x + 2, s.y + 7, LGREEN);
    } else if (s.kind === 'c64') {
      write('READY.', s.x + 2, s.y + 1, LBLUE);
    } else {
      b.fill(s.x + 1, s.y + s.h - 8, 5, 7, BLACK);
      write('K', s.x + 2, s.y + s.h - 7, WHITE);
    }
  }
}

// ---------- terrain ----------

/* Draws a stage's terrain in fat pixels from its heights (in rows, per fat column): machinery
   lit from above, built of panels with pipes, ribs, grilles and vents in them, plus the domes,
   tanks and old computers the stage generator put on it. The machinery follows the ground,
   the floor without the computers on it. */
export function terrain(level, look) {
  const { floor, ceil, domes, tanks } = level;
  const ground = level.ground ?? floor;
  const cols = floor.length;
  const [dark, mid, light, hi] = look.rock;
  const b = new Bitmap(cols, PLAY_H);
  const r = rng(level.seed);
  const top = (x) => PLAY_H - ground[clamp(x, 0, cols - 1)];
  const bottom = (x) => ceil[clamp(x, 0, cols - 1)];

  // Panels: runs of columns of random widths, cut into bands by depth, each with a pattern.
  const panelOf = new Int32Array(cols);
  const panelX = new Int32Array(cols);
  const panelW = [];
  for (let x = 0; x < cols; ) {
    const w = r.int(5, 14);
    for (let i = 0; i < w && x + i < cols; i++) {
      panelOf[x + i] = panelW.length;
      panelX[x + i] = i;
    }
    panelW.push(w);
    x += w;
  }
  const BANDS = [0, 9, 19, 31];
  const PATTERNS = [
    // pipes lying along
    (i, j) => [light, mid, dark, BLACK][j % 4],
    // one fat pipe, with joints
    (i, j, w, h) =>
      i % 6 === 0 ? BLACK : i % 6 === 1 ? light : [light, light, mid, mid, dark, dark, BLACK][Math.min(6, Math.floor((j * 7) / h))],
    // ribs
    (i) => [light, mid, dark][i % 3],
    // a grille
    (i, j) => ((i + j) % 2 ? BLACK : dark),
    // a bolted plate
    (i, j) => (i % 3 === 1 && j % 3 === 1 ? light : mid),
    // a vent
    (i, j, w, h) => (j > 0 && j < h - 1 && i > 0 && i < w - 1 ? (j % 2 ? BLACK : dark) : mid),
  ];
  const patternOf = (panel, band) =>
    (Math.imul((panel * 73856093) ^ (band * 19349663) ^ level.seed, 2654435761) >>> 0) % PATTERNS.length;
  const inside = (x, d) => {
    if (d >= BANDS[3]) return d > 40 || (x + d) % 2 ? BLACK : dark;
    const band = d >= BANDS[2] ? 2 : d >= BANDS[1] ? 1 : 0;
    const j = d - BANDS[band];
    const i = panelX[x];
    if (i === 0 || j === 0) return BLACK;
    if (j === 1) return light;
    const w = panelW[panelOf[x]];
    const h = BANDS[band + 1] - BANDS[band];
    return PATTERNS[patternOf(panelOf[x], band)](i - 1, j - 2, w - 1, h - 2);
  };

  for (let x = 0; x < cols; x++) {
    const t = top(x);
    for (let y = t; y < PLAY_H; y++) {
      const dTop = y - t;
      let dSide = 9;
      let side = 0;
      for (let i = 1; i <= 2 && !side; i++) {
        if (top(x - i) > y) {
          dSide = i - 1;
          side = -1;
        } else if (top(x + i) > y) {
          dSide = i - 1;
          side = 1;
        }
      }
      let c;
      if (side && dSide < dTop) c = side < 0 ? (dSide ? mid : light) : dSide ? dark : BLACK;
      else if (dTop < 4) c = [hi, light, mid, BLACK][dTop];
      else c = inside(x, dTop - 4);
      b.set(x, y, c);
    }
    const bt = bottom(x);
    for (let y = 0; y < bt; y++) {
      const dBot = bt - 1 - y;
      let c;
      if (bottom(x - 1) <= y) c = mid;
      else if (bottom(x + 1) <= y) c = BLACK;
      else if (dBot < 4) c = [mid, dark, BLACK, dark][dBot];
      else c = inside(x, dBot - 4);
      b.set(x, y, c);
    }
  }

  // Lights set into the walls, here and there.
  for (let x = 6; x < cols; x += r.int(14, 30)) {
    const y = top(x) + r.int(6, 10);
    if (y < PLAY_H - 2 && panelX[x] > 1) b.set(x, y, look.light);
    const yc = bottom(x) - r.int(6, 10);
    if (yc > 2 && panelX[x] > 1) b.set(x, yc, r.chance(0.3) ? RED : look.light);
  }

  for (const dome of domes) {
    for (let x = dome.x - dome.r; x <= dome.x + dome.r; x++) {
      for (let y = dome.base - dome.h; y < dome.base; y++) {
        const dx = (x - dome.x) / (dome.r + 0.5);
        const dy = (dome.base - y) / dome.h;
        const d2 = dx * dx + dy * dy;
        if (d2 > 1) continue;
        const lum = -dx * 0.5 + dy * 0.7 + Math.sqrt(1 - d2) * 0.4;
        let c = lum > 0.95 ? hi : lum > 0.6 ? light : lum > 0.25 ? mid : dark;
        if (d2 > 0.82) c = BLACK;
        else if ((dome.base - y) % 4 === 0 && lum < 0.95) c = dark;
        b.set(x, y, c);
      }
    }
    b.set(dome.x - 1, dome.base - dome.h + 2, look.light);
  }

  // Tanks and pillars: ribbed cylinders, lit down one side.
  const CYLINDER = [dark, mid, light, light, mid, dark, dark];
  for (const tank of tanks) {
    const w = tank.x1 - tank.x0;
    const from = tank.fromTop ? 0 : PLAY_H - tank.h;
    const to = tank.fromTop ? tank.h : PLAY_H;
    for (let x = tank.x0; x < tank.x1; x++) {
      const shade = CYLINDER[Math.min(6, Math.floor(((x - tank.x0) / w) * 7))];
      for (let y = from; y < to; y++) {
        const ring = (tank.fromTop ? tank.h - 1 - y : y - from) % 7;
        let c = ring === 0 ? BLACK : ring === 1 ? (shade === dark ? mid : light) : shade;
        if (x === tank.x0 || x === tank.x1 - 1) c = BLACK;
        b.set(x, y, c);
      }
    }
  }

  for (const p of level.props ?? []) {
    const { bitmap } = prop(p.kind);
    b.blit(bitmap, p.x, PLAY_H - p.base - bitmap.h);
  }
  return b;
}

/* Distant structures for the parallax layer behind the terrain, wrapping sideways so the strip
   tiles: low spires, domes and masts, and the silhouettes of old computers with their screens
   glowing, in two dark colours with a few lights. No towers, so it reads as hulks in space
   rather than a city. In fat pixels. */
export function skyline(seed, colours, w = 160, h = 40) {
  const r = rng(seed);
  const b = new Bitmap(w, h);
  const put = (x, y, c) => b.set(((x % w) + w) % w, y, c);
  const rect = (x, y, rw, rh, c) => {
    for (let j = 0; j < rh; j++) for (let i = 0; i < rw; i++) put(x + i, y + j, c);
  };
  [colours[0], colours[1]].forEach((c, layer) => {
    // The near rank's screens glow in the stage's light colour, the far rank's more dimly.
    const glow = layer ? colours[2] : colours[1];
    let x = r.int(0, 6);
    const tall = layer === 0 ? 1 : 0.55;
    while (x < w) {
      const kind = r.pick(['spire', 'dome', 'dome', 'mast', 'monitor', 'pc', 'breadbin', 'joystick']);
      let bw;
      if (kind === 'monitor') {
        bw = r.int(7, 11);
        const bh = Math.round(bw * 1.4 * (layer ? 0.75 : 1));
        rect(x, h - bh, bw, bh, c);
        rect(x + 1, h - bh + 1, bw - 2, bh - 4, glow);
      } else if (kind === 'pc') {
        bw = r.int(11, 14);
        const mw = bw - 4;
        const mh = Math.round(mw * 1.3 * (layer ? 0.75 : 1));
        rect(x, h - 4, bw, 4, c);
        rect(x + 1, h - 4 - mh, mw, mh, c);
        rect(x + 2, h - 3 - mh, mw - 2, mh - 3, glow);
      } else if (kind === 'breadbin') {
        bw = r.int(10, 14);
        rect(x, h - 3, bw, 3, c);
        rect(x + 1, h - 4, bw - 2, 1, c);
        if (layer) put(x + bw - 3, h - 3, RED);
      } else if (kind === 'joystick') {
        bw = 4;
        rect(x, h - 3, 4, 3, c);
        rect(x + 1, h - 9, 1, 6, c);
        put(x + 1, h - 10, layer ? RED : c);
      } else {
        bw = kind === 'mast' ? 1 : kind === 'dome' ? r.int(6, 14) : r.int(3, 7);
        const bh = Math.round(r.int(8, 38) * tall);
        for (let i = 0; i < bw; i++) {
          let colH = bh;
          if (kind === 'spire') colH = Math.round(bh * (1 - Math.abs(i - (bw - 1) / 2) / (bw / 2 + 0.5)));
          if (kind === 'dome') colH = Math.round((bh / 2) * Math.sqrt(Math.max(0, 1 - ((i - (bw - 1) / 2) / (bw / 2)) ** 2)));
          for (let y = h - colH; y < h; y++) put(x + i, y, c);
        }
        if (layer === 1 && colours[2]) {
          for (let k = 0; k < bw; k++) {
            if (r.chance(0.2)) put(x + r.int(0, bw - 1), h - r.int(2, Math.max(3, bh - 2)), colours[2]);
          }
          if (kind === 'mast') put(x, h - bh - 1, RED);
        }
      }
      x += bw + r.int(1, layer ? 10 : 4);
    }
  });
  return b;
}

// ---------- small things ----------

/* A power-up capsule with its letter on it. */
export function capsule(colour, letter) {
  const b = new Bitmap(11, 11);
  b.fill(1, 0, 9, 11, BLACK);
  b.fill(0, 1, 11, 9, BLACK);
  b.fill(1, 1, 9, 9, colour);
  b.fill(2, 1, 6, 1, WHITE);
  drawGlyph(b, letter, 3, 2, BLACK);
  return b;
}

/* Enemy bullets: small hot orbs that flicker between two colours, so they read against any
   background. */
export function bulletFrames() {
  const orb = (rim, core) => {
    const b = new Bitmap(5, 5);
    b.fill(1, 0, 3, 5, rim);
    b.fill(0, 1, 5, 3, rim);
    b.fill(1, 1, 3, 3, core);
    b.set(1, 1, WHITE);
    return b;
  };
  return [orb(RED, PINK), orb(PINK, WHITE)];
}

/* A pod that rides along with the ship, with a light that blinks. */
export function podFrames() {
  const a = ball(4, 4, [DBLUE, BLUE, LBLUE, WHITE]);
  const b = ball(4, 4, [DBLUE, BLUE, LBLUE, WHITE]);
  b.fill(4, 4, 2, 2, RED);
  return [a, b];
}
