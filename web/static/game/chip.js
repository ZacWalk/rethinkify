/* The game's sound chip: a small synthesiser in the manner of the C64's SID, a composer that
   writes a new tune for every stage, and the game's sound effects.

   It runs on the audio thread as an AudioWorklet, so the music never stutters when the game is
   busy, and audio.js only sends it short messages: play this tune, make this sound. Apart from
   the few lines at the bottom that register it, it is plain JavaScript, so it can also be run
   offline to check what it plays.

   Like a C64 player routine, everything except the oscillators moves on in frames, fifty times
   a second: notes start on frame boundaries, arpeggios step a note a frame, and vibrato, pulse
   width and the filter sweep a frame at a time. That stepping is much of the C64's sound. */

const FRAME_HZ = 50;
const ROWS_PER_BAR = 16;

/* The same seeded generator as art.js, repeated because a worklet stands alone. */
function rng(seed) {
  let s = seed >>> 0 || 1;
  const next = () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  next.int = (lo, hi) => lo + Math.floor(next() * (hi - lo + 1));
  next.pick = (list) => list[Math.floor(next() * list.length)];
  next.chance = (p) => next() < p;
  return next;
}

const midiHz = (n) => 440 * 2 ** ((n - 69) / 12);

// ---------- the voices ----------

const OFF = 0;
const ATTACK = 1;
const DECAY = 2;
const RELEASE = 3;

/* Rounds off the corners of saw and pulse waves (PolyBLEP), so high notes do not alias into a
   hiss. The SID's own waves were cleaner than a naive digital square. */
function blep(t, dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1;
  }
  if (t > 1 - dt) {
    t = (t - 1) / dt;
    return t * t + t + t + 1;
  }
  return 0;
}

/* One voice: an oscillator (pulse, saw, triangle or noise) and an ADSR envelope, played by an
   instrument that changes its settings frame by frame. */
class Voice {
  constructor(sr, pan = 0) {
    this.sr = sr;
    this.panL = Math.cos(((pan + 1) * Math.PI) / 4);
    this.panR = Math.sin(((pan + 1) * Math.PI) / 4);
    this.wave = 'pulse';
    this.freq = 440;
    this.pw = 0.5;
    this.level = 0.5;
    this.phase = 0;
    this.env = 0;
    this.stage = OFF;
    this.lfsr = 0x7ffff8;
    this.noise = 0;
    this.step = -1;
    this.prog = null;
    this.envelope([0.002, 0.1, 0.5, 0.1]);
  }

  /* Attack, decay and release in seconds, sustain as a level. */
  envelope([a, d, s, r]) {
    this.aStep = 1 / Math.max(1, a * this.sr);
    this.dMul = Math.exp(-4 / Math.max(1, d * this.sr));
    this.sustain = s;
    this.rMul = Math.exp(-4 / Math.max(1, r * this.sr));
  }

  /* Starts a note: `inst` is an instrument or a sound effect (see INSTRUMENTS and SFX). Its
     first frame applies at once; `fresh` stops the chip's frame tick, which may follow in the
     same frame, from skipping straight on to the second. */
  play(inst, note = 60, chord = null) {
    this.wave = inst.wave ?? (inst.table ? inst.table[0][0] : 'pulse');
    this.pw = inst.pw ?? 0.5;
    this.level = inst.level;
    this.envelope(inst.env);
    this.prog = { inst, note, chord, frame: 0, pw: this.pw, pwDir: 1 };
    this.stage = ATTACK;
    this.tick();
    this.prog.fresh = true;
  }

  release() {
    if (this.stage !== OFF) this.stage = RELEASE;
  }

  get busy() {
    return this.stage !== OFF;
  }

