/* The game itself: the player's ship and its weapons, the stage scrolling past, the enemies
   and their bullets, power-ups, explosions, and the run of a game from GET READY to the high
   score table. arcade.js owns the screen, and calls update() sixty times a second and
   render() once a frame.

   The ship is placed by its top left corner, as it is drawn; everything else by its centre. */

import * as art from './art.js';
import { stage as makeStage } from './stage.js';
import { CSS, H, PH, W, blit, canvasOf, circlePoints, disc, ditherCanvas, sprite, text } from './screen.js';
import { KINDS, MOVES, drawEnemy, spawnWave } from './cast.js';
import { Boss } from './boss.js';
import { drawCards, drawHud } from './hud.js';

const { AMBER, BLUE, BROWN, CYAN, DGREY, GREY, LBLUE, LGREEN, LGREY, PINK, RED, WHITE, YELLOW } = art;

/* How fast the stage scrolls, in pixels a frame: R-Type's stately pace. */
const SCROLL = 0.5;

/* Where the high score table is kept. boot.js reads it too, to put the high score on the
   start button. */
export const SCORES_KEY = 'rethinkify-scores';

const FIRE = [WHITE, YELLOW, YELLOW, AMBER, AMBER, RED, BROWN, DGREY];
const WORLD = new Set(['ready', 'play', 'warning', 'boss', 'clear', 'over']);
const PAUSABLE = new Set(['ready', 'play', 'warning', 'boss', 'clear']);

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const rand = (lo, hi) => lo + Math.random() * (hi - lo);
const pick = (list) => list[Math.floor(Math.random() * list.length)];

/* A table to beat on the first game, signed by the chips inside a C64. */
const DEFAULT_SCORES = [
  { name: 'ZAC', score: 50000, stage: 5 },
  { name: 'SID', score: 40000, stage: 4 },
  { name: 'VIC', score: 30000, stage: 3 },
  { name: 'CIA', score: 20000, stage: 2 },
  { name: 'PLA', score: 10000, stage: 1 },
];

function loadScores() {
  try {
    const saved = JSON.parse(localStorage.getItem(SCORES_KEY) ?? 'null');
    const valid = Array.isArray(saved) ? saved.filter((s) => typeof s?.name === 'string' && Number.isFinite(s?.score)) : [];
    if (valid.length) return valid.slice(0, 5);
  } catch {
    // Storage may be off, or hold something else; the default table will do.
  }
  return DEFAULT_SCORES.map((s) => ({ ...s }));
}

function saveScores(scores) {
  try {
    localStorage.setItem(SCORES_KEY, JSON.stringify(scores));
  } catch {
    // Not saved this time, which only matters to the player's pride.
  }
}

/* Power-ups: S speeds the ship up, W adds guns, B adds a pod, and * is a bonus. */
const PICKUPS = {
  S: { colour: YELLOW, name: 'SPEED UP', stat: 'speed', max: 3 },
  W: { colour: LGREEN, name: 'WEAPON UP', stat: 'weapon', max: 4 },
  B: { colour: LBLUE, name: 'POD', stat: 'pods', max: 2 },
  '*': { colour: PINK, name: '1000' },
};

export class Game {
  constructor(g, input, sound, calm) {
    this.g = g;
    this.input = input;
    this.sound = sound;
    this.calm = calm;
    this.scores = loadScores();
    this.ship = sprite(art.ship());
    this.podSprites = art.podFrames().map(sprite);
    this.bulletSprites = art.bulletFrames().map(sprite);
    this.capsules = Object.fromEntries(Object.entries(PICKUPS).map(([k, v]) => [k, sprite(art.capsule(v.colour, k))]));
    // The robots are the same in every stage: chrome, after Terminator 2's endoskeletons.
    const endo = art.endoskeleton();
    this.robot = endo.frames.map((poses) => poses.map((f) => ({ ...sprite(f.bitmap), muzzle: f.muzzle })));
    this.skull = sprite(endo.skull);
    this.dither = ditherCanvas();
    this.t = 0;
    this.paused = false;
    this.shake = 0;
    this.muzzle = 0;
    this.onMute = null;
    this.newGame();
  }

  // ---------- the run of a game ----------

  newGame() {
    this.score = 0;
    this.lives = 2;
    this.nextLife = 20000;
    this.newEntry = null;
    this.respawnIn = 0;
    this.player = { x: -40, y: PH / 2 - 6, alive: true, speed: 0, weapon: 1, pods: 0, orbit: 0, charge: 0, held: 0, cool: 0, invuln: 0, flyIn: true };
    this.startStage(0);
  }

