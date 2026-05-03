/* Drawing on the game's canvas: Bitmaps from art.js made into canvases, text in the game's own
   font, and a few shapes, all on whole pixels so the scaled-up screen stays crisp. */

import * as art from './art.js';

/* The canvas: the playfield is the top W x PH, and the HUD the strip below it. */
export const W = art.PLAY_W;
export const PH = art.PLAY_H;
export const H = 180;

/* The palette as CSS colours, for fillStyle. */
export const CSS = art.PALETTE.map((c) => (c ? `rgb(${c.join(',')})` : 'transparent'));

const RGBA = art.PALETTE.map((c) => (c ? (0xff000000 | (c[2] << 16) | (c[1] << 8) | c[0]) >>> 0 : 0));

export function canvasOf(bitmap) {
  const c = document.createElement('canvas');
  c.width = bitmap.w;
  c.height = bitmap.h;
  const g = c.getContext('2d');
  const img = g.createImageData(bitmap.w, bitmap.h);
  const px = new Uint32Array(img.data.buffer);
  for (let i = 0; i < bitmap.px.length; i++) px[i] = RGBA[bitmap.px[i]];
  g.putImageData(img, 0, 0);
  return c;
}

/* A Bitmap ready to draw, with its white silhouette for the flash when it is hit. */
export function sprite(bitmap) {
  return {
    bitmap,
    img: canvasOf(bitmap),
    flash: canvasOf(bitmap.silhouette(art.WHITE)),
    w: bitmap.w,
    h: bitmap.h,
  };
}

/* Draws a canvas with its top left at (x, y), mirrored left to right if `flip`. */
export function blit(g, img, x, y, flip = false) {
  if (!flip) {
    g.drawImage(img, x, y);
    return;
  }
  g.save();
  g.translate(x + img.width, y);
  g.scale(-1, 1);
  g.drawImage(img, 0, 0);
  g.restore();
}

const fonts = new Map();
function font(colour, bold) {
  const key = colour * 2 + (bold ? 1 : 0);
  if (!fonts.has(key)) fonts.set(key, canvasOf(art.fontAtlas(colour, bold)));
  return fonts.get(key);
}

export function textWidth(str, bold = false, scale = 1) {
  return (String(str).length * art.advance(bold) - 1) * scale;
}

/* Text in the game's font, with a drop shadow so it reads over anything. */
export function text(g, str, x, y, colour = art.WHITE, { bold = false, align = 'left', scale = 1, shadow = true } = {}) {
  str = String(str).toUpperCase();
  const cell = art.advance(bold);
  if (align === 'center') x -= Math.floor(textWidth(str, bold, scale) / 2);
  else if (align === 'right') x -= textWidth(str, bold, scale);
  const run = (c, offset) => {
    const atlas = font(c, bold);
    let cx = Math.round(x) + offset;
    for (const ch of str) {
      const i = art.CHARSET.indexOf(ch);
      if (i >= 0 && ch !== ' ') {
        g.drawImage(atlas, i * cell, 0, cell, art.GLYPH_H, cx, Math.round(y) + offset, cell * scale, art.GLYPH_H * scale);
      }
      cx += cell * scale;
    }
  };
  if (shadow) run(art.BLACK, scale);
  run(colour, 0);
}

/* A filled circle on whole pixels. */
export function disc(g, cx, cy, r, colour) {
  g.fillStyle = CSS[colour];
  cx = Math.round(cx);
  cy = Math.round(cy);
  for (let dy = -r; dy <= r; dy++) {
    const half = Math.floor(Math.sqrt(r * r - dy * dy));
    g.fillRect(cx - half, cy + dy, 2 * half + 1, 1);
  }
}

const circles = [];

/* The pixels of a circle's outline, by the midpoint method, cached by radius. */
export function circlePoints(r) {
  if (!circles[r]) {
    const pts = [];
    let x = r;
    let y = 0;
    let err = 1 - r;
    while (x >= y) {
      pts.push([x, y], [y, x], [-y, x], [-x, y], [-x, -y], [-y, -x], [y, -x], [x, -y]);
      y++;
      if (err < 0) err += 2 * y + 1;
      else {
        x--;
        err += 2 * (y - x) + 1;
      }
    }
    circles[r] = pts;
  }
  return circles[r];
}

/* The playfield with every other pixel in black, laid over the game to dim it, the way an
   8-bit machine would. The HUD below it stays clear. */
export function ditherCanvas() {
  const b = new art.Bitmap(W, PH);
  for (let y = 0; y < PH; y++) for (let x = y & 1; x < W; x += 2) b.px[y * W + x] = art.BLACK;
  return canvasOf(b);
}
