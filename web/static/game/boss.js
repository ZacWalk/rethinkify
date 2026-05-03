/* The boss at the end of each stage. It slides in, bobs, and opens its eye to attack (on the
   C64 it shows its cursor); the eye is the only place it can be hurt, and only while it is
   open. The rest of it, body and limbs, stops shots and is deadly to touch. */

import { AMBER, CLEAR, DGREY, LGREY, RED, WHITE, YELLOW } from './art.js';
import { CSS, PH, W, sprite, text } from './screen.js';

const rand = (lo, hi) => lo + Math.random() * (hi - lo);
const pick = (list) => list[Math.floor(Math.random() * list.length)];

/* The order each kind of boss runs through its attacks. */
const PATTERNS = {
  head: ['fan', 'ring', 'guns', 'spiral'],
  disc: ['ring', 'spawn', 'fan', 'spiral'],
  wing: ['guns', 'fan', 'spiral', 'ring'],
  c64: ['fan', 'spiral', 'ring', 'guns'],
};

/* Frames the eye stays shut, opening, open and closing. The C64's cursor blinks faster. */
const EYE = { normal: [100, 8, 160, 8], c64: [40, 0, 70, 0] };

export class Boss {
  constructor(game, artwork) {
    this.game = game;
    this.art = artwork;
    this.kind = game.st.info.kind;
    const body = artwork.body[0];
    this.sprites = { body: sprite(body), core: artwork.core.map(sprite), segment: sprite(artwork.segment) };
    this.w = body.w;
    this.h = body.h;
    this.x = W + 10;
    this.baseY = Math.round((PH - body.h) / 2);
    this.sway = Math.max(0, Math.min(16, Math.floor((PH - 24 - body.h) / 2)));
    this.y = this.baseY;
    this.targetX = W - body.w - 4;
    this.hp = game.st.bossHp;
    this.max = this.hp;
    this.t = 0;
    this.phase = 'enter';
    this.eye = 0;
    this.flash = 0;
    this.pattern = 0;
    this.turn = 0;
    this.queue = [];
    this.dying = 0;
    this.limbs = artwork.limbs.map((l) => ({ ...l, segs: Array.from({ length: l.length }, () => ({ x: 0, y: 0 })) }));
    this.moveLimbs();
  }

  get alive() {
    return this.phase === 'enter' || this.phase === 'fight';
  }

  get open() {
    return this.phase === 'fight' && this.eye === 2;
  }

  update() {
    const game = this.game;
    this.t++;
    if (this.flash > 0) this.flash--;
    if (this.phase === 'enter') {
      this.x -= 0.75;
      if (this.x <= this.targetX) {
        this.x = this.targetX;
        this.phase = 'fight';
        this.t = 0;
      }
    }
    if (this.alive) this.y = this.baseY + Math.round(Math.sin(this.t * 0.017) * this.sway);
    if (this.phase === 'fight') {
      const [shut, opening, open, closing] = EYE[this.kind === 'c64' ? 'c64' : 'normal'];
      const c = this.t % (shut + opening + open + closing);
      this.eye = c < shut ? 0 : c < shut + opening ? 1 : c < shut + opening + open ? 2 : 1;
      if (c === shut + opening || (game.stageIndex >= 2 && c === shut + opening + (open >> 1))) this.attack();
      while (this.queue.length && this.queue[0].at <= this.t) this.queue.shift().fn();
    }
    this.moveLimbs();
    if (this.phase === 'dying') {
      this.dying++;
      game.shake = Math.max(game.shake, 3);
      if (this.dying % 5 === 0) {
        const big = this.dying % 15 === 0;
        game.explode(...this.randomPoint(), big ? 2 : 1);
        game.sound.sfx(big ? 'boom' : 'pop');
      }
      if (this.dying >= 150) {
        for (let i = 0; i < 6; i++) game.explode(...this.randomPoint(), 3);
        game.sound.sfx('boom');
        const st = game.st;
        game.addScore(10000 * ((game.stageIndex % 5) + 1) * (st.loop + 1), this.x + this.w / 2, this.y + this.h / 2, true);
        this.phase = 'gone';
      }
    }
  }