  /* The instrument's work for one frame: drum and effect tables, arpeggios, vibrato, pulse
   width sweeps, and letting go of the gate. */
  tick() {
    const p = this.prog;
    if (!p) return;
    if (p.fresh) {
      p.fresh = false;
      return;
    }
    const { inst } = p;
    const f = p.frame++;
    let note = p.note;
    let hz = null;
    if (inst.table) {
      const [wave, stepHz, pw] = inst.table[Math.min(f, inst.table.length - 1)];
      this.wave = wave;
      hz = stepHz;
      if (pw) this.pw = pw;
      if (f >= (inst.gate ?? inst.table.length)) this.release();
    }
    if (p.chord) note += p.chord[Math.floor(f / (inst.arpSpeed ?? 1)) % p.chord.length];
    if (inst.bend && f < inst.bend.length) note += inst.bend[f];
    if (inst.vibrato && f >= inst.vibrato[0]) {
      const [delay, depth, rate] = inst.vibrato;
      note += Math.sin(((f - delay) * 2 * Math.PI * rate) / FRAME_HZ) * depth * Math.min(1, (f - delay) / 10);
    }
    this.freq = hz ?? midiHz(note);
    if (inst.pwm) {
      const [lo, hi, speed] = inst.pwm;
      p.pw += p.pwDir * speed;
      if (p.pw > hi || p.pw < lo) p.pwDir = -p.pwDir;
      this.pw = p.pw;
    }
  }

  sample() {
    if (this.stage === OFF) return 0;
    const dt = Math.min(0.45, this.freq / this.sr);
    let t = this.phase + dt;
    if (t >= 1) t -= 1;
    this.phase = t;
    let o;
    if (this.wave === 'saw') {
      o = 2 * t - 1 - blep(t, dt);
    } else if (this.wave === 'tri') {
      o = 4 * Math.abs(t - 0.5) - 1;
    } else if (this.wave === 'noise') {
      // Like the SID: a shift register clocked by the oscillator, so noise has a pitch.
      const step = (t * 8) | 0;
      if (step !== this.step) {
        this.step = step;
        const bit = ((this.lfsr >> 22) ^ (this.lfsr >> 17)) & 1;
        this.lfsr = ((this.lfsr << 1) | bit) & 0x7fffff;
        this.noise = ((this.lfsr >> 7) & 0xff) / 127.5 - 1;
      }
      o = this.noise;
    } else {
      const pw = this.pw;
      o = (t < pw ? 1 : -1) + blep(t, dt) - blep((t - pw + 1) % 1, dt);
    }
    if (this.stage === ATTACK) {
      this.env += this.aStep;
      if (this.env >= 1) {
        this.env = 1;
        this.stage = DECAY;
      }
    } else if (this.stage === DECAY) {
      this.env = this.sustain + (this.env - this.sustain) * this.dMul;
    } else {
      this.env *= this.rMul;
      if (this.env < 1e-4) {
        this.env = 0;
        this.stage = OFF;
      }
    }
    return o * this.env * this.level;
  }
}

/* The SID's best-loved part: a resonant low-pass filter, here a state-variable one, which the
   bass runs through so each note can open bright and close down. */
class Filter {
  constructor(sr) {
    this.sr = sr;
    this.low = 0;
    this.band = 0;
    this.set(1000, 0.5);
  }

  set(cutoff, res) {
    this.f = 2 * Math.sin((Math.PI * Math.min(cutoff, this.sr / 7)) / this.sr);
    this.q = 1.05 - Math.min(0.9, res);
  }

  process(x) {
    this.low += this.f * this.band;
    const high = x - this.low - this.q * this.band;
    this.band += this.f * high;
    return this.low;
  }
}

// ---------- instruments and sound effects ----------

/* What a voice does on each frame of a note. env is [attack, decay, sustain, release]; pwm
   sweeps the pulse width between two values; vibrato is [delay in frames, depth in semitones,
   rate in Hz]; bend nudges the first frames' pitch; a table plays one [wave, Hz, pulse width]
   step a frame, which is how C64 drums were made; filter is [from Hz, to Hz, frames,
   resonance] for a voice that runs through the filter. */