  startStage(index) {
    const st = makeStage(index);
    this.st = st;
    this.stageIndex = index;
    const cast = art.enemySet(st.seed, st.look);
    this.sprites = Object.fromEntries(Object.entries(cast).map(([name, frames]) => [name, frames.map(sprite)]));
    this.sprites.robot = this.robot;
    const ground = art.terrain(st.level, st.look).widen();
    art.writeScreens(ground, st.level.screens);
    this.terrain = canvasOf(ground);
    this.skyline = canvasOf(art.skyline(st.seed, st.look.skyline).widen());
    this.bossArt = art.boss(st.seed, st.look, st.info.kind);
    this.stars = Array.from({ length: 90 }, (_, i) => {
      const layer = i < 50 ? 0 : i < 78 ? 1 : 2;
      return {
        x: rand(0, W),
        y: rand(0, PH),
        v: [0.1, 0.3, 0.75][layer] * rand(0.8, 1.2),
        c: st.look.stars[layer === 0 ? Math.floor(rand(0, 2)) : layer + 1],
        layer,
        twinkle: Math.floor(rand(0, 200)),
      };
    });
    this.scrollX = 0;
    this.scrollSpeed = 0;
    this.waveAt = 0;
    this.enemies = [];
    this.bullets = [];
    this.shots = [];
    this.beams = [];
    this.pickups = [];
    this.particles = [];
    this.popups = [];
    this.warnings = [];
    this.boss = null;
    this.noMiss = true;
    this.bonus = 0;
    Object.assign(this.player, { x: -40, y: PH / 2 - 6, alive: true, flyIn: true, invuln: 120, charge: 0, held: 0 });
    this.enter('ready');
    this.sound.tune('ready', st.seed, { tune: 'stage', seed: st.seed });
  }

  enter(state) {
    this.state = state;
    this.stateT = 0;
  }

  update() {
    const input = this.input;
    input.poll();
    if (input.took('mute') && this.state !== 'initials') this.onMute?.(this.sound.toggleMute());
    if (PAUSABLE.has(this.state) && (input.took('pause') || (this.paused && input.tapped))) this.setPaused(!this.paused);
    if (!this.paused) {
      this.t++;
      this.stateT++;
      if (WORLD.has(this.state)) this.updateWorld();
      this.advance();
    }
    input.endFrame();
  }

  setPaused(on) {
    if (on === this.paused || (on && !PAUSABLE.has(this.state))) return;
    this.paused = on;
    if (on) this.sound.pause();
    else this.sound.resume();
  }

  /* Moves the game on from one part to the next. */
  advance() {
    const t = this.stateT;
    switch (this.state) {
      case 'ready':
        if (t >= 150) this.enter('play');
        break;
      case 'play':
        if (this.scrollX >= this.st.length) {
          this.enter('warning');
          this.sound.silence();
          this.sound.sfx('warn');
        }
        break;
      case 'warning':
        if (t >= 200) {
          this.boss = new Boss(this, this.bossArt);
          this.enter('boss');
          this.sound.tune('boss', this.st.seed);
        }
        break;
      case 'boss':
        if (this.boss.phase === 'gone') this.clearStage();
        break;
      case 'clear':
        if (t >= (this.stageIndex % 5 === 4 ? 420 : 330)) this.startStage(this.stageIndex + 1);
        break;
      case 'over':
        if (t >= 300 || (t > 60 && this.input.confirmed())) this.afterGameOver();
        break;
      case 'initials':
        this.updateInitials();
        break;
      case 'scores':
        if (t > 30 && this.input.confirmed()) this.newGame();
        break;
    }
  }

  clearStage() {
    this.enter('clear');
    this.bonus = this.noMiss ? 5000 : 0;
    if (this.bonus) this.addScore(this.bonus);
    for (const e of this.enemies) this.explode(e.x, e.y, 1);
    this.enemies = [];
    this.bullets = [];
    this.sound.charge(0);
    this.sound.tune('clear', this.st.seed);
  }

  gameOver() {
    this.enter('over');
    this.respawnIn = 0;
    this.sound.charge(0);
    this.sound.tune('over', this.st.seed);
  }

  afterGameOver() {
    const last = this.scores[this.scores.length - 1];
    if (this.score > 0 && (this.scores.length < 5 || this.score > last.score)) {
      // Initials take a keyboard or a pad; on a touch screen the score is simply yours.
      if (this.input.touched) this.record('YOU');
      else {
        this.entry = { letters: ['A', 'A', 'A'], pos: 0 };
        this.enter('initials');
      }
    } else this.enter('scores');
  }

