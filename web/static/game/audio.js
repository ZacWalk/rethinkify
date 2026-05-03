/* The game's link to its sound chip. chip.js runs on the audio thread; this starts it and
   passes it the game's requests. If the browser cannot run it, every call quietly does nothing
   and the game plays in silence. */

const VOLUME = 0.9;

export class Sound {
  /* `context` is an AudioContext made in the click that started the game, since browsers only
     let sound start from a click or a key press. Even when the chip cannot start, the Sound
     keeps the context, so close() still closes it. */
  static async start(context) {
    if (!context?.audioWorklet) return new Sound(context);
    try {
      await context.audioWorklet.addModule(new URL('./chip.js', import.meta.url));
      const node = new AudioWorkletNode(context, 'chip', { numberOfInputs: 0, outputChannelCount: [2] });
      const gain = context.createGain();
      gain.gain.value = VOLUME;
      node.connect(gain).connect(context.destination);
      return new Sound(context, node, gain);
    } catch {
      return new Sound(context);
    }
  }

  constructor(context, node = null, gain = null) {
    this.context = context;
    this.node = node;
    this.gain = gain;
    this.muted = false;
    this.charging = 0;
  }

  send(msg) {
    this.node?.port.postMessage(msg);
  }

  /* Starts a tune: 'stage' and 'boss' loop; 'ready', 'clear' and 'over' play once, then hand
     over to `next` if there is one. */
  tune(tune, seed, next = null) {
    this.send({ type: 'tune', tune, seed, next });
  }

  silence() {
    this.send({ type: 'silence' });
  }

  sfx(name) {
    this.send({ type: 'sfx', name });
  }

  /* How far the beam is charged, 0 to 1; sent only when it changes enough to hear. */
  charge(level) {
    const stepped = Math.round(level * 24) / 24;
    if (stepped === this.charging) return;
    this.charging = stepped;
    this.send({ type: 'charge', level: stepped });
  }

  toggleMute() {
    this.muted = !this.muted;
    this.gain?.gain.setTargetAtTime(this.muted ? 0 : VOLUME, this.context.currentTime, 0.02);
    return this.muted;
  }

  pause() {
    this.context?.suspend().catch(() => {});
  }

  resume() {
    this.context?.resume().catch(() => {});
  }

  close() {
    this.node?.disconnect();
    this.context?.close().catch(() => {});
  }
}