const INSTRUMENTS = {
  bass: { wave: 'pulse', pw: 0.25, pwm: [0.1, 0.45, 0.012], env: [0.002, 0.22, 0.45, 0.06], level: 0.5, filter: [2600, 320, 14, 0.6] },
  dark: { wave: 'pulse', pw: 0.3, pwm: [0.15, 0.45, 0.006], env: [0.004, 0.3, 0.5, 0.08], level: 0.5, filter: [900, 220, 20, 0.5] },
  growl: { wave: 'saw', env: [0.002, 0.16, 0.55, 0.05], level: 0.38, filter: [3400, 480, 8, 0.72] },
  lead: { wave: 'pulse', pw: 0.45, pwm: [0.18, 0.48, 0.006], env: [0.006, 0.3, 0.65, 0.12], level: 0.3, vibrato: [14, 0.22, 5.5], bend: [-0.6, -0.2] },
  saw: { wave: 'saw', env: [0.004, 0.25, 0.6, 0.1], level: 0.24, vibrato: [10, 0.28, 6] },
  arp: { wave: 'pulse', pw: 0.22, env: [0.001, 0.12, 0.3, 0.05], level: 0.19 },
  kick: {
    table: [['noise', 3200], ['pulse', 190, 0.5], ['pulse', 140], ['pulse', 105], ['tri', 85], ['tri', 70], ['tri', 60], ['tri', 52]],
    gate: 1,
    env: [0, 0.16, 0, 0.12],
    level: 0.8,
  },
  snare: {
    table: [['pulse', 280, 0.45], ['noise', 6000], ['noise', 5200], ['noise', 4600], ['noise', 4200]],
    gate: 1,
    env: [0, 0.17, 0, 0.14],
    level: 0.42,
  },
  hat: { table: [['noise', 9500]], gate: 1, env: [0, 0.03, 0, 0.03], level: 0.15 },
  ohat: { table: [['noise', 8500]], gate: 1, env: [0, 0.14, 0, 0.12], level: 0.12 },
};

const sweep = (wave, from, to, frames, pw) =>
  Array.from({ length: frames }, (_, i) => [wave, from * (to / from) ** (i / Math.max(1, frames - 1)), pw]);

/* Two tables a frame at a time each, the C64 way of mixing noise into a tone on one voice. */
const interleave = (a, b) => a.flatMap((step, i) => [step, b[i] ?? step]);

const arpeggio = (notes, frames, wave, pw) => notes.flatMap((n) => Array(frames).fill([wave, midiHz(n), pw]));

/* Sound effects, all one voice each. prio decides which sound gives way when every effects
   voice is busy. */
const SFX = {
  shot: { table: sweep('pulse', 1500, 450, 5, 0.25), env: [0, 0.09, 0, 0.03], level: 0.2, prio: 1 },
  beam: {
    table: interleave(sweep('noise', 5000, 900, 14), sweep('saw', 900, 110, 14)),
    env: [0, 0.5, 0.35, 0.25],
    level: 0.55,
    prio: 3,
  },
  hit: { table: [['noise', 7000], ['noise', 5000]], env: [0, 0.04, 0, 0.02], level: 0.14, prio: 0 },
  tink: { table: sweep('pulse', 3400, 2500, 3, 0.5), env: [0, 0.05, 0, 0.02], level: 0.11, prio: 0 },
  pop: { table: sweep('noise', 3000, 500, 10), env: [0, 0.22, 0, 0.08], level: 0.42, prio: 2 },
  boom: { table: [['pulse', 90, 0.5], ['pulse', 70, 0.5], ...sweep('noise', 1400, 140, 32)], env: [0, 0.9, 0, 0.4], level: 0.75, prio: 4 },
  power: { table: arpeggio([72, 76, 79, 84, 88, 91, 96], 2, 'pulse', 0.5), env: [0.001, 0.4, 0.4, 0.15], level: 0.34, prio: 3 },
  oneup: { table: arpeggio([84, 88, 91, 96, 91, 96, 100], 4, 'pulse', 0.25), env: [0.001, 0.4, 0.5, 0.2], level: 0.3, prio: 5 },
  die: {
    table: interleave(sweep('noise', 4000, 90, 50), sweep('saw', 600, 40, 50)),
    env: [0, 1.6, 0, 0.6],
    level: 0.75,
    prio: 5,
  },
  warn: {
    table: Array.from({ length: 100 }, (_, i) => ['pulse', Math.floor(i / 12) % 2 ? 660 : 880, 0.5]),
    env: [0.002, 0.2, 0.8, 0.1],
    level: 0.28,
    prio: 5,
  },
  enemy: { table: sweep('pulse', 700, 380, 4, 0.5), env: [0, 0.07, 0, 0.02], level: 0.08, prio: 0 },
  // A robot's plasma rifle: a falling saw roughened with noise, a frame of each in turn.
  zap: { table: interleave(sweep('saw', 2600, 260, 8), sweep('noise', 7000, 1800, 8)), env: [0, 0.14, 0, 0.05], level: 0.16, prio: 1 },
  select: { table: [['tri', 1200], ['tri', 1600]], env: [0, 0.06, 0, 0.03], level: 0.2, prio: 5 },
};