  record(name) {
    this.newEntry = { name, score: this.score, stage: this.stageIndex + 1 };
    this.scores.push(this.newEntry);
    this.scores.sort((a, b) => b.score - a.score);
    this.scores.length = Math.min(5, this.scores.length);
    saveScores(this.scores);
    this.enter('scores');
  }

  /* Three letters, arcade style: up and down change one, fire moves on; or type them. */
  updateInitials() {
    const e = this.entry;
    const input = this.input;
    const ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!? ';
    const next = () => {
      this.sound.sfx('select');
      if (++e.pos > 2) this.record(e.letters.join('').trim() || 'ACE');
    };
    if (input.typed.length) {
      for (const ch of input.typed) {
        if (this.state !== 'initials') break;
        e.letters[e.pos] = ch;
        next();
      }
      return;
    }
    if (input.took('up') || input.took('down')) {
      const i = ALPHABET.indexOf(e.letters[e.pos]);
      e.letters[e.pos] = ALPHABET[(i + (input.took('up') ? 1 : -1) + ALPHABET.length) % ALPHABET.length];
      this.sound.sfx('select');
    }
    if (input.took('left') && e.pos > 0) e.pos--;
    if (input.took('right') || input.took('fire')) next();
  }

  // ---------- the world ----------

  updateWorld() {
    const st = this.st;
    const moving = (this.state === 'ready' || this.state === 'play') && this.scrollX < st.length;
    this.scrollSpeed = moving ? SCROLL : 0;
    this.scrollX = Math.min(st.length, this.scrollX + this.scrollSpeed);
    while (this.waveAt < st.waves.length && st.waves[this.waveAt].at <= this.scrollX) spawnWave(this, st.waves[this.waveAt++]);
    for (const s of this.stars) {
      s.x -= s.v * (moving ? 2 : 1);
      if (s.x < 0) {
        s.x += W;
        s.y = rand(0, PH);
      }
    }
    if (this.shake > 0) this.shake--;
    this.updatePlayer();
    this.updateShots();
    this.updateEnemies();
    this.boss?.update();
    this.updateBullets();
    this.updatePickups();
    this.updateEffects();
    this.collide();
  }

  /* Whether a point on screen is inside the terrain. */
  solid(x, y) {
    const L = this.st.level;
    const col = Math.floor((this.scrollX + x) / 2);
    if (col < 0 || col >= L.cols) return false;
    return y >= PH - L.floor[col] || y < L.ceil[col];
  }

  updatePlayer() {
    const p = this.player;
    const input = this.input;
    if (!p.alive) {
      if (this.respawnIn > 0 && --this.respawnIn === 0) this.respawn();
      return;
    }
    const leaving = this.state === 'clear';
    if (p.flyIn) {
      p.x += 1.5;
      if (p.x >= 36) p.flyIn = false;
    } else if (leaving) {
      p.x += Math.min(5, 1 + this.stateT * 0.05);
    } else {
      const speed = 1.25 + 0.35 * p.speed;
      const m = input.move();
      p.x = clamp(p.x + m.x * speed + input.drag.x, 0, W - 32);
      p.y = clamp(p.y + m.y * speed + input.drag.y, 1, PH - 13);
    }
    if (p.invuln > 0) p.invuln--;
    p.orbit += 0.05;

    // Holding fire shoots and charges the beam; letting go fires the beam if it has charge.
    const firing = input.fire && !p.flyIn && !leaving && this.state !== 'over';
    if (firing) {
      if (p.cool <= 0) {
        this.volley();
        p.cool = p.weapon >= 4 ? 6 : 7;
      }
      if (++p.held > 12) p.charge = Math.min(1, p.charge + 1 / 70);
    } else {
      if (p.charge >= 0.3) this.fireBeam(p.charge);
      p.charge = 0;
      p.held = 0;
    }
    p.cool--;
    this.sound.charge(p.charge);
    if (p.charge > 0.05 && this.t % 2 === 0) {
      // Sparks drawn into the nose while the beam charges.
      const a = rand(0, Math.PI * 2);
      const d = rand(10, 18);
      this.particles.push({
        kind: 'spark',
        x: p.x + 31 + Math.cos(a) * d,
        y: p.y + 7 + Math.sin(a) * d,
        vx: (-Math.cos(a) * d) / 8,
        vy: (-Math.sin(a) * d) / 8,
        drag: 1,
        life: 0,
        max: 8,
        c: pick([LBLUE, WHITE, CYAN]),
      });
    }
    if (!leaving && !p.flyIn) this.checkTerrain(p);
  }

