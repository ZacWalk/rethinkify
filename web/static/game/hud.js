/* What is drawn over the game: the HUD along the bottom, and the cards for each part of a game:
   get ready, warning, stage clear, game over, high scores, and pause. */

import { BLACK, BLUE, DBLUE, GREY, LBLUE, LGREEN, LGREY, PINK, RED, WHITE, YELLOW } from './art.js';
import { CSS, H, PH, W, text } from './screen.js';

const pad = (n, len = 7) => String(Math.max(0, Math.floor(n))).padStart(len, '0');
const MID = W / 2;

function drawLife(g, x, y) {
  g.fillStyle = CSS[LGREY];
  g.fillRect(x, y + 2, 7, 2);
  g.fillRect(x + 1, y + 1, 3, 1);
  g.fillStyle = CSS[LBLUE];
  g.fillRect(x + 3, y + 2, 2, 1);
  g.fillStyle = CSS[RED];
  g.fillRect(x, y + 3, 1, 1);
}

/* The strip along the bottom, the way arcade shooters kept score: the score, ships in reserve,
   the beam's charge, the high score and the stage. */
export function drawHud(g, game) {
  const p = game.player;
  g.fillStyle = CSS[BLACK];
  g.fillRect(0, PH, W, H - PH);
  g.fillStyle = CSS[DBLUE];
  g.fillRect(0, PH, W, 1);
  const y = PH + 3;
  text(g, '1UP', 2, y, LBLUE, { shadow: false });
  text(g, pad(game.score), 22, y, WHITE, { shadow: false });
  for (let i = 0; i < Math.min(game.lives, 4); i++) drawLife(g, 68 + i * 9, y + 1);
  text(g, 'BEAM', 106, y, LBLUE, { shadow: false });
  const full = p.charge >= 1;
  const lit = Math.floor(p.charge * 16 + 1e-6);
  for (let i = 0; i < 16; i++) {
    let c = DBLUE;
    if (i < lit) c = full ? (game.t % 8 < 4 ? WHITE : LBLUE) : i > 11 ? WHITE : i > 5 ? LBLUE : BLUE;
    g.fillStyle = CSS[c];
    g.fillRect(134 + i * 4, y, 3, 7);
  }
  text(g, 'HI', 204, y, LBLUE, { shadow: false });
  text(g, pad(Math.max(game.score, game.scores[0]?.score ?? 0)), 218, y, WHITE, { shadow: false });
  text(g, `ST${(game.stageIndex % 5) + 1}`, W - 2, y, GREY, { align: 'right', shadow: false });
}

/* Hazard stripes for the boss warning. */
function stripes(g, y, t) {
  g.fillStyle = CSS[RED];
  for (let x = -16 + (t % 16); x < W; x += 16) for (let i = 0; i < 6; i++) g.fillRect(x + i, y + i, 8, 1);
}

export function drawCards(g, game) {
  const s = game.state;
  const t = game.stateT;
  const touch = game.input.touched;
  const centre = (str, y, colour, opts = {}) => text(g, str, MID, y, colour, { align: 'center', ...opts });

  for (const w of game.warnings) if (w.t < 70 && game.t % 10 < 6) text(g, '<!', 3, w.y - 3, RED, { bold: true });

  // The cards after a game sit over the world dimmed, the way an 8-bit machine dimmed it.
  if (s === 'over' || s === 'initials' || s === 'scores') g.drawImage(game.dither, 0, 0);

  if (s === 'ready' && t < 150) {
    centre(`STAGE ${(game.stageIndex % 5) + 1}`, 44, WHITE, { bold: true, scale: 2 });
    centre(game.st.info.name, 66, YELLOW, { bold: true });
    if (game.st.loop) centre(`LOOP ${game.st.loop + 1}`, 78, PINK);
    if (game.t % 30 < 20) centre('GET READY', 94, LBLUE);
    if (game.stageIndex === 0) {
      const help = touch
        ? ['DRAG ANYWHERE TO FLY', 'HOLD ON TO FIRE, LET GO TO BLAST']
        : ['ARROWS OR WASD TO FLY', 'HOLD SPACE TO FIRE AND CHARGE, LET GO TO BLAST'];
      help.forEach((line, i) => centre(line, 122 + i * 11, LGREY));
    }
  } else if (s === 'warning') {
    stripes(g, 4, game.t);
    stripes(g, PH - 10, game.t);
    if (game.t % 30 < 20) centre('WARNING', 52, RED, { bold: true, scale: 2 });
    centre('A HUGE BUG IS APPROACHING FAST', 80, WHITE);
    centre(game.st.info.boss, 96, YELLOW, { bold: true });
  } else if (s === 'boss' && game.boss) {
    game.boss.drawBar(g);
  } else if (s === 'clear') {
    centre('STAGE CLEAR', 40, WHITE, { bold: true, scale: 2 });
    if (t > 40) centre(`${game.st.info.boss} FIXED`, 66, YELLOW);
    if (t > 80 && game.bonus) centre(`NO MISS BONUS ${game.bonus}`, 80, LGREEN);
    if (game.stageIndex % 5 === 4 && t > 130) {
      centre('BACK WHERE IT ALL BEGAN', 104, LBLUE);
      text(g, 'READY.', MID - 18, 118, LBLUE);
      if (game.t % 40 < 20) {
        g.fillStyle = CSS[LBLUE];
        g.fillRect(MID - 18, 128, 6, 7);
      }
    }
  } else if (s === 'over') {
    centre('GAME OVER', 56, RED, { bold: true, scale: 2 });
    centre(`SCORE ${pad(game.score)}`, 84, WHITE);
  } else if (s === 'initials') {
    centre('NEW HIGH SCORE', 32, YELLOW, { bold: true });
    centre('ENTER YOUR INITIALS', 50, WHITE);
    game.entry.letters.forEach((ch, i) => {
      const x = MID - 27 + i * 20;
      const current = i === game.entry.pos;
      if (!current || game.t % 30 < 20) text(g, ch, x, 72, current ? YELLOW : WHITE, { bold: true, scale: 2 });
      if (current) {
        g.fillStyle = CSS[YELLOW];
        g.fillRect(x, 90, 12, 2);
      }
    });
    centre('UP AND DOWN TO CHANGE, FIRE FOR NEXT', 118, LGREY);
    centre('OR JUST TYPE THEM', 130, GREY);
  } else if (s === 'scores') {
    centre('TOP PILOTS', 18, LBLUE, { bold: true, scale: 2 });
    game.scores.forEach((entry, i) => {
      const y = 46 + i * 13;
      const c = entry === game.newEntry ? YELLOW : WHITE;
      text(g, `${i + 1}.`, 88, y, GREY);
      text(g, entry.name, 106, y, c);
      text(g, pad(entry.score), 136, y, c);
      text(g, `STAGE ${entry.stage}`, 190, y, LGREY);
    });
    if (game.t % 40 < 26) centre(touch ? 'TAP TO PLAY AGAIN' : 'PRESS FIRE TO PLAY AGAIN', 124, YELLOW);
    centre(touch ? 'QUIT IS AT THE TOP RIGHT' : 'ESC TO QUIT', 138, GREY);
  }

  if (game.paused) {
    g.drawImage(game.dither, 0, 0);
    centre('PAUSED', 64, WHITE, { bold: true, scale: 2 });
    centre(touch ? 'TAP TO CARRY ON' : 'P TO CARRY ON, ESC TO QUIT', 92, LGREY);
  }
}