  /* Limbs are chains of balls that sway, each link further out swinging a little wider. */
  moveLimbs() {
    for (const l of this.limbs) {
      let x = this.x + l.x;
      let y = this.y + l.y;
      l.segs.forEach((s, i) => {
        const a = l.angle + Math.sin(this.t * 0.03 + i * 0.5) * 0.3 * (1 + i * 0.15);
        x += Math.cos(a) * 7;
        y += Math.sin(a) * 7;
        s.x = x;
        s.y = y;
      });
    }
  }

  /* Starts the next attack in this kind's list; its shots go out over the next second or two. */
  attack() {
    const game = this.game;
    const name = PATTERNS[this.kind][this.pattern++ % PATTERNS[this.kind].length];
    const speed = game.st.hard.bulletSpeed;
    const glyph = this.kind === 'c64' ? () => pick([...'?SYNTAXERROR']) : () => null;
    const at = (delay, fn) => {
      this.queue.push({ at: this.t + delay, fn });
      this.queue.sort((a, b) => a.at - b.at);
    };
    const core = () => {
      const c = this.coreBox();
      return [c.x + c.w / 2, c.y + c.h / 2];
    };
    const gun = (i) => {
      const g = this.art.guns[i % 2];
      return [this.x + g.x, this.y + g.y];
    };
    const towards = ([x, y]) => Math.atan2(game.player.y + 7 - y, game.player.x + 16 - x);
    const fire = (o, angle, s) => game.shoot(o[0], o[1], angle, s, false, glyph());
    if (name === 'fan') {
      for (let k = 0; k < 3; k++) {
        at(k * 20, () => {
          const o = core();
          for (let i = -2; i <= 2; i++) fire(o, towards(o) + i * 0.2, speed);
          game.sound.sfx('enemy');
        });
      }
    } else if (name === 'ring') {
      // Rings and spirals start gentle and fill in as the stages go by.
      const n = 10 + 2 * Math.min(3, game.stageIndex);
      for (let k = 0; k < (game.stageIndex >= 3 ? 3 : 2); k++) {
        at(k * 28, () => {
          const o = core();
          this.turn += 0.13;
          for (let i = 0; i < n; i++) fire(o, this.turn + (i / n) * Math.PI * 2, speed * 0.8);
          game.sound.sfx('enemy');
        });
      }
    } else if (name === 'guns') {
      for (let k = 0; k < 8; k++) {
        at(k * 10, () => {
          const o = gun(k);
          fire(o, towards(o), speed * 1.15);
          game.sound.sfx('enemy');
        });
      }
    } else if (name === 'spiral') {
      for (let k = 0; k < 18 + 4 * Math.min(2, game.stageIndex); k++) {
        at(k * 4, () => {
          const o = core();
          this.turn += 0.3;
          fire(o, this.turn, speed * 0.75);
          fire(o, this.turn + Math.PI, speed * 0.75);
        });
      }
    } else {
      for (let k = 0; k < 4; k++) {
        at(k * 18, () => {
          const [x, y] = gun(k);
          game.addEnemy('drifter', x, y, { y0: y, vx: -1.3, amp: 14, freq: 0.06, phase: 0 });
        });
      }
    }
  }

  coreBox() {
    const c = this.sprites.core[0];
    return { x: this.x + this.art.coreAt.x, y: this.y + this.art.coreAt.y, w: c.w, h: c.h };
  }

  solid(x, y) {
    return this.art.body[0].get(Math.floor(x - this.x), Math.floor(y - this.y)) !== CLEAR;
  }

  limbAt(x, y, r = 5) {
    return this.limbs.some((l) => l.segs.some((s) => (x - s.x) ** 2 + (y - s.y) ** 2 < r * r));
  }