  checkTerrain(p) {
    const hull = [
      [p.x + 28, p.y + 7],
      [p.x + 10, p.y + 3],
      [p.x + 12, p.y + 10],
      [p.x + 3, p.y + 7],
    ];
    if (!hull.some(([x, y]) => this.solid(x, y))) return;
    if (p.invuln > 0) {
      // Still shielded after a respawn: nudge the ship back into the open rather than lose it.
      const L = this.st.level;
      const col = clamp(Math.floor((this.scrollX + p.x + 16) / 2), 0, L.cols - 1);
      p.y += Math.sign((L.ceil[col] + PH - L.floor[col]) / 2 - 6 - p.y) * 2;
    } else this.killPlayer();
  }

  volley() {
    const p = this.player;
    const x = p.x + 30;
    const y = p.y + 7;
    const add = (dx, dy, vx, vy) => this.shots.push({ x: x + dx, y: y + dy, vx, vy, dmg: 1 });
    if (p.weapon === 1) add(0, 0, 6, 0);
    else if (p.weapon === 2) {
      add(0, -3, 6, 0);
      add(0, 3, 6, 0);
    } else if (p.weapon === 3) {
      add(0, -3, 6, 0);
      add(0, 3, 6, 0);
      add(-4, 0, 5.6, -1.6);
      add(-4, 0, 5.6, 1.6);
    } else {
      add(0, 0, 6.5, 0);
      add(0, -5, 6.5, 0);
      add(0, 5, 6.5, 0);
      add(-4, 0, 5.8, -1.8);
      add(-4, 0, 5.8, 1.8);
    }
    for (const pod of this.podSpots()) this.shots.push({ x: pod.x + 5, y: pod.y, vx: 5, vy: 0, dmg: 1, pod: true });
    this.muzzle = 3;
    this.sound.sfx('shot');
  }

  /* Pods circle the ship, mostly above and below it, and soak up bullets. */
  podSpots() {
    const p = this.player;
    return Array.from({ length: p.alive ? p.pods : 0 }, (_, i) => {
      const a = p.orbit + i * Math.PI;
      return { x: p.x + 14 + Math.cos(a) * 6, y: p.y + 6 + Math.sin(a) * 17 };
    });
  }

  fireBeam(charge) {
    const p = this.player;
    this.beams.push({ x: p.x + 28, y: p.y + 7, h: Math.round(4 + 12 * charge), len: 0, dmg: Math.round(3 + 14 * charge), hit: new Set(), t: 0 });
    this.sound.sfx('beam');
    if (charge >= 1) this.shake = Math.max(this.shake, 4);
  }

  updateShots() {
    for (const s of this.shots) {
      s.x += s.vx;
      s.y += s.vy;
      if (s.x > W + 8 || s.y < -8 || s.y > PH + 8) s.dead = true;
      else if (this.solid(s.x, s.y)) {
        s.dead = true;
        this.spark(s.x, s.y, GREY, 2);
      }
    }
    this.shots = this.shots.filter((s) => !s.dead);
    for (const b of this.beams) {
      b.x += 7;
      b.len = Math.min(52, b.len + 7);
      b.t++;
    }
    this.beams = this.beams.filter((b) => b.x - b.len < W + 4);
    if (this.muzzle > 0) this.muzzle--;
  }

  addEnemy(kind, x, y, extra = {}) {
    const k = KINDS[kind];
    const hp = k.hp > 1 ? k.hp + this.st.hard.toughness * (k.big ? 8 : 1) : k.hp;
    const e = { kind, x, y, vx: 0, vy: 0, t: 0, flash: 0, ...k, hp, max: hp, sprites: this.sprites[k.sprite ?? kind], ...extra };
    this.enemies.push(e);
    return e;
  }

  updateEnemies() {
    for (const e of this.enemies) {
      e.t++;
      if (e.flash > 0) e.flash--;
      if (e.ground) e.x -= this.scrollSpeed;
      MOVES[e.move](this, e);
      if (e.dying > 0 && --e.dying === 0) this.killEnemy(e);
      const off = e.vx > 0 ? e.x > W + 60 : e.x < -70;
      if (off || e.y < -50 || e.y > PH + 50) e.gone = true;
    }
    this.enemies = this.enemies.filter((e) => !e.dead && !e.gone);
    for (const w of this.warnings) w.t++;
    this.warnings = this.warnings.filter((w) => w.t < 80);
  }