// ---------- the composer ----------

const SCALES = {
  aeolian: [0, 2, 3, 5, 7, 8, 10],
  dorian: [0, 2, 3, 5, 7, 9, 10],
  harmonic: [0, 2, 3, 5, 7, 8, 11],
  phrygian: [0, 1, 3, 5, 7, 8, 10],
  ionian: [0, 2, 4, 5, 7, 9, 11],
};

/* Semitones above the key's tonic for a scale degree, which may run past an octave either way. */
const semis = (scale, deg) => scale[((deg % 7) + 7) % 7] + 12 * Math.floor(deg / 7);

/* Chords as scale degrees, a bar each: the minor-key moves of a thousand game tunes. */
const PROGRESSIONS = [
  [0, 5, 2, 6],
  [0, 6, 5, 6],
  [0, 3, 6, 2],
  [0, 5, 3, 4],
  [0, 0, 5, 6],
  [0, 2, 5, 4],
  [5, 6, 0, 0],
  [0, 3, 0, 4],
  [0, 6, 3, 4],
];
const BOSS_PROGRESSIONS = [
  [0, 1, 0, 1],
  [0, 5, 1, 4],
  [0, 0, 3, 4],
  [0, 6, 5, 4],
  [0, 3, 1, 4],
];

/* Patterns, a character a row. Bass: R root, O octave, 5 fifth, - hold, . let go. Drums: K kick,
   S snare, H hat, O open hat. Arpeggios: A chord, - hold, . let go. */
const BASS = ['R.O.R.O.R.O.R.O.', 'R-.RR-.RR-.RO-5.', 'R.RO.RR.R.RO.R5O', 'R---R.O.R---R.OR', 'RR.ORR.ORR.ORR5O'];
const BOSS_BASS = ['RRORRRORRRORRROR', 'RROR5RORRROR5ROO', 'R.RRO.RRR.RRO.R5'];
const DRUMS = ['K.H.S.H.K.H.S.H.', 'K.HKS.H.K.HKS.HS', 'K.H.S..KK.H.S.H.', 'K.H.S.HKK.H.S.HO', 'KHHKSHHHKHHKSHHK'];
const BOSS_DRUMS = ['K.HKS.HKK.HKS.HK', 'KHKHSHKHKHKHSHKS', 'K.K.S.K.K.K.S.KS'];
const INTRO_DRUMS = 'K...H...K...H.H.';
const BREAK_DRUMS = 'K.H.H.H.K.H.H.HO';
const FILLS = ['SSSS', 'S.SS', 'SSKS', 'KSSS', 'S.S.'];
const ARPS = ['A---A---A---A---', 'A-.AA-.AA-.AA-.A', 'A.A.A.A.A.A.A.A.', 'A--.A--.A-.A-.A.'];

const OFF_EVENT = 0;

/* A tune: a speed (frames per row), four tracks of events by row, and where it loops back to. */
class Song {
  constructor(speed, loop) {
    this.speed = speed;
    this.loop = loop;
    this.bars = 0;
    this.loopBar = 0;
    this.tracks = { bass: [], lead: [], arp: [], drums: [] };
  }

  get rows() {
    return this.bars * ROWS_PER_BAR;
  }

  note(track, row, n, inst, chord = null) {
    this.tracks[track][row] = { n, inst, chord };
  }

  off(track, row) {
    if (this.tracks[track][row] === undefined) this.tracks[track][row] = OFF_EVENT;
  }

  pattern(track, bar, pattern, notes) {
    for (let i = 0; i < ROWS_PER_BAR; i++) {
      const ch = pattern[i];
      const row = bar * ROWS_PER_BAR + i;
      if (ch === '.') this.off(track, row);
      else if (notes[ch]) this.note(track, row, ...notes[ch]);
    }
  }
}

/* Two bars of rhythm for a melody: where each note starts, how long it lasts, and which way it
   steps from the last one, in scale steps. */
