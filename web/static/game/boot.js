/* The only script the page loads up front. It shows the title screen's PRESS START button,
   which the page hides for anyone without scripts, and loads the game (arcade.js and the rest)
   only when someone reaches for the button. */

const button = document.querySelector('[data-press-start]');

if (button && 'HTMLDialogElement' in window) {
  // The best score on this machine (see SCORES_KEY in game.js), as an arcade would show it.
  try {
    const best = JSON.parse(localStorage.getItem('rethinkify-scores') ?? 'null')?.[0]?.score;
    const hi = button.querySelector('[data-hi]');
    if (hi && Number.isFinite(best)) hi.textContent = `HI ${String(best).padStart(7, '0')}`;
  } catch {
    // No storage, or nothing saved yet: the button just says PRESS START.
  }

  let loading = null;
  let starting = false;
  const load = () => (loading ??= import('./arcade.js'));
  button.addEventListener('pointerenter', load, { once: true });
  button.addEventListener('focus', load, { once: true });
  button.addEventListener('click', async () => {
    // A second tap while the game is still loading would make a second AudioContext.
    if (starting) return;
    starting = true;
    // Browsers only let sound start inside a click, so make the AudioContext here and now.
    const Context = window.AudioContext ?? window.webkitAudioContext;
    let audio = null;
    try {
      audio = Context ? new Context({ latencyHint: 'interactive' }) : null;
      audio?.resume().catch(() => {});
    } catch {
      audio = null;
    }
    try {
      const { play } = await load();
      await play({ audio, opener: button });
    } catch (err) {
      audio?.close().catch(() => {});
      loading = null;
      console.error(err);
    } finally {
      starting = false;
    }
  });
  button.hidden = false;
}
