/* What each stage is and where things happen in it: its name, colours and boss, the shape of
   its terrain, and the waves of enemies along the way. Pure functions of the stage number,
   like art.js, so a stage plays the same every time and can be checked outside a browser. */

import {
  AMBER,
  BLUE,
  BROWN,
  CYAN,
  DBLUE,
  DGREY,
  GREEN,
  GREY,
  LBLUE,
  LGREEN,
  LGREY,
  NAVY,
  NIGHT,
  PINK,
  PLAY_H,
  PLAY_W,
  PURPLE,
  RED,
  WHITE,
  YELLOW,
  noise1,
  prop,
  rng,
} from './art.js';

/* Five stages, one for each stop on the page's journey back in time, each in the colours of
   its part of the page: today's greys, Teams purple, amber trading screens, Windows 95 grey and
   cyan, and the C64's own blue at the end. Bosses are named after the bugs of each era. After
   the fifth the loop starts again, harder. */
export const STAGES = [
  {
    name: 'THE CLOUD',
    boss: 'THE HALLUCINATION',
    kind: 'head',
    seed: 2026,
    look: {
      sky: NIGHT,
      stars: [DGREY, GREY, LGREY, WHITE],
      skyline: [NAVY, DBLUE, GREY],
      rock: [DGREY, GREY, LGREY, WHITE],
      light: CYAN,
      flesh: [DGREY, RED, PINK, WHITE],
      armour: [DBLUE, BLUE, LBLUE, WHITE],
      eye: YELLOW,
      glow: WHITE,
    },
  },
  {
    name: 'ELECTRON',
    boss: 'THE MEMORY HOG',
    kind: 'disc',
    seed: 2018,
    look: {
      sky: NIGHT,
      stars: [DBLUE, PURPLE, LBLUE, WHITE],
      skyline: [NAVY, DBLUE, PURPLE],
      rock: [DBLUE, PURPLE, LBLUE, WHITE],
      light: PINK,
      flesh: [DGREY, GREEN, LGREEN, WHITE],
      armour: [DGREY, GREY, LGREY, WHITE],
      eye: RED,
      glow: PINK,
    },
  },
  {
    name: 'WALL STREET',
    boss: 'THE BLACK SWAN',
    kind: 'wing',
    seed: 2007,
    look: {
      sky: NIGHT,
      stars: [BROWN, AMBER, YELLOW, WHITE],
      skyline: [NAVY, DGREY, AMBER],
      rock: [DGREY, BROWN, AMBER, YELLOW],
      light: LGREEN,
      flesh: [DBLUE, BLUE, LBLUE, WHITE],
      armour: [DGREY, GREY, LGREY, WHITE],
      bossRamp: [NAVY, DGREY, GREY, LGREY],
      eye: RED,
      glow: LGREEN,
    },
  },
  {
    name: 'SHAREWARE',
    boss: 'THE BLUE SCREEN',
    kind: 'head',
    seed: 1998,
    look: {
      sky: NIGHT,
      stars: [DGREY, GREY, CYAN, WHITE],
      skyline: [NAVY, DGREY, CYAN],
      rock: [DGREY, GREY, CYAN, WHITE],
      light: YELLOW,
      flesh: [BROWN, AMBER, YELLOW, WHITE],
      armour: [DBLUE, BLUE, LBLUE, WHITE],
      eye: RED,
      glow: WHITE,
    },
  },
  {
    name: 'COMMODORE 64',
    boss: '?SYNTAX ERROR',
    kind: 'c64',
    seed: 1982,
    look: {
      sky: DBLUE,
      stars: [BLUE, BLUE, LBLUE, WHITE],
      skyline: [NAVY, NIGHT, BLUE],
      rock: [NAVY, BLUE, LBLUE, WHITE],
      light: YELLOW,
      flesh: [BROWN, AMBER, YELLOW, WHITE],
      armour: [DGREY, GREY, LGREY, WHITE],
      eye: RED,
      glow: WHITE,
    },
  },
];

/* Rows the ship always has between the floor and the ceiling. */
export const MIN_GAP = 76;

/* Fat columns of the boss arena, which is flat, at the end of every stage. */
const ARENA = PLAY_W / 2 + 40;

/* How hard stage `index` is, from 0 for the first stage of the first loop. */
export function difficulty(index) {
  return {
    bulletSpeed: 1.25 + 0.14 * index,
    fireRate: 1 + 0.22 * index,
    toughness: Math.floor(index / 2),
    gap: Math.max(78, 150 - 12 * index),
  };
}