function motif(r) {
  const notes = [];
  for (let row = 0; row < 32; ) {
    let len = r.pick(row % 4 === 0 ? [2, 4, 4, 6, 8] : [1, 2, 2, 3]);
    len = Math.min(len, 32 - row);
    if (row > 0 && r.chance(0.12)) {
      row += len;
      continue;
    }
    notes.push({ row, len, step: r.pick([-2, -1, -1, -1, 0, 1, 1, 1, 2, 3, -3]) });
    row += len;
  }
  return notes;
}

/* The same rhythm with its second bar re-stepped: a phrase that answers the first. */
const vary = (r, m) => m.map((n) => (n.row >= 16 && r.chance(0.6) ? { ...n, step: r.pick([-2, -1, 1, 2]) } : n));

const isChordTone = (deg, root) => [0, 2, 4].includes((((deg - root) % 7) + 7) % 7);

function nearestChordTone(deg, root) {
  for (let d = 0; d < 4; d++) {
    if (isChordTone(deg + d, root)) return deg + d;
    if (isChordTone(deg - d, root)) return deg - d;
  }
  return deg;
}

/* Sets a two-bar motif to music over its chords: steps through the scale, landing on a chord
   tone on every strong beat, bouncing off the edges of the range, and with a cadence it ends
   on the chord's root and holds it. */
function realize(song, m, bar0, chords, ctx, cadence = false) {
  const LO = -2;
  const HI = 11;
  m.forEach((note, k) => {
    const chord = chords[Math.floor(note.row / ROWS_PER_BAR)];
    let deg = ctx.deg + note.step;
    if (deg > HI) deg = 2 * HI - deg;
    if (deg < LO) deg = 2 * LO - deg;
    if (note.row % 8 === 0) deg = nearestChordTone(deg, chord);
    const last = cadence && k === m.length - 1;
    if (last) {
      deg = chord + 7 * Math.round((deg - chord) / 7);
      if (deg > HI) deg -= 7;
    }
    ctx.deg = deg;
    const row = bar0 * ROWS_PER_BAR + note.row;
    song.note('lead', row, ctx.tonic + semis(ctx.scale, deg), ctx.inst);
    const len = last ? 32 - note.row : note.len;
    song.off('lead', row + len - (len > 2 ? 1 : 0));
  });
}

/* A looping tune for a stage, or for a boss: an intro, then sections over two chord
   progressions, with a melody built from two motifs, stated, answered and resolved. */