  /* An enemy shot at the ship, if it is on screen and not point blank. */
  aim(e, speed = 1, offset = 0) {
    const p = this.player;
    if (!p.alive || e.x > W - 6 || e.x < 10) return;
    const dx = p.x + 16 - e.x;
    const dy = p.y + 7 - e.y;
    if (dx * dx + dy * dy < 1600) return;
    this.shoot(e.x, e.y, Math.atan2(dy, dx) + offset, this.st.hard.bulletSpeed * speed, true);
  }

  shoot(x, y, angle, speed, noisy = false, glyph = null) {
    const b = { x, y, vx: Math.cos(angle) * speed, vy: Math.sin(angle) * speed, glyph };
    this.bullets.push(b);
    if (noisy) this.sound.sfx('enemy');
    return b;
  }

  damage(e, dmg) {
    if (e.dead || e.dying) return;
    e.hp -= dmg;
    e.flash = 3;
    if (e.hp <= 0) this.killEnemy(e);
    else this.sound.sfx('hit');
  }

  killEnemy(e) {
    if (e.dead) return;
    e.dead = true;
    this.addScore(e.score, e.x, e.y, e.score >= 300);
    this.explode(e.x, e.y, e.big ? 3 : 1);
    this.sound.sfx(e.big ? 'boom' : 'pop');
    if (e.burst) {
      const n = 6 + Math.min(4, this.stageIndex);
      for (let i = 0; i < n; i++) this.shoot(e.x, e.y, (i / n) * Math.PI * 2 + e.t * 0.1, this.st.hard.bulletSpeed * 0.7);
    }
    e.segments?.forEach((s, i) => {
      if (!s.dead) s.dying = 6 + i * 5;
    });
    if (e.kind === 'robot') this.wreck(e);
    if (e.drop || e.big) this.drop(e.x, e.y);
    if (e.group && ++e.group.killed === e.group.size) {
      if (Math.random() < 0.5) this.drop(e.x, e.y);
      else this.addScore(1000, e.x, e.y - 8, true);
    }
  }

  updateBullets() {
    const pods = this.podSpots();
    for (const b of this.bullets) {
      b.x += b.vx;
      b.y += b.vy;
      if (b.x < -8 || b.x > W + 8 || b.y < -8 || b.y > PH + 8 || this.solid(b.x, b.y)) b.dead = true;
      else if (pods.some((q) => Math.abs(q.x - b.x) < 6 && Math.abs(q.y - b.y) < 6)) {
        b.dead = true;
        this.spark(b.x, b.y, LBLUE, 3);
      }
    }
    this.bullets = this.bullets.filter((b) => !b.dead);
  }

  /* Drops a power-up, chosen from what the ship still lacks. */
  drop(x, y) {
    const p = this.player;
    const wants = [];
    if (p.weapon < 4) wants.push('W', 'W');
    if (p.pods < 2) wants.push('B');
    if (p.speed < 3) wants.push('S');
    this.pickups.push({ x, y, y0: y, type: wants.length ? pick(wants) : '*', t: 0 });
  }

  updatePickups() {
    const p = this.player;
    for (const k of this.pickups) {
      k.t++;
      k.x -= 0.6;
      k.y = k.y0 + Math.sin(k.t * 0.08) * 4;
      if (p.alive && Math.abs(k.x - (p.x + 16)) < 16 && Math.abs(k.y - (p.y + 7)) < 10) {
        k.dead = true;
        this.collect(k.type, k.x, k.y);
      } else if (k.x < -12) k.dead = true;
    }
    this.pickups = this.pickups.filter((k) => !k.dead);
  }

  collect(type, x, y) {
    const p = this.player;
    const kind = PICKUPS[type];
    this.sound.sfx('power');
    if (kind.stat && p[kind.stat] < kind.max) {
      p[kind.stat]++;
      this.popup(kind.name, x, y);
    } else {
      const points = type === '*' ? 1000 : 500;
      this.addScore(points, x, y, true);
    }
  }

  addScore(points, x = 0, y = 0, show = false) {
    this.score += points;
    if (show) this.popup(String(points), x, y);
    if (this.score >= this.nextLife) {
      this.lives++;
      this.nextLife += this.nextLife < 60000 ? 40000 : 60000;
      this.sound.sfx('oneup');
      this.popup('1UP', this.player.x + 16, this.player.y - 4);
    }
  }