/* The terrain heights for a stage, in rows per fat column, with the domes, tanks and old
   computers on it and the flat stretches where turrets can hang. `ground` is the floor without
   the computers, which art.js draws its machinery to; `floor` is what the ship, shots and
   robots meet. `length` is how far the stage scrolls before its boss, in pixels. */
export function terrainLevel(seed, length) {
  const r = rng(seed);
  const cols = Math.ceil((length + PLAY_W) / 2) + 8;
  const floor = new Int16Array(cols);
  const ceil = new Int16Array(cols);
  const n = [noise1(r, 22), noise1(r, 31), noise1(r, 17), noise1(r, 27)];
  const shapes = {
    open: (x) => [8 + 10 * n[0](x), 0],
    hills: (x) => [10 + 46 * n[1](x), n[2](x) > 0.6 ? 6 : 0],
    corridor: (x) => [16 + 28 * n[0](x), 16 + 28 * n[2](x)],
    cavern: (x) => [8 + 52 * n[1](x), 8 + 48 * n[3](x)],
    pillars: () => [10, 8],
  };

  // A plan of sections, flat at both ends: a quiet start, and the boss arena.
  const plan = [{ from: 0, kind: 'flat' }];
  let x = 150;
  const arenaStart = Math.floor(length / 2) + 10;
  while (x < arenaStart - 60) {
    plan.push({ from: x, kind: r.pick(['open', 'hills', 'hills', 'corridor', 'corridor', 'cavern', 'pillars']) });
    x += r.int(60, 130);
  }
  plan.push({ from: arenaStart - 40, kind: 'flat' });
  const sectionAt = (col) => {
    let s = plan[0];
    for (const p of plan) if (p.from <= col) s = p;
    return s;
  };

  // A stretch of level floor kept for each of the old computers, one of each kind in a
  // shuffled order, spread through the stage, with the ceiling held up out of the way.
  const order = ['ibm', 'c64', 'zx81'];
  for (let i = order.length - 1; i > 0; i--) {
    const j = r.int(0, i);
    [order[i], order[j]] = [order[j], order[i]];
  }
  const sites = order.map((kind, i) => ({ kind, x: Math.round(arenaStart * (0.24 + 0.26 * i)) + r.int(-24, 24), w: prop(kind).bitmap.w }));
  const kept = (col, pad) => sites.some((s) => col >= s.x - pad && col < s.x + s.w + pad);

  const raw = [];
  for (let col = 0; col < cols; col++) {
    const s = sectionAt(col);
    const [f, c] = s.kind === 'flat' ? [10, col >= arenaStart - 40 ? 8 : 0] : shapes[s.kind](col);
    raw.push(kept(col, 8) ? [10, Math.min(c, 30)] : [f, c]);
  }
  // Ease between sections, then step the heights into terraces, the way machinery is built.
  for (let col = 0; col < cols; col++) {
    let f = 0;
    let c = 0;
    let k = 0;
    for (let i = -6; i <= 6; i++) {
      const p = raw[Math.min(cols - 1, Math.max(0, col + i))];
      f += p[0];
      c += p[1];
      k++;
    }
    floor[col] = Math.round(f / k / 3) * 3;
    ceil[col] = Math.round(c / k / 3) * 3;
  }

  const domes = [];
  const tanks = [];
  const room = (col) => PLAY_H - MIN_GAP - floor[col] - ceil[col];
  for (let col = 170; col < arenaStart - 60; col++) {
    const s = sectionAt(col);
    if (kept(col, 12)) continue;
    if ((s.kind === 'open' || s.kind === 'hills') && r.chance(0.03)) {
      const rad = r.int(4, 8);
      const base = PLAY_H - Math.max(floor[col - rad], floor[col], floor[col + rad]);
      const h = Math.min(Math.round(rad * 1.8), room(col));
      if (h < 5) continue;
      domes.push({ x: col, r: rad, h, base });
      for (let i = -rad; i <= rad; i++) {
        const top = base - Math.round(h * Math.sqrt(Math.max(0, 1 - (i / (rad + 0.5)) ** 2)));
        floor[col + i] = Math.max(floor[col + i], PLAY_H - top);
      }
      col += rad * 3;
    } else if ((s.kind === 'pillars' && r.chance(0.08)) || (s.kind === 'corridor' && r.chance(0.015))) {
      const w = r.int(4, 7);
      const fromTop = r.chance(0.5);
      let space = Infinity;
      for (let i = 0; i < w; i++) space = Math.min(space, room(col + i));
      const grow = Math.min(r.int(24, 60), space);
      if (grow < 10) continue;
      let h = 0;
      for (let i = 0; i < w; i++) h = Math.max(h, (fromTop ? ceil : floor)[col + i] + grow);
      tanks.push({ x0: col, x1: col + w, h, fromTop });
      for (let i = 0; i < w; i++) (fromTop ? ceil : floor)[col + i] = h;
      col += w + r.int(14, 30);
    }
  }

  // The rule the ship relies on: there is always room to get through.
  for (let col = 0; col < cols; col++) {
    const over = floor[col] + ceil[col] - (PLAY_H - MIN_GAP);
    if (over > 0) {
      const cut = Math.ceil(over / 2);
      floor[col] = Math.max(0, floor[col] - cut);
      ceil[col] = Math.max(0, ceil[col] - (over - cut));
    }
  }

  // The old computers stand on their stretches, each on a plinth that levels it. The floor then
  // rises to their outline, so they are as solid as the hull; `ground` keeps the floor without
  // them, for the machinery under them.
  const ground = Int16Array.from(floor);
  const props = [];
  const screens = [];
  for (const { kind, x } of sites) {
    const { bitmap, profile, screens: lit } = prop(kind);
    const base = Math.max(...floor.subarray(x, x + bitmap.w));
    for (let i = 0; i < bitmap.w; i++) {
      ground[x + i] = base;
      floor[x + i] = base + profile[i];
    }
    props.push({ kind, x, base });
    for (const s of lit) screens.push({ ...s, x: 2 * x + s.x, y: PLAY_H - base - bitmap.h + s.y });
  }

  // Flat runs of at least six fat columns, where turrets can hang.
  const ledges = [];
  for (const [side, h] of [
    ['floor', floor],
    ['ceil', ceil],
  ]) {
    let start = 0;
    for (let col = 1; col <= cols; col++) {
      if (col === cols || h[col] !== h[start]) {
        if (col - start >= 6 && h[start] > 0) ledges.push({ side, x0: start * 2, x1: col * 2, h: h[start] });
        start = col;
      }
    }
  }
  ledges.sort((a, b) => a.x0 - b.x0);
  return { seed, cols, floor, ceil, ground, domes, tanks, props, screens, ledges };
}

