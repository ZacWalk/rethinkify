/* The cabinet around the game: a full-window <dialog> with the screen in it, scaled by a whole
   number of device pixels; the loop that runs the game sixty times a second; and putting the
   page back as it was when the player quits. boot.js calls play() when someone presses start. */

import { LBLUE } from './art.js';
import { Sound } from './audio.js';
import { Game } from './game.js';
import { Input } from './input.js';
import { H, W, text } from './screen.js';

const STEP = 1000 / 60;
let running = false;

function cabinet() {
  const dialog = document.createElement('dialog');
  dialog.className = 'arcade';
  dialog.setAttribute('aria-label', 'Rethinkify, the game');
  dialog.setAttribute('aria-describedby', 'arcade-help');
  const canvas = document.createElement('canvas');
  canvas.className = 'arcade-screen';
  canvas.width = W;
  canvas.height = H;
  canvas.tabIndex = -1;
  canvas.textContent = 'A sideways-scrolling shoot-them-up.';
  const help = document.createElement('p');
  help.id = 'arcade-help';
  help.className = 'visually-hidden';
  help.textContent =
    'A sideways-scrolling shoot-them-up. Arrow keys or WASD fly the ship. Hold space to fire and charge the beam, and let go to fire it. P pauses, M mutes, F goes full screen, and Escape quits.';
  const bar = document.createElement('div');
  bar.className = 'arcade-bar';
  const button = (label) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'arcade-button';
    b.textContent = label;
    return b;
  };
  const mute = button('Mute');
  mute.setAttribute('aria-pressed', 'false');
  const quit = button('Quit');
  bar.append(mute, quit);
  dialog.append(canvas, help, bar);
  return { dialog, canvas, mute, quit };
}

/* Takes over the window with the game. `audio` is an AudioContext made in the click that
   started it; `opener` gets the focus back afterwards. */
export async function play({ audio = null, opener = null } = {}) {
  if (running) {
    audio?.close().catch(() => {});
    return;
  }
  running = true;
  const { dialog, canvas, mute, quit } = cabinet();
  const g = canvas.getContext('2d', { alpha: false });
  g.imageSmoothingEnabled = false;
  const input = new Input(dialog, canvas);
  const sound = Sound.start(audio);
  let game = null;
  let raf = 0;
  let closed = false;

  // Whole device pixels per game pixel, so every pixel is the same size; below one, as large
  // as fits.
  const fit = () => {
    const dpr = window.devicePixelRatio || 1;
    const k = Math.min((window.innerWidth * dpr * 0.96) / W, (window.innerHeight * dpr * 0.9) / H);
    const scale = k >= 1 ? Math.floor(k) : k;
    const width = (W * scale) / dpr;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${(H * scale) / dpr}px`;
    input.scale = width / W;
  };
  const fullscreen = () => {
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
    else document.documentElement.requestFullscreen?.().catch(() => {});
  };
  // Escape quits and F toggles full screen, except while typing initials. Escape is handled
  // here rather than left to the dialog's own cancel, which not every browser (or embedded
  // browser) sends.
  const onKey = (e) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      dialog.close();
    } else if (e.code === 'KeyF' && !e.repeat && game?.state !== 'initials') fullscreen();
  };
  const onVisibility = () => {
    if (document.hidden) game?.setPaused(true);
  };
  const onBlur = () => game?.setPaused(true);
  const showMute = (muted) => mute.setAttribute('aria-pressed', String(muted));

  const close = () => {
    if (closed) return;
    closed = true;
    cancelAnimationFrame(raf);
    input.detach();
    window.removeEventListener('resize', fit);
    window.removeEventListener('blur', onBlur);
    document.removeEventListener('visibilitychange', onVisibility);
    sound.then((s) => s.close());
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
    if (dialog.open) dialog.close();
    dialog.remove();
    document.documentElement.classList.remove('arcade-open');
    running = false;
    opener?.focus();
  };

  dialog.addEventListener('close', close);
  dialog.addEventListener('keydown', onKey);
  quit.addEventListener('click', () => dialog.close());
  mute.addEventListener('click', () => {
    if (game) showMute(game.sound.toggleMute());
    canvas.focus();
  });
  window.addEventListener('resize', fit);
  window.addEventListener('blur', onBlur);
  document.addEventListener('visibilitychange', onVisibility);

  document.body.append(dialog);
  document.documentElement.classList.add('arcade-open');
  dialog.showModal();
  canvas.focus();
  fit();

  // LOADING, the way the C64 put it, while the sound chip starts on the audio thread.
  g.fillStyle = '#000';
  g.fillRect(0, 0, W, H);
  text(g, 'LOADING', W / 2, H / 2 - 4, LBLUE, { align: 'center' });

  const calm = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const chip = await sound;
  if (closed) return;
  game = new Game(g, input, chip, calm);
  game.onMute = showMute;

  let last = performance.now();
  let owed = 0;
  const frame = (now) => {
    raf = requestAnimationFrame(frame);
    owed += Math.min(250, now - last);
    last = now;
    try {
      while (owed >= STEP) {
        game.update();
        owed -= STEP;
      }
      game.render();
    } catch (err) {
      console.error(err);
      dialog.close();
    }
  };
  raf = requestAnimationFrame(frame);
}