  killPlayer() {
    const p = this.player;
    if (!p.alive || p.invuln > 0) return;
    p.alive = false;
    this.explode(p.x + 16, p.y + 7, 3);
    this.sound.sfx('die');
    this.sound.charge(0);
    this.noMiss = false;
    this.respawnIn = 150;
    // Losing a ship costs a little of everything, but not all of it.
    p.weapon = Math.max(1, p.weapon - 1);
    p.speed = Math.max(0, p.speed - 1);
    p.pods = Math.max(0, p.pods - 1);
    p.charge = 0;
    p.held = 0;
  }

  respawn() {
    if (this.lives === 0) {
      this.gameOver();
      return;
    }
    this.lives--;
    const L = this.st.level;
    const col = clamp(Math.floor((this.scrollX + 40) / 2), 0, L.cols - 1);
    const y = clamp((L.ceil[col] + PH - L.floor[col]) / 2 - 6, 1, PH - 13);
    Object.assign(this.player, { alive: true, flyIn: true, x: -36, y, invuln: 150 });
    for (const b of this.bullets) this.spark(b.x, b.y, LGREY, 1);
    this.bullets = [];
  }

  collide() {
    for (const s of this.shots) {
      for (const e of this.enemies) {
        if (e.dead || e.dying || e.delay > 0) continue;
        if (Math.abs(s.x - e.x) < e.hw + 3 && Math.abs(s.y - e.y) < e.hh + 2) {
          s.dead = true;
          this.spark(s.x, s.y, WHITE, 2);
          this.damage(e, s.dmg);
          break;
        }
      }
      if (!s.dead && this.boss?.hitByShot(s)) s.dead = true;
    }
    this.shots = this.shots.filter((s) => !s.dead);
    for (const b of this.beams) {
      const x0 = b.x - b.len;
      const y0 = b.y - b.h / 2 - 2;
      const y1 = b.y + b.h / 2 + 2;
      for (const e of this.enemies) {
        if (e.dead || e.dying || e.delay > 0 || b.hit.has(e)) continue;
        if (e.x + e.hw > x0 && e.x - e.hw < b.x && e.y + e.hh > y0 && e.y - e.hh < y1) {
          b.hit.add(e);
          this.damage(e, b.dmg);
        }
      }
      this.boss?.hitByBeam(b);
    }
    const p = this.player;
    if (!p.alive || p.invuln > 0 || this.state === 'clear') return;
    const px = p.x + 16;
    const py = p.y + 7;
    const rammed = this.enemies.some((e) => !e.dead && !(e.delay > 0) && Math.abs(px - e.x) < e.hw + 10 && Math.abs(py - e.y) < e.hh + 3);
    const shot = this.bullets.some((b) => Math.abs(px - b.x) < 5 && Math.abs(py - b.y) < 3.5);
    if (rammed || shot || this.boss?.touches(px, py)) this.killPlayer();
  }

  // ---------- effects ----------

  explode(x, y, size = 1) {
    if (this.particles.length > 700) return;
    for (let i = 0; i < 8 + size * 8; i++) {
      const a = rand(0, Math.PI * 2);
      const v = rand(0.3, 1 + size * 0.7);
      this.particles.push({
        kind: 'fire',
        x,
        y,
        vx: Math.cos(a) * v,
        vy: Math.sin(a) * v,
        life: 0,
        max: Math.round(rand(14, 22 + size * 10)),
        size: Math.random() < 0.3 ? 2 : 1,
      });
    }
    this.particles.push({ kind: 'ring', x, y, r: 2, grow: 0.8 + size * 0.5, life: 0, max: 8 + size * 4 });
    if (size >= 2) this.shake = Math.max(this.shake, 4 + size * 2);
  }

  spark(x, y, c = YELLOW, n = 3) {
    for (let i = 0; i < n; i++) {
      this.particles.push({ kind: 'spark', x, y, vx: rand(-1.6, 0.4), vy: rand(-1.2, 1.2), life: 0, max: Math.round(rand(5, 10)), c });
    }
  }

  smoke(x, y) {
    this.particles.push({ kind: 'smoke', x, y, vx: -0.3, vy: -0.3, drag: 1, life: 0, max: 30, c: pick([DGREY, GREY]) });
  }

  /* A robot comes apart: a second blast in its chest, scrap flying, and its skull thrown clear
     behind it. */
  wreck(e) {
    this.explode(e.x, e.y - 6, 1);
    this.spark(e.x, e.y, LGREY, 8);
    this.particles.push({ kind: 'skull', x: e.x, y: e.y - 11, vx: -e.dir * rand(0.4, 1), vy: -2.2, life: 0, max: 170, flip: e.dir > 0 });
  }