/* The whole of stage `index`: its look, its terrain, and its waves, each with the scroll
   position (in pixels) at which it arrives. */
export function stage(index) {
  const info = STAGES[index % STAGES.length];
  const loop = Math.floor(index / STAGES.length);
  const seed = info.seed + loop * 7919;
  const r = rng(seed ^ 0x5eed);
  const hard = difficulty(index);
  const length = 2700 + 150 * Math.min(index, 6);
  const level = terrainLevel(seed, length);

  // The open air at a pixel column: the band between ceiling and floor, with a margin.
  const air = (px, margin = 14) => {
    const col = Math.max(0, Math.min(level.cols - 1, Math.floor(px / 2)));
    return [level.ceil[col] + margin, PLAY_H - level.floor[col] - margin];
  };
  const flyingY = (px) => {
    const [lo, hi] = air(px, 18);
    return Math.round(r.range(lo, Math.max(lo, hi)));
  };

  // Robots march along the floor, so they need no ledge; turrets hang from the ceiling, and
  // where there is no ceiling to hang from, robots come instead. The second wave of every
  // stage is robots, so they are never long in coming.
  const waves = [];
  const kinds = ['swarm', 'swarm', 'darters', 'spores', 'snake', 'robots', 'turrets'];
  if (index >= 1) kinds.push('rear', 'darters');
  if (index >= 2) kinds.push('snake', 'robots');
  for (let at = 300; at < length - 260; at += r.int(hard.gap, hard.gap + 70)) {
    const enter = at + PLAY_W;
    let kind = waves.length === 1 ? 'robots' : r.pick(kinds);
    const ledges = level.ledges.filter((l) => l.x1 > enter && l.x0 < enter + 160);
    if (kind === 'turrets' && !ledges.some((l) => l.side === 'ceil')) kind = 'robots';
    const count = kind === 'swarm' ? r.int(5, 7) : kind === 'robots' ? r.int(2, 2 + Math.min(2, index)) : r.int(2, 4);
    waves.push({ at, kind, y: flyingY(enter), ledges, count });
  }
  for (let at = 520; at < length - 300; at += r.int(560, 720)) {
    waves.push({ at, kind: 'carrier', y: flyingY(at + PLAY_W), count: 1 });
  }
  const mid = Math.round((length * r.range(0.5, 0.62)) / 10) * 10;
  waves.push({ at: mid, kind: 'cruiser', y: Math.round((air(mid + PLAY_W)[0] + air(mid + PLAY_W)[1]) / 2), count: 1 });
  waves.sort((a, b) => a.at - b.at);

  return {
    index,
    loop,
    seed,
    info,
    look: info.look,
    hard,
    length,
    level,
    waves,
    bossHp: 110 + 45 * index,
  };
}