function composeLoop(seed, boss) {
  const r = rng(seed);
  const scale = SCALES[boss ? r.pick(['harmonic', 'phrygian']) : r.pick(['aeolian', 'aeolian', 'dorian', 'harmonic'])];
  const key = r.int(0, 11);
  const song = new Song(boss ? 5 : r.pick([6, 6, 7]), true);
  const progs = boss ? BOSS_PROGRESSIONS : PROGRESSIONS;
  const progA = r.pick(progs);
  const progB = r.pick(progs.filter((p) => p !== progA));
  const bassA = r.pick(boss ? BOSS_BASS : BASS);
  const bassB = r.pick(boss ? BOSS_BASS : BASS);
  const drumA = r.pick(boss ? BOSS_DRUMS : DRUMS);
  const drumB = r.pick(boss ? BOSS_DRUMS : DRUMS);
  const arpA = r.pick(ARPS);
  const arpB = r.pick(ARPS);
  const ctx = { tonic: 60 + key, scale, deg: 4, inst: boss ? r.pick(['saw', 'lead']) : r.pick(['lead', 'lead', 'saw']) };
  const ma = motif(r);
  const mb = motif(r);
  const sections = boss
    ? [['intro', 2, progA], ['A', 8, progA], ['B', 8, progB]]
    : [['intro', 4, progA], ['A', 8, progA], ['B', 8, progB], ['A', 8, progA], ['break', 4, progB]];
  song.loopBar = sections[0][1];

  for (const [kind, bars, prog] of sections) {
    const bar0 = song.bars;
    song.bars += bars;
    const chords = Array.from({ length: bars }, (_, i) => prog[i % prog.length]);
    chords.forEach((deg, b) => {
      const bar = bar0 + b;
      const root = (key + semis(scale, deg)) % 12;
      const third = semis(scale, deg + 2) - semis(scale, deg);
      const fifth = semis(scale, deg + 4) - semis(scale, deg);
      const bassNote = 36 + root;
      const bassInst = kind === 'intro' ? 'dark' : boss ? 'growl' : 'bass';
      const bassPattern = kind === 'intro' ? 'R---R---R---R.O.' : kind === 'break' ? 'R-------R---R-O-' : kind === 'B' ? bassB : bassA;
      song.pattern('bass', bar, bassPattern, { R: [bassNote, bassInst], O: [bassNote + 12, bassInst], 5: [bassNote + 7, bassInst] });
      let drums = kind === 'intro' ? INTRO_DRUMS : kind === 'break' ? BREAK_DRUMS : kind === 'B' ? drumB : drumA;
      if (b % 4 === 3 && kind !== 'intro') drums = drums.slice(0, 12) + r.pick(FILLS);
      song.pattern('drums', bar, drums, { K: [36, 'kick'], S: [38, 'snare'], H: [42, 'hat'], O: [46, 'ohat'] });
      if (kind !== 'intro') {
        song.pattern('arp', bar, kind === 'B' ? arpB : arpA, { A: [55 + ((root + 5) % 12), 'arp', [0, third, fifth]] });
      }
    });
    if (kind === 'A') {
      realize(song, ma, bar0, chords.slice(0, 2), ctx);
      realize(song, vary(r, ma), bar0 + 2, chords.slice(2, 4), ctx);
      realize(song, mb, bar0 + 4, chords.slice(4, 6), ctx);
      realize(song, vary(r, ma), bar0 + 6, chords.slice(6, 8), ctx, true);
    } else if (kind === 'B') {
      realize(song, mb, bar0, chords.slice(0, 2), ctx);
      realize(song, vary(r, mb), bar0 + 2, chords.slice(2, 4), ctx);
      ctx.deg += 2;
      realize(song, ma, bar0 + 4, chords.slice(4, 6), ctx);
      realize(song, vary(r, mb), bar0 + 6, chords.slice(6, 8), ctx, true);
    }
  }
  return song;
}

/* The short tunes: get ready, stage clear and game over. They play once. */
function composeJingle(kind, seed) {
  const r = rng(seed);
  const key = r.int(0, 11);
  if (kind === 'ready') {
    const song = new Song(5, false);
    song.bars = 2;
    const tonic = 60 + key;
    [-7, -5, -3, 0, 2, 4, 7].forEach((deg, i) => song.note('lead', i * 2, tonic + semis(SCALES.aeolian, deg), 'lead'));
    song.off('lead', 31);
    song.pattern('arp', 0, 'A---A---A---A---', { A: [55 + ((key + 5) % 12), 'arp', [0, 3, 7]] });
    song.pattern('arp', 1, 'A---A---A-A-A-A.', { A: [55 + ((key + 12) % 12), 'arp', [0, 4, 7]] });
    song.pattern('bass', 0, 'R---R---R---R---', { R: [36 + key, 'bass'] });
    song.pattern('bass', 1, 'R-R-R-R-R-R-RRR.', { R: [36 + ((key + 7) % 12), 'bass'] });
    song.pattern('drums', 0, 'K...S...K...S...', { K: [36, 'kick'], S: [38, 'snare'] });
    song.pattern('drums', 1, 'S.S.S.S.SSSSSSSS', { S: [38, 'snare'] });
    return song;
  }
  if (kind === 'clear') {
    const song = new Song(5, false);
    song.bars = 4;
    const tonic = 60 + ((key + 3) % 12);
    const scale = SCALES.ionian;
    const tune = [[0, 4, 2], [2, 4, 2], [4, 4, 2], [6, 7, 10], [16, 8, 2], [18, 7, 2], [20, 8, 2], [22, 9, 10], [32, 11, 4], [36, 9, 4], [40, 8, 4], [44, 6, 4], [48, 7, 15]];
    for (const [row, deg, len] of tune) {
      song.note('lead', row, tonic + semis(scale, deg), 'lead');
      song.off('lead', row + len);
    }
    [0, 3, 4, 0].forEach((deg, bar) => {
      const root = (tonic + semis(scale, deg)) % 12;
      const third = semis(scale, deg + 2) - semis(scale, deg);
      const fifth = semis(scale, deg + 4) - semis(scale, deg);
      song.pattern('bass', bar, bar === 3 ? 'R---------------' : 'R.R.R.R.R.R.R.R.', { R: [36 + root, 'bass'] });
      song.pattern('arp', bar, bar === 3 ? 'A---------------' : 'A---A---A---A---', { A: [55 + ((root + 5) % 12), 'arp', [0, third, fifth]] });
      song.pattern('drums', bar, bar === 3 ? 'K...............' : 'K.H.S.H.K.H.S.HS', { K: [36, 'kick'], S: [38, 'snare'], H: [42, 'hat'] });
    });
    return song;
  }
  // Game over: slower, and downhill all the way.
  const song = new Song(6, false);
  song.bars = 3;
  const tonic = 60 + key;
  const scale = SCALES.aeolian;
  [7, 6, 5, 4, 3, 2, 1, 0].forEach((deg, i) => {
    song.note('lead', i * 6, tonic + semis(scale, deg), 'saw');
    song.off('lead', i * 6 + 5);
  });
  [0, 5, 0].forEach((deg, bar) => {
    const root = (key + semis(scale, deg)) % 12;
    song.pattern('bass', bar, 'R-------R-------', { R: [36 + root, 'dark'] });
  });
  song.off('lead', 47);
  return song;
}