  /* A skull falls, bounces off the floor with a tink until it lies still, and moves with the
     world. */
  bounce(q) {
    q.x -= this.scrollSpeed;
    if (q.x < -12) q.life = q.max;
    if (q.still) return;
    q.x += q.vx;
    q.vy += 0.15;
    q.y += q.vy;
    const L = this.st.level;
    const { h } = this.skull;
    const rest = PH - L.floor[clamp(Math.floor((this.scrollX + q.x) / 2), 0, L.cols - 1)] - (h - 1 - (h >> 1));
    if (q.y <= rest) return;
    if (q.y - rest > 8) {
      // Into the side of something: knocked back.
      q.x -= q.vx;
      q.vx *= -0.5;
      return;
    }
    q.y = rest;
    q.vy *= -0.45;
    q.vx *= 0.7;
    if (q.vy > -0.6) q.still = true;
    else this.sound.sfx('tink');
  }

  popup(str, x, y) {
    this.popups.push({ str, x, y, t: 0 });
  }

  updateEffects() {
    for (const q of this.particles) {
      q.life++;
      if (q.kind === 'skull') this.bounce(q);
      else if (q.vx !== undefined) {
        q.x += q.vx;
        q.y += q.vy;
        const drag = q.drag ?? 0.95;
        q.vx *= drag;
        q.vy *= drag;
      }
      if (q.kind === 'ring') q.r += q.grow;
    }
    this.particles = this.particles.filter((q) => q.life < q.max);
    for (const u of this.popups) {
      u.t++;
      u.y -= 0.35;
    }
    this.popups = this.popups.filter((u) => u.t < 50);
  }

  // ---------- drawing ----------

  render() {
    const g = this.g;
    g.fillStyle = CSS[this.st.look.sky];
    g.fillRect(0, 0, W, H);
    g.save();
    g.beginPath();
    g.rect(0, 0, W, PH);
    g.clip();
    if (this.shake > 0 && !this.calm) g.translate(Math.round(rand(-2, 2)), Math.round(rand(-2, 2)));
    this.drawBackdrop(g);
    for (const k of this.pickups) {
      const c = this.capsules[k.type];
      if (k.t % 24 < 20) g.drawImage(c.img, Math.round(k.x - c.w / 2), Math.round(k.y - c.h / 2));
    }
    this.boss?.draw(g);
    for (const e of this.enemies) drawEnemy(g, e);
    this.drawShots(g);
    this.drawPlayer(g);
    this.drawBullets(g);
    this.drawEffects(g);
    g.restore();
    drawHud(g, this);
    drawCards(g, this);
  }

  drawBackdrop(g) {
    for (const s of this.stars) {
      if (s.layer === 0 && (this.t + s.twinkle) % 160 < 6) continue;
      g.fillStyle = CSS[s.c];
      g.fillRect(Math.round(s.x), Math.round(s.y), s.layer === 2 ? 2 : 1, 1);
    }
    const sky = this.skyline;
    const off = Math.floor(this.scrollX * 0.3) % sky.width;
    for (let x = -off; x < W; x += sky.width) g.drawImage(sky, x, PH - sky.height - 6);
    g.drawImage(this.terrain, Math.floor(this.scrollX), 0, W, PH, 0, 0, W, PH);
    // The old computers' cursors, blinking on their screens.
    const left = Math.floor(this.scrollX);
    for (const s of this.st.level.screens) {
      const c = art.SCREEN_CURSORS[s.kind];
      if (!c || s.x + s.w < left || s.x > left + W || Math.floor(this.t / c.blink) % 2) continue;
      g.fillStyle = CSS[c.colour];
      g.fillRect(s.x - left + c.x, s.y + c.y, c.w, c.h);
    }
  }

  drawShots(g) {
    for (const s of this.shots) {
      const x = Math.round(s.x);
      const y = Math.round(s.y);
      if (s.pod) {
        g.fillStyle = CSS[CYAN];
        g.fillRect(x - 3, y, 5, 1);
        continue;
      }
      g.fillStyle = CSS[LBLUE];
      g.fillRect(x - 5, y - 1, 7, 2);
      g.fillStyle = CSS[WHITE];
      g.fillRect(x - 1, y - 1, 4, 2);
    }
    // The beam: a band of blue light, rippling, with a white-hot core.
    for (const b of this.beams) {
      const half = b.h / 2;
      for (let x = Math.round(b.x - b.len); x < b.x; x++) {
        const top = Math.round(b.y - half + Math.sin((x + b.t * 5) * 0.4) * Math.max(1, half * 0.3));
        g.fillStyle = CSS[BLUE];
        g.fillRect(x, top, 1, b.h);
        g.fillStyle = CSS[LBLUE];
        g.fillRect(x, top + 1, 1, Math.max(1, b.h - 2));
        g.fillStyle = CSS[WHITE];
        g.fillRect(x, top + Math.floor(half) - 1, 1, Math.max(2, Math.floor(b.h / 4)));
      }
    }
  }

