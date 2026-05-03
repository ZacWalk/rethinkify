/* The enemies: what each kind is worth, how it moves, how it is drawn, and how each kind of wave
   arrives. Positions are screen pixels at the sprite's centre. Things that stand on the
   terrain also keep a world position, so they scroll with it. */

import { ENDO_AIMS, ENDO_H, ENDO_POSES, ENDO_STRIDE, ENDO_W, GREY, LBLUE, LGREY, WHITE } from './art.js';
import { CSS, PH, W, blit } from './screen.js';

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const rand = (lo, hi) => lo + Math.random() * (hi - lo);
const angleDiff = (a, b) => Math.atan2(Math.sin(a - b), Math.cos(a - b));

/* How fast a robot walks, in pixels a frame, and how far it walks between one pose of its
   stride and the next. */
const ROBOT_SPEED = 0.3;
const POSE_STEP = ENDO_STRIDE / ENDO_POSES;

/* Each kind's toughness, points, hit box (half width and half height), how it moves (one of
   MOVES), and how many frames each picture of its animation lasts. */
export const KINDS = {
  drifter: { hp: 1, score: 100, hw: 8, hh: 6, anim: 8, move: 'sine' },
  darter: { hp: 2, score: 200, hw: 12, hh: 5, move: 'darter' },
  spore: { hp: 2, score: 150, hw: 7, hh: 7, anim: 14, move: 'drift', burst: true },
  head: { hp: 8, score: 500, hw: 9, hh: 7, move: 'snake' },
  segment: { hp: 3, score: 100, hw: 5, hh: 4, move: 'follow' },
  turret: { hp: 4, score: 300, hw: 6, hh: 5, move: 'turret' },
  robot: { hp: 6, score: 400, hw: 5, hh: 14, move: 'robot' },
  cruiser: { hp: 40, score: 2000, hw: 27, hh: 10, move: 'cruiser', big: true },
  carrier: { hp: 3, score: 500, hw: 13, hh: 7, anim: 10, move: 'drift', drop: true },
  rear: { hp: 2, score: 200, hw: 12, hh: 5, move: 'rear', sprite: 'darter', flip: true },
};

