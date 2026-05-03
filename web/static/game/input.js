/* The controls: the keyboard, the first gamepad, and a finger (or a mouse) dragged on the
   screen. The game reads actions from here once a frame, then calls endFrame(). */

const KEYS = {
  left: ['ArrowLeft', 'KeyA'],
  right: ['ArrowRight', 'KeyD'],
  up: ['ArrowUp', 'KeyW'],
  down: ['ArrowDown', 'KeyS'],
  fire: ['Space', 'KeyZ', 'KeyX', 'KeyJ', 'Enter', 'NumpadEnter'],
  pause: ['KeyP'],
  mute: ['KeyM'],
  full: ['KeyF'],
};
const GAME_KEYS = new Set(Object.values(KEYS).flat());

export class Input {
  constructor(dialog, canvas) {
    this.held = new Set();
    this.pressed = new Set();
    this.typed = [];
    this.drag = { x: 0, y: 0 };
    this.finger = null;
    this.tapped = false;
    this.touched = false;
    this.scale = 1;
    this.pad = { x: 0, y: 0, fire: false };
    this.padWas = {};
    this.listeners = [];
    const on = (target, type, fn) => {
      target.addEventListener(type, fn);
      this.listeners.push([target, type, fn]);
    };

    on(dialog, 'keydown', (e) => {
      // A focused button keeps Space and Enter for itself.
      if (e.target instanceof HTMLButtonElement && (e.code === 'Space' || e.code === 'Enter')) return;
      if (GAME_KEYS.has(e.code)) e.preventDefault();
      if (!e.repeat) {
        this.pressed.add(e.code);
        // Typed letters come from the key's character, so they follow the keyboard's layout;
        // the controls above go by where keys are, which suits WASD on any layout.
        if (/^[a-z0-9]$/i.test(e.key) && !e.ctrlKey && !e.metaKey && !e.altKey) this.typed.push(e.key.toUpperCase());
      }
      this.held.add(e.code);
    });
    on(dialog, 'keyup', (e) => this.held.delete(e.code));
    on(window, 'blur', () => this.held.clear());

    // A finger steers by dragging: the ship moves as far as the finger does, wherever it
    // started, so a thumb never covers the ship. Holding on fires; letting go fires the beam.
    on(canvas, 'pointerdown', (e) => {
      if (this.finger) return;
      this.finger = { id: e.pointerId, x: e.clientX, y: e.clientY, moved: 0 };
      if (e.pointerType !== 'mouse') this.touched = true;
      canvas.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    on(canvas, 'pointermove', (e) => {
      const f = this.finger;
      if (!f || f.id !== e.pointerId) return;
      const dx = (e.clientX - f.x) / this.scale;
      const dy = (e.clientY - f.y) / this.scale;
      this.drag.x += dx;
      this.drag.y += dy;
      f.moved += Math.abs(dx) + Math.abs(dy);
      f.x = e.clientX;
      f.y = e.clientY;
    });
    const lift = (e) => {
      if (this.finger?.id !== e.pointerId) return;
      if (this.finger.moved < 6) this.tapped = true;
      this.finger = null;
    };
    on(canvas, 'pointerup', lift);
    on(canvas, 'pointercancel', lift);
  }

  /* Reads the first gamepad, so its stick, pad and buttons count as the same actions. */
  poll() {
    const pad = [...(navigator.getGamepads?.() ?? [])].find((p) => p?.connected);
    const now = { x: 0, y: 0, fire: false, pause: false, up: false, down: false, left: false, right: false };
    if (pad) {
      const [ax = 0, ay = 0] = pad.axes;
      const b = (i) => !!pad.buttons[i]?.pressed;
      now.left = b(14) || ax < -0.4;
      now.right = b(15) || ax > 0.4;
      now.up = b(12) || ay < -0.4;
      now.down = b(13) || ay > 0.4;
      now.x = Math.abs(ax) > 0.25 ? ax : (now.right ? 1 : 0) - (now.left ? 1 : 0);
      now.y = Math.abs(ay) > 0.25 ? ay : (now.down ? 1 : 0) - (now.up ? 1 : 0);
      now.fire = b(0) || b(1) || b(2) || b(3) || b(7);
      now.pause = b(9);
    }
    for (const k of ['fire', 'pause', 'up', 'down', 'left', 'right']) {
      if (now[k] && !this.padWas[k]) this.pressed.add(`pad:${k}`);
    }
    this.padWas = now;
    this.pad = now;
  }

  is(action) {
    return KEYS[action].some((c) => this.held.has(c));
  }

  /* An action begun since the last frame. */
  took(action) {
    return (KEYS[action]?.some((c) => this.pressed.has(c)) ?? false) || this.pressed.has(`pad:${action}`);
  }

  /* Fire, a tap, or a gamepad button: anything that means "go on". */
  confirmed() {
    return this.took('fire') || this.tapped;
  }

  /* Which way the keys or the stick point, at most 1 long. */
  move() {
    let x = (this.is('right') ? 1 : 0) - (this.is('left') ? 1 : 0) + this.pad.x;
    let y = (this.is('down') ? 1 : 0) - (this.is('up') ? 1 : 0) + this.pad.y;
    const len = Math.hypot(x, y);
    if (len > 1) {
      x /= len;
      y /= len;
    }
    return { x, y };
  }

  get fire() {
    return this.is('fire') || this.pad.fire || !!this.finger;
  }

  endFrame() {
    this.pressed.clear();
    this.typed.length = 0;
    this.drag.x = 0;
    this.drag.y = 0;
    this.tapped = false;
  }

  detach() {
    for (const [target, type, fn] of this.listeners) target.removeEventListener(type, fn);
  }
}