  drawPlayer(g) {
    const p = this.player;
    if (!p.alive || (p.invuln > 0 && !p.flyIn && (p.invuln >> 2) % 2)) return;
    const x = Math.round(p.x);
    const y = Math.round(p.y);
    const len = 3 + ((this.t >> 1) % 3) + (this.input.move().x > 0.3 ? 3 : 0);
    g.fillStyle = CSS[RED];
    g.fillRect(x - len, y + 7, len, 1);
    g.fillStyle = CSS[AMBER];
    g.fillRect(x - len + 2, y + 6, len - 2, 3);
    g.fillStyle = CSS[YELLOW];
    g.fillRect(x - 2, y + 7, 2, 1);
    const pod = this.podSprites[(this.t >> 3) % 2];
    for (const q of this.podSpots()) g.drawImage(pod.img, Math.round(q.x - pod.w / 2), Math.round(q.y - pod.h / 2));
    g.drawImage(this.ship.img, x, y);
    if (this.muzzle > 0) {
      g.fillStyle = CSS[WHITE];
      g.fillRect(x + 31, y + 6, 2, 3);
      g.fillStyle = CSS[YELLOW];
      g.fillRect(x + 33, y + 7, 2, 1);
    }
    if (p.charge > 0.05) {
      const r = 1 + Math.floor(p.charge * 4) + ((this.t >> 2) % 2);
      disc(g, x + 32, y + 7, r, p.charge >= 1 && this.t % 6 < 3 ? WHITE : LBLUE);
      disc(g, x + 32, y + 7, Math.max(0, r - 2), WHITE);
    }
  }

  drawBullets(g) {
    const f = this.bulletSprites[(this.t >> 2) % 2];
    for (const b of this.bullets) {
      if (b.plasma) this.drawPlasma(g, b);
      else if (b.glyph) text(g, b.glyph, b.x - 2, b.y - 3, LBLUE);
      else g.drawImage(f.img, Math.round(b.x - 2), Math.round(b.y - 2));
    }
  }

  /* A robot's bolt of plasma: a white-hot head, trailing light blue, then blue. */
  drawPlasma(g, b) {
    const v = Math.hypot(b.vx, b.vy) || 1;
    for (let i = 8; i >= 0; i--) {
      const size = i < 5 ? 2 : 1;
      g.fillStyle = CSS[i < 2 ? WHITE : i < 5 ? LBLUE : BLUE];
      g.fillRect(Math.round(b.x - (b.vx / v) * i) - (size >> 1), Math.round(b.y - (b.vy / v) * i) - (size >> 1), size, size);
    }
  }

  drawEffects(g) {
    for (const q of this.particles) {
      const f = q.life / q.max;
      if (q.kind === 'skull') {
        // Tumbling while it flies, and blinking out at the end.
        const { img, w, h } = this.skull;
        const flip = q.still || (q.life >> 3) % 2 ? q.flip : !q.flip;
        if (q.max - q.life > 24 || q.life % 4 < 2) blit(g, img, Math.round(q.x) - (w >> 1), Math.round(q.y) - (h >> 1), flip);
        continue;
      }
      if (q.kind === 'ring') {
        g.fillStyle = CSS[f < 0.35 ? WHITE : f < 0.7 ? YELLOW : AMBER];
        const cx = Math.round(q.x);
        const cy = Math.round(q.y);
        for (const [dx, dy] of circlePoints(Math.round(q.r))) g.fillRect(cx + dx, cy + dy, 1, 1);
        continue;
      }
      g.fillStyle = CSS[q.kind === 'fire' ? FIRE[Math.min(FIRE.length - 1, Math.floor(f * FIRE.length))] : q.c];
      const size = q.size ?? 1;
      g.fillRect(Math.round(q.x), Math.round(q.y), size, size);
    }
    for (const u of this.popups) {
      if (u.t < 30 || u.t % 4 < 2) text(g, u.str, u.x, u.y - 3, u.t % 8 < 4 ? WHITE : YELLOW, { align: 'center' });
    }
  }
}