export const MOVES = {
  // Formation flyers: a sine wave, taking the odd potshot.
  sine(game, e) {
    e.x += e.vx;
    e.y = e.y0 + Math.sin(e.t * e.freq + e.phase) * e.amp;
    if (Math.random() < 0.0022 * game.st.hard.fireRate) game.aim(e);
  },

  // Darts in fast, brakes, takes aim, fires, then peels away.
  darter(game, e) {
    if (e.stage === 0) {
      e.x += e.vx;
      if (e.x < e.stopX + 40) e.vx = Math.min(-0.3, e.vx * 0.93);
      if (e.x <= e.stopX) {
        e.stage = 1;
        e.wait = 50;
      }
    } else if (e.stage === 1) {
      e.x -= 0.3;
      if (--e.wait === 25) game.aim(e, 1.15);
      if (e.wait <= 0) {
        e.stage = 2;
        e.vx = -0.5;
        e.vy = e.y < PH / 2 ? 1.1 : -1.1;
      }
    } else {
      e.vx = Math.max(-3.2, e.vx - 0.07);
      e.x += e.vx;
      e.y += e.vy;
    }
  },

  // Spores and carriers: a slow bob across the screen.
  drift(game, e) {
    e.x += e.vx;
    e.y = e.y0 + Math.sin(e.t * 0.05 + e.phase) * 6;
  },

  // A snake's head weaves, leaving a trail its body follows.
  snake(game, e) {
    e.x += e.vx;
    e.y = e.y0 + Math.sin(e.t * 0.035 + e.phase) * 28;
    e.trail.unshift([e.x, e.y]);
    if (e.trail.length > 160) e.trail.length = 160;
    if (--e.reload <= 0) {
      e.reload = Math.round(120 / game.st.hard.fireRate);
      game.aim(e);
    }
  },

  // Each link of the body is where the head was a few frames before. Once the head has left the
  // screen its trail stops, so the body swims on after it instead.
  follow(game, e) {
    if (e.head.gone) {
      e.x += e.head.vx;
      return;
    }
    const trail = e.head.trail;
    [e.x, e.y] = trail[Math.min(trail.length - 1, e.index * 8)];
  },

  // Hangs from the ceiling and turns towards the ship a little at a time, never aiming into
  // the ceiling, and fires.
  turret(game, e) {
    const p = game.player;
    const want = Math.atan2(p.y + 7 - e.y, p.x + 16 - e.x);
    e.aim += clamp(angleDiff(want, e.aim), -0.04, 0.04);
    e.aim = clamp(e.aim, 0.15, Math.PI - 0.15);
    if (--e.reload <= 0 && e.x > 8 && e.x < W - 8) {
      e.reload = Math.round(rand(90, 130) / game.st.hard.fireRate);
      game.shoot(e.x + Math.cos(e.aim) * 9, e.y + Math.sin(e.aim) * 9, e.aim, game.st.hard.bulletSpeed, true);
    }
  },

  // Marches along the floor towards the ship, never up a wall or off a drop, and halts to fire,
  // raising its rifle a notch at a time to follow the ship. It keeps a world position, and its
  // picture moves a pose's step at a time, so a planted foot stays put on the moving floor.
  robot(game, e) {
    const L = game.st.level;
    const p = game.player;
    if (e.turnWait > 0) e.turnWait--;
    if (e.firing > 0) e.firing--;

    // Face the ship while it is about; turn back from a wall, a drop or another robot when it
    // is not. Turning keeps the picture where it was.
    const sx = e.wx - game.scrollX;
    const seen = sx > 12 && sx < W - 12;
    const facing = p.alive && seen && Math.abs(p.x + 16 - sx) > 16 ? Math.sign(p.x + 16 - sx) : 0;
    let open = canStep(game, e);
    if (!e.firing && !e.turnWait && (facing ? facing !== e.dir : !open)) {
      const lag = e.walked % POSE_STEP;
      e.wx -= e.dir * lag;
      e.walked -= lag;
      e.dir = -e.dir;
      e.turnWait = 40;
      open = canStep(game, e);
    }
    const walking = open && !e.firing;
    if (walking) {
      e.wx += e.dir * ROBOT_SPEED;
      e.walked += ROBOT_SPEED;
    }
    e.vx = (walking ? e.dir * ROBOT_SPEED : 0) - game.scrollSpeed;
    e.pose = walking ? Math.floor(e.walked / POSE_STEP) % ENDO_POSES : ENDO_POSES;
    e.groundY = PH - footing(L, e.wx);
    e.x = e.wx - game.scrollX;
    e.y = e.groundY - 15;
    e.left = Math.round(e.wx - e.dir * (e.walked % POSE_STEP) - ENDO_W / 2) - Math.floor(game.scrollX);
    e.top = e.groundY - ENDO_H + 1;

    // The rifle follows the ship's height from the chest, and fires only forwards.
    const dx = p.x + 16 - e.x;
    const elevation = Math.atan2(e.groundY - 19 - (p.y + 7), Math.abs(dx));
    if (e.t % 8 === 0 && !(e.firing > 0 && e.firing <= 20)) {
      let best = 0;
      ENDO_AIMS.forEach((a, i) => {
        if (Math.abs(a - elevation) < Math.abs(ENDO_AIMS[best] - elevation)) best = i;
      });
      e.aim += Math.sign(best - e.aim);
    }
    const inSight = p.alive && e.x > 12 && e.x < W - 12 && dx * e.dir > 12 && elevation > -0.3 && elevation < 1.3;
    if (--e.reload <= 0 && !e.firing && inSight) {
      e.firing = 40;
      e.reload = Math.round(rand(150, 200) / game.st.hard.fireRate);
    }
    if (e.firing === 20) {
      const [mx, my] = muzzle(e);
      const a = ENDO_AIMS[e.aim];
      const up = clamp(elevation, a - 0.15, a + 0.15);
      game.shoot(mx, my, e.dir < 0 ? up - Math.PI : -up, game.st.hard.bulletSpeed * 1.1).plasma = true;
      game.sound.sfx('zap');
    }
  },

  // A gunship: comes in, slows to a crawl, fires spreads, and smokes once it is hurt.
  cruiser(game, e) {
    e.x += e.vx;
    if (e.x < W - 60) e.vx = -0.2;
    if (--e.reload <= 0 && e.x < W - 10) {
      e.reload = Math.round(80 / game.st.hard.fireRate);
      for (const offset of [-0.25, 0, 0.25]) game.aim(e, 1, offset);
    }
    if (e.hp < e.max / 2 && e.t % 6 === 0) game.smoke(e.x + rand(-20, 20), e.y + rand(-6, 6));
  },

  // Comes from behind, after a warning, and fires once it has passed.
  rear(game, e) {
    if (e.delay > 0) {
      e.delay--;
      return;
    }
    e.x += e.vx;
    e.y += Math.sin(e.t * 0.08) * 0.5;
    if (!e.fired && e.x > W * 0.6) {
      e.fired = true;
      game.aim(e);
    }
  },
};