  /* A shot that reaches the boss: it hurts if it finds the open eye, and glances off anything
     else. Returns whether the shot was stopped. */
  hitByShot(s) {
    if (!this.alive) return false;
    const c = this.coreBox();
    if (s.x >= c.x - 2 && s.x <= c.x + c.w && s.y >= c.y && s.y <= c.y + c.h) {
      if (this.open) this.hurt(s.dmg, s.x, s.y);
      else this.glance(s.x, s.y);
      return true;
    }
    if (this.solid(s.x, s.y) || this.limbAt(s.x, s.y)) {
      this.glance(s.x, s.y);
      return true;
    }
    return false;
  }

  /* The beam hurts the eye once as it passes, if the eye is open. */
  hitByBeam(b) {
    if (!this.alive || b.hit.has(this)) return;
    const c = this.coreBox();
    if (b.x - b.len < c.x + c.w && b.x > c.x && b.y - b.h / 2 < c.y + c.h && b.y + b.h / 2 > c.y && this.open) {
      b.hit.add(this);
      this.hurt(b.dmg, c.x + c.w / 2, c.y + c.h / 2);
    }
  }

  /* Whether the ship, centred at (px, py), touches any part of the boss. */
  touches(px, py) {
    if (this.phase === 'gone') return false;
    return [
      [px - 12, py],
      [px + 13, py],
      [px, py - 3],
      [px, py + 3],
    ].some(([x, y]) => this.solid(x, y) || this.limbAt(x, y, 6));
  }

  glance(x, y) {
    this.game.sound.sfx('tink');
    this.game.spark(x, y, LGREY);
  }

  hurt(dmg, x, y) {
    const game = this.game;
    this.hp -= dmg;
    this.flash = 3;
    game.sound.sfx('hit');
    game.spark(x, y, YELLOW);
    if (this.hp <= 0) {
      this.phase = 'dying';
      this.queue = [];
      game.sound.sfx('boom');
      for (const b of game.bullets) game.spark(b.x, b.y, LGREY, 2);
      game.bullets = [];
    }
  }

  randomPoint() {
    const body = this.art.body[0];
    for (let i = 0; i < 30; i++) {
      const x = Math.floor(rand(0, body.w));
      const y = Math.floor(rand(0, body.h));
      if (body.get(x, y) !== CLEAR) return [this.x + x, this.y + y];
    }
    return [this.x + body.w / 2, this.y + body.h / 2];
  }

  draw(g) {
    if (this.phase === 'gone') return;
    const { body, core, segment } = this.sprites;
    for (const l of this.limbs) {
      for (let i = l.segs.length - 1; i >= 0; i--) {
        const s = l.segs[i];
        g.drawImage(segment.img, Math.round(s.x - segment.w / 2), Math.round(s.y - segment.h / 2));
      }
    }
    g.drawImage(this.flash > 0 ? body.flash : body.img, Math.round(this.x), Math.round(this.y));
    const eye = core[this.phase === 'dying' ? 0 : this.eye];
    g.drawImage(eye.img, Math.round(this.x + this.art.coreAt.x), Math.round(this.y + this.art.coreAt.y));
  }

  /* Its name and what is left of it, across the top of the screen, just below the arena's
     ceiling. */
  drawBar(g) {
    if (this.phase === 'gone') return;
    const y = 13;
    text(g, this.game.st.info.boss, 3, y, LGREY);
    const x = 116;
    const w = W - x - 4;
    g.fillStyle = CSS[DGREY];
    g.fillRect(x, y, w, 7);
    g.fillStyle = CSS[this.flash > 0 ? WHITE : this.hp < this.max / 4 ? RED : AMBER];
    g.fillRect(x + 1, y + 1, Math.max(0, Math.round(((w - 2) * this.hp) / this.max)), 5);
  }
}