/* Plays a Song on the four music voices, a row every `speed` frames. */
class Player {
  constructor(chip, song, next) {
    this.chip = chip;
    this.song = song;
    this.next = next;
    this.row = 0;
    this.tick = 0;
  }

  frame() {
    const { song, chip } = this;
    if (this.tick === 0) {
      for (const name of ['bass', 'lead', 'arp', 'drums']) {
        const e = song.tracks[name][this.row];
        const voice = chip.music[name];
        if (e === OFF_EVENT) voice.release();
        else if (e) {
          const inst = INSTRUMENTS[e.inst];
          voice.play(inst, e.n, e.chord);
          if (name === 'bass') chip.sweep = { filter: inst.filter, frame: 0 };
        }
      }
    }
    if (++this.tick >= song.speed) {
      this.tick = 0;
      if (++this.row >= song.rows) {
        if (song.loop) this.row = song.loopBar * ROWS_PER_BAR;
        else return this.chip.tuneEnded(this.next);
      }
    }
  }
}

export function compose(kind, seed) {
  if (kind === 'stage') return composeLoop(seed, false);
  if (kind === 'boss') return composeLoop(seed ^ 0xb055, true);
  return composeJingle(kind, seed);
}

// ---------- the chip ----------

/* Four voices of music (bass, lead, chords, drums), three for sound effects, one for the
   charge-up whine, a filter on the bass, an echo on the lead, and the mix. */
export class Chip {
  constructor(sr) {
    this.sr = sr;
    this.music = {
      bass: new Voice(sr, 0),
      lead: new Voice(sr, -0.3),
      arp: new Voice(sr, 0.35),
      drums: new Voice(sr, 0),
    };
    this.effects = [new Voice(sr, -0.1), new Voice(sr, 0.1), new Voice(sr, 0)];
    this.charger = new Voice(sr, 0);
    this.voices = [...Object.values(this.music), ...this.effects, this.charger];
    this.filter = new Filter(sr);
    this.sweep = null;
    this.player = null;
    this.chargeLevel = 0;
    this.chargeFrame = 0;
    this.perFrame = sr / FRAME_HZ;
    this.untilFrame = 0;
    this.echo = new Float32Array(Math.ceil(sr));
    this.echoAt = 0;
    this.echoLen = Math.round(sr * 0.36);
    this.dcIn = [0, 0];
    this.dcOut = [0, 0];
    this.gain = 0.6;
  }

  /* A message from audio.js. */
  command(msg) {
    if (msg.type === 'tune') this.startTune(msg.tune, msg.seed, msg.next);
    else if (msg.type === 'sfx') this.sfx(msg.name);
    else if (msg.type === 'charge') this.chargeLevel = msg.level;
    else if (msg.type === 'silence') {
      this.player = null;
      for (const v of Object.values(this.music)) v.release();
    }
  }