const floorAt = (L, wx) => L.floor[clamp(Math.floor(wx / 2), 0, L.cols - 1)];

/* The floor under a robot's feet at world x, in rows: the highest it reaches beneath them. */
function footing(L, wx) {
  let rows = 0;
  for (let x = wx - 3; x <= wx + 3; x += 2) rows = Math.max(rows, floorAt(L, x));
  return rows;
}

/* Whether a robot can step the way it faces: no wall in reach of its rifle that is too high to
   step up, no drop under its front foot too deep to step down, and no robot in the way. */
function canStep(game, e) {
  const L = game.st.level;
  const under = footing(L, e.wx);
  for (let k = 1; k <= 9; k++) {
    const rows = floorAt(L, e.wx + e.dir * k);
    if (rows > under + 6 || (k <= 4 && rows < under - 6)) return false;
  }
  return !game.enemies.some((o) => o !== e && o.kind === 'robot' && !o.dead && (o.wx - e.wx) * e.dir > 0 && Math.abs(o.wx - e.wx) < 16);
}

/* Where a robot's rifle fires from, on screen. The pictures face left, so facing right
   mirrors it. */
function muzzle(e) {
  const m = e.sprites[e.aim][ENDO_POSES].muzzle;
  return [e.dir < 0 ? e.left + m.x : e.left + ENDO_W - 1 - m.x, e.top + m.y];
}

/* Sets a robot down at world x or a little after it, on bare floor with room to walk: not on
   an old computer, a dome or a pillar. Returns it, or null if there is nowhere before `limit`. */
function placeRobot(game, wx, limit) {
  const L = game.st.level;
  for (; wx < limit; wx += 4) {
    let lo = Infinity;
    let hi = 0;
    let bare = true;
    for (let x = wx - 9; x <= wx + 9; x += 2) {
      const col = clamp(Math.floor(x / 2), 0, L.cols - 1);
      lo = Math.min(lo, L.floor[col]);
      hi = Math.max(hi, L.floor[col]);
      bare &&= L.floor[col] === L.ground[col];
    }
    if (bare && hi - lo <= 6) {
      return game.addEnemy('robot', wx - game.scrollX, PH, {
        wx,
        dir: -1,
        aim: 0,
        pose: ENDO_POSES,
        walked: 0,
        firing: 0,
        turnWait: 0,
        reload: Math.round(rand(40, 80)),
      });
    }
  }
  return null;
}

/* Brings in one of the stage's waves (see stage.js) just off the right edge of the screen. */
export function spawnWave(game, w) {
  const n = w.count;
  const add = (kind, x, y, extra) => game.addEnemy(kind, x, y, extra);
  switch (w.kind) {
    case 'swarm': {
      const group = { size: n, killed: 0 };
      const amp = rand(14, 30);
      for (let i = 0; i < n; i++) add('drifter', W + 14 + i * 16, w.y, { y0: w.y, vx: -1.15, amp, freq: 0.045, phase: i * 0.55, group });
      break;
    }
    case 'darters':
      for (let i = 0; i < n; i++) {
        add('darter', W + 16 + i * 22, clamp(w.y + (i - (n - 1) / 2) * 20, 12, PH - 12), { vx: -3.4, stopX: rand(170, 250), stage: 0 });
      }
      break;
    case 'spores':
      for (let i = 0; i <= n; i++) {
        const y = clamp(w.y + rand(-24, 24), 14, PH - 14);
        add('spore', W + 12 + i * 34, y, { y0: y, vx: -0.55, phase: rand(0, 6) });
      }
      break;
    case 'snake': {
      // The trail starts as if the snake had been swimming in from off screen.
      const trail = Array.from({ length: 160 }, (_, k) => [W + 14 + k * 0.95, w.y]);
      const head = add('head', W + 14, w.y, { y0: w.y, vx: -0.95, phase: 0, trail, segments: [], reload: 70 });
      const length = 8 + Math.min(4, game.stageIndex);
      for (let i = 1; i <= length; i++) head.segments.push(add('segment', W + 14 + i * 7.6, w.y, { head, index: i }));
      break;
    }
    case 'turrets': {
      // Turrets hang from the ceiling; on the floor, robots stand guard instead.
      let placed = 0;
      for (const ledge of w.ledges) {
        if (placed >= n) break;
        if (ledge.side === 'floor') {
          if (placeRobot(game, Math.max(ledge.x0 + 8, game.scrollX + W + 12), ledge.x1 + 40)) placed++;
          continue;
        }
        const wx = clamp(game.scrollX + W + rand(10, 120), ledge.x0 + 8, ledge.x1 - 8);
        if (wx < game.scrollX + W + 6) continue;
        add('turret', wx - game.scrollX, ledge.h + 5, { ground: true, aim: Math.PI / 2 + 0.8, reload: Math.round(rand(40, 90)) });
        placed++;
      }
      break;
    }
    case 'robots': {
      let wx = game.scrollX + W + 12;
      for (let i = 0; i < n; i++) {
        const robot = placeRobot(game, wx, wx + 140);
        if (!robot) break;
        wx = robot.wx + rand(22, 32);
      }
      break;
    }
    case 'cruiser':
      add('cruiser', W + 34, clamp(w.y, 26, PH - 26), { vx: -0.7, reload: 60 });
      break;
    case 'carrier':
      add('carrier', W + 16, w.y, { y0: w.y, vx: -0.7, phase: 0 });
      break;
    case 'rear':
      game.warnings.push({ y: w.y, t: 0 });
      for (let i = 0; i < 3; i++) add('rear', -20 - i * 24, clamp(w.y + (i - 1) * 18, 12, PH - 12), { vx: 2.4, delay: 70 });
      break;
  }
}

/* Draws an enemy, with a turret's barrel where it has one. */
export function drawEnemy(g, e) {
  if (e.delay > 0) return;
  if (e.kind === 'robot') {
    drawRobot(g, e);
    return;
  }
  const frames = e.sprites;
  const spr = frames[e.anim ? Math.floor(e.t / e.anim) % frames.length : 0];
  const img = e.flash > 0 ? spr.flash : spr.img;
  if (e.kind === 'turret') {
    for (let i = 0; i <= 9; i++) {
      g.fillStyle = CSS[i === 9 ? WHITE : i % 3 === 0 ? GREY : LGREY];
      g.fillRect(Math.round(e.x + Math.cos(e.aim) * i) - 1, Math.round(e.y + Math.sin(e.aim) * i) - 1, 2, 2);
    }
  }
  blit(g, img, Math.round(e.x - spr.w / 2), Math.round(e.y - spr.h / 2), e.flip);
}

/* A robot in its pose, with the flash of its rifle as it fires: a white-hot star that shrinks
   back to the muzzle. */
function drawRobot(g, e) {
  const spr = e.sprites[e.aim][e.pose];
  blit(g, e.flash > 0 ? spr.flash : spr.img, e.left, e.top, e.dir > 0);
  if (e.firing > 14 && e.firing <= 20) {
    const [x, y] = muzzle(e);
    const r = e.firing > 17 ? 2 : 1;
    g.fillStyle = CSS[LBLUE];
    g.fillRect(x - r, y, 2 * r + 1, 1);
    g.fillRect(x, y - r, 1, 2 * r + 1);
    g.fillStyle = CSS[WHITE];
    g.fillRect(x - r + 1, y, 2 * r - 1, 1);
    g.fillRect(x, y - r + 1, 1, 2 * r - 1);
  }
}