  startTune(kind, seed, next = null) {
    // Let go of whatever the last tune left sounding; the new one starts its own notes, and a
    // jingle without chords must not keep the old chord going under it.
    for (const v of Object.values(this.music)) v.release();
    const song = compose(kind, seed);
    this.player = new Player(this, song, next);
    this.echoLen = Math.min(this.echo.length - 1, Math.round(((3 * song.speed) / FRAME_HZ) * this.sr));
  }

  tuneEnded(next) {
    this.player = null;
    for (const v of Object.values(this.music)) v.release();
    if (next) this.startTune(next.tune, next.seed, next.next);
  }

  sfx(name) {
    const fx = SFX[name];
    if (!fx) return;
    let voice = this.effects.find((v) => !v.busy);
    if (!voice) {
      voice = this.effects.reduce((a, b) => ((a.prog?.inst.prio ?? 0) <= (b.prog?.inst.prio ?? 0) ? a : b));
      if ((voice.prog?.inst.prio ?? 0) > fx.prio) return;
    }
    voice.play(fx);
  }

  /* The charge-up whine: a triangle that climbs with the charge and warbles when it is full. */
  chargeFrameTick() {
    const level = this.chargeLevel;
    const v = this.charger;
    if (level <= 0) {
      v.release();
      return;
    }
    if (!v.busy || v.stage === RELEASE) {
      v.wave = 'tri';
      v.level = 0.16;
      v.envelope([0.05, 0.1, 1, 0.08]);
      v.prog = null;
      v.stage = ATTACK;
    }
    this.chargeFrame++;
    const full = level >= 1;
    v.wave = full && this.chargeFrame % 8 < 4 ? 'pulse' : 'tri';
    v.freq = 160 * 2 ** (Math.min(1, level) * 2.5) * (1 + 0.02 * Math.sin(this.chargeFrame * (full ? 2.4 : 0.9)));
  }

  frame() {
    this.player?.frame();
    for (const v of Object.values(this.music)) v.tick();
    for (const v of this.effects) v.tick();
    this.chargeFrameTick();
    if (this.sweep?.filter) {
      const [from, to, frames, res] = this.sweep.filter;
      const t = Math.min(1, this.sweep.frame++ / frames);
      this.filter.set(from * (to / from) ** t, res);
    }
  }

  /* Fills one block of samples, stereo. */
  render(left, right) {
    const { bass, lead, arp, drums } = this.music;
    const others = [arp, drums, ...this.effects, this.charger];
    for (let i = 0; i < left.length; i++) {
      if (--this.untilFrame <= 0) {
        this.untilFrame += this.perFrame;
        this.frame();
      }
      const b = this.filter.process(bass.sample());
      const l = lead.sample();
      let L = b + l * lead.panL;
      let R = b + l * lead.panR;
      for (const v of others) {
        const s = v.sample();
        L += s * v.panL;
        R += s * v.panR;
      }
      // An echo of the lead, mostly on the right, like a second voice a beat behind.
      const at = this.echoAt;
      const back = this.echo[(at - this.echoLen + this.echo.length) % this.echo.length];
      this.echo[at] = l + back * 0.35;
      this.echoAt = (at + 1) % this.echo.length;
      L += back * 0.12;
      R += back * 0.26;
      left[i] = this.out(0, L);
      right[i] = this.out(1, R);
    }
  }

  /* Takes out any DC (pulse waves have some), and rounds off peaks rather than clipping. */
  out(ch, x) {
    const y = x - this.dcIn[ch] + 0.995 * this.dcOut[ch];
    this.dcIn[ch] = x;
    this.dcOut[ch] = y;
    return Math.tanh(y * this.gain);
  }
}

// ---------- the AudioWorklet glue ----------

if (typeof registerProcessor === 'function') {
  registerProcessor(
    'chip',
    class extends AudioWorkletProcessor {
      constructor() {
        super();
        // `sampleRate` is a global of the audio thread.
        this.chip = new Chip(sampleRate);
        this.port.onmessage = (e) => this.chip.command(e.data);
      }

      process(_inputs, outputs) {
        const [left, right] = outputs[0];
        this.chip.render(left, right ?? left);
        return true;
      }
    },
  );
}
