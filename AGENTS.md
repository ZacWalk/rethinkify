# Copilot Instructions

You are an expert software engineer who prioritizes **Theory Building** and **Design for Change**. Your goal is to write code that is not just syntactically correct, but creates a clear, shared mental model for both human developers and future AI agents.

## Project Overview

This is the personal site of **Zac Walker** — a single page covering his sites, open-source
projects, professional work and education. The only JavaScript is the title screen's game,
which loads when someone presses start; the page is complete without it.

The page is a journey back in time. The top is an 80s title screen: the attract mode of an arcade
shooter in the spirit of R-Type (Irem, 1987), drawn in C64 pixels. Deep space drifts past over
the hull of something huge, the ship idles beside a wave of enemies, and PRESS START plays the
game. The portrait sits in a chrome pipe frame with energy bars and a red HUD readout, after the
C64 Terminator 2 cartridge (Ocean, 1991), under a chrome wordmark. The content already runs from
now back to 1992, and the page ends at a Commodore 64 start-up screen, where it all began. Keep
that arc when adding things: retro computers first, 80s sci-fi second, and never so loud that the
project stories are hard to read.

**Live URL:** rethinkify (GCP project), deployed in `europe-west1`.

## Tech Stack

- **Platform:** Google App Engine (Python 3.13 Standard Environment)
- **Framework:** Flask + gunicorn
- **Templating:** Jinja2 (via Flask)
- **Styling:** Hand-written CSS in `web/static/styles.css`
- **Game:** plain ES modules in `web/static/game`, no build step and no libraries, with its
  sound from an AudioWorklet

## Project Structure

```
tools/
├── hero_art.py        # Draws the title-screen pixel art (dev only)
└── requirements.txt   # Its one dependency, Pillow; never deployed
web/
├── app.yaml           # GAE configuration (python313, gunicorn entrypoint, static caching)
├── main.py            # Flask application entry point
├── requirements.txt   # Python dependencies (Flask, gunicorn)
├── static/            # Static assets
│   ├── favicon-c64.svg
│   ├── game/          # The title screen's game (see "The game" below)
│   ├── images/
│   └── styles.css     # Main stylesheet
└── templates/
    ├── index.html     # Single-page Jinja2 template
    ├── error.html     # 404 / 500 page, a C64 error screen
    └── sitemap.xml
```

## Routes

- `GET /`            — renders the page
- `GET /health`      — returns `ok` (200) for uptime checks
- `GET /robots.txt`  — generated
- `GET /sitemap.xml` — generated
- `GET /favicon.ico` — serves `static/favicon-c64.svg`
- `GET /game/<version>/<file>.js` — the game's modules, only under the current version token

## Conventions

- A strict `Content-Security-Policy` is set in `main.py`. `script-src 'self'` lets the game's
  modules load from this origin, and nothing inline runs. Adding any other script (analytics
  included) means changing that header.
- The game's modules import each other by relative URL, so they are served under a version
  token in the path, `/game/<token>/boot.js`, taken from a hash of every file in
  `static/game`. Every import then carries it. `main.py` serves only the current token, which
  browsers may keep for 30 days, and returns 404 for any other, so two deploys' modules never
  meet in one page. Keep the game in that one flat folder.
- `style-src` has no `'unsafe-inline'`, so browsers silently drop `style="..."` attributes. Put
  every style in `styles.css` — per-card colours are modifier classes such as
  `.site-card--turbo8bit`.
- `styles.css` is requested with a `?v=` token taken from a hash of its contents, so `app.yaml`
  can cache static files for days without serving a stale stylesheet. Not the file's mtime:
  App Engine stamps every deployed file 1980-01-01, so that would never change.
- Every `<img>` carries explicit `width`/`height` to avoid layout shift.
- Images are WebP (or SVG for charts and diagrams), at most 1200px wide. Project images come
  from the screenshots each repo publishes; `win32-desktop.webp` is a composite of four of them
  (the editor, potato, system-stuff and ergo-active) on a Windows 95 teal desktop. `app.yaml`
  caches `/static/images` for 30 days and images carry no `?v=` token, so when a picture
  changes, give it a new file name and delete the old one. Reusing a name serves the old picture
  to returning visitors. The same goes for the favicon, cached for 7 days. Delete pictures the
  page stops using; they stay in git history.
- No CSS framework and no build step. Anything added to `styles.css` must be used; the file has
  been pruned of dead rules once already.

## Look

- **Palette:** the C64's own colours, as the Terminator 2 cartridge used them: blues for chrome
  and links, amber, brown and red for fire, all as `--c64-*` custom properties. The page
  background drifts from near-black to C64 blue on the way down. Text colours all pass WCAG AA;
  the real C64 text colour (the border, `--c64-border`) does not, so C64 text uses
  `--c64-text` instead.
- **Type:** Sixtyfour (a C64 face) for the wordmark and the small C64 details (`LOAD` lines,
  timeline years, the footer screen), VT323 (a DEC VT320 terminal face) for section and theme
  headings, Barlow for reading, Share Tech Mono for HUD labels, captions and meta. Sixtyfour is
  heavy, so it stays off headings that have to be read; its `SCAN` axis opens up scan lines,
  with 100 (solid) the default on `body` and 45 on the hero wordmark. Older sections look like
  older screens through their phosphor instead: the Work heading is amber and the Education
  heading green, before the C64's blue at the bottom.
- **Title screen:** `hero-space.webp`, `hero-depths.webp` and `hero-hull.webp` (far stars,
  nearer stars with distant hulks, and the hull underneath), `hero-ship.webp` and
  `hero-wave.webp` (two frames each, one above the other) are drawn by `tools/hero_art.py` in
  C64 multicolour pixels and scaled by a whole number with `image-rendering: pixelated`, so
  every pixel stays the same size; `--px` in `.hero-scene` is one picture pixel. The hull's
  underside is dark because the wordmark sits on it, and the wave is silver, not its stage's
  red, so the title screen stays calm beside the page. The ship is the game's, drawn from the
  same rows (`SHIP_ART`) in both places, so change them together. To change the art, change
  the script and save under new names.
- **C64 touches:** every section heading has a `LOAD"NAME",8,1` line whose status steps through
  `SEARCHING FOR`, `LOADING` and `READY.` as it scrolls in. The footer is a C64 start-up
  screen with its links as `LOAD` commands, and the error page is a C64 error screen.
- **Not too much:** no glow, no neon, no gradients on body text, and cards stay calm. Keep new
  effects to headings, frames and the edges of the page.

## Project themes

The Projects section takes its cue from Fabrice Bellard's home page: plain, unassuming, and
dense with links. There are no cards. Each interest is a short first-person story with the
achievements linked inline, and one picture.

Each theme is a `<section class="theme">` with a `.theme-head` (a `NN/NN` counter and an `h3`)
and a `.theme-body`: a `.theme-story` of a few short paragraphs, and one
`<figure class="theme-shot">`, a single picture in a chrome frame (`.theme-shot-screen`) with a
short `figcaption`. On wide screens the picture sits right of the story and sticks under the
bars while you read it; on phones it comes first. The `.theme-nav` strip under the section intro
links to every theme with a short label. Adding or removing a theme means updating the strip,
every counter, and the timeline names in the "where you are" block of `styles.css`.

The links are the point. Every project name links to its repo or site, and work in other
people's projects links to the evidence: the GCC commits and release notes, the Electron pull
requests, Oxidizer. Links into `github.com/ZacWalk/` never break across lines, because names
such as `spike-db` would otherwise break at the hyphen, so keep those link texts to the repo's
name. Not every repo needs a sentence; the page is meant to stay short.

Only public repos get a link. Check a repo's visibility
(`gh repo view ZacWalk/<name> --json visibility`) before deploying, or the link 404s. Projects
whose repos are still private wait in `parked/projects.html`, with their pictures in
`parked/images`, and each has a note saying which story it joins. `parked/` is git-ignored,
because this repo is public, and it is never deployed. Some repos stay private on purpose; they
are listed in the git-ignored `todo.md` (section 4), so their names stay out of this public
repo. Leave them off the page, and do not quote real trading P&L or account sizes.

## Navigation

A sticky `.topbar` links to the four sections, and inside Projects the `.theme-nav` strip sticks
under it, then leaves with the section. Their heights are `--topbar-h` and `--strip-h`, and
`html` has a `scroll-padding-top` to match, so anchor jumps and keyboard focus land below both.
The strip stays one row: on narrow screens it scrolls sideways and fades at whichever edge has
more chips.

Both bars light up the section or theme crossing a line 40% of the way down the window. This is
a scroll-driven animation too: `timeline-scope` on `.page-shell` makes each section's named
`view-timeline` visible to the links, and each link animates on its own section's timeline. The
lit range stretches over the gap to the next section, so the gaps live in `--section-gap` and
`--theme-gap`; change them there.

## Scroll animation

Scrolling is the journey, so most of the motion is tied to it. On load the title screen powers
up once: the portrait frame switches on like a CRT, a red scan line runs down it and the HUD
readout prints. From then on the scene drifts past like an arcade attract mode, the nearer layers
faster and a picture pixel at a time, while the ship idles and the wave flaps; that is the one
motion on the page tied to time rather than scrolling. As you scroll, an energy bar fills
across the top, each section's `LOAD` line steps to `READY.`, section headings draw in raster
lines, theme headings type themselves in with a blinking cursor, each theme's picture steps in at
a low frame rate while its scan lines fade, and at the bottom the C64 boots: loading stripes run
through its border and the screen prints a line at a time.

It is all CSS (`animation-timeline: view()` / `scroll(root)` for the scroll-driven parts); the
only script is the game, and the page's own motion stays in CSS. Everything that moves sits inside
`prefers-reduced-motion: no-preference`, and the scroll-driven parts also inside
`@supports (animation-timeline: view())`, so the page without them is complete and holds
still. Keep animations stepped (`steps()`), not eased; that is the 8-bit look. Animate
`translate`, not `transform`, which the hover lift uses. `.page-shell` must stay
`overflow: clip`: `overflow: hidden` makes it a scroll container, and every `view()` timeline
would bind to it and freeze, and the sticky bars would stop sticking. The same goes for
anything that holds an animated element, such as `.theme-shot`: with `hidden`, the scan lines
over its picture would bind to the figure, which never scrolls, so they would never play. Use
`clip` for any new box that needs its corners trimmed. A sticky element's `view()` timeline
stops while it is stuck, so the picture's animations finish well before it sticks; keep them
short.

## The game

PRESS START on the title screen plays a sideways-scrolling shoot-'em-up in the spirit of R-Type:
hold fire to shoot and charge the beam, let go to fire it, pick up pods and weapons, and meet a
boss at the end of every stage. It is a homage, not a copy: every picture, stage, tune and sound
effect is made by code from a seed.

The five stages run back in time like the page: The Cloud, Electron (Teams purple), Wall Street
(amber screens), Shareware (Windows 95 grey) and the Commodore 64 (its own blue). Each boss is a
bug of its era, and the last is a C64 `?SYNTAX ERROR` whose cursor is its weak spot; beating it
ends on `READY.`, as the page does, and the loop starts again, harder.

The machines it all started on stand on the floor of every stage, as solid as the hull: an IBM
PC with DOS 4.0z's prompt on its screen, a Commodore 64 at `READY.` and a ZX81 showing its K,
with their cursors blinking. More old computers glow in the distant skyline. Robots march along
the floor, chrome endoskeletons after the machines of Terminator 2 (1991), whose C64 cartridge
the title screen's portrait frame comes from. They stop to fire plasma, raising their rifles to
follow the ship, and when one is destroyed its skull bounces along the floor.

The modules in `web/static/game`, each with one job:

- `boot.js` — the only script the page loads up front. It shows the PRESS START button, which is
  `hidden` in the HTML so it never shows without scripts, makes the AudioContext inside the
  click (browsers only start sound there), and imports `arcade.js` only then.
- `arcade.js` — the cabinet: a modal `<dialog>` over the whole window, the screen scaled by a
  whole number of device pixels, the fixed 60 Hz loop, and putting the page back on Escape.
- `game.js` — the game: the run from GET READY to the high score table, the ship and its
  weapons, collisions and effects. `cast.js` is the enemies and their waves, `boss.js` the
  bosses, `hud.js` the HUD and the cards between stages, `input.js` the keyboard, gamepad and
  touch.
- `art.js` — every picture, as pure functions of a seed that return palette-index Bitmaps:
  the palette, a 5x7 font, the ship, a kit for sculpting creatures and bosses, the robots, the
  old computers and the terrain. `screen.js` turns Bitmaps into canvases and draws text.
- `stage.js` — what each stage is and where things happen: names, colours, terrain heights
  (always leaving `MIN_GAP` rows to fly through, computers included), where the old computers
  stand, and the schedule of waves.
- `chip.js` — the sound chip, an AudioWorklet: SID-like voices (pulse with PWM, saw, triangle,
  noise, ADSR and a resonant filter) driven a frame at a time like a C64 player routine, a
  composer that writes each stage's tune from its seed, and the sound effects. `audio.js` is
  the game's side of it.

`art.js`, `stage.js` and `chip.js` touch no DOM, so they also run in Node: draw the art to PNGs,
or render the chip's tunes to WAV, to check a change. The game skips screen shake for
`prefers-reduced-motion`, and keeps its top five scores in `localStorage` under
`rethinkify-scores`, which `boot.js` reads too, for the HI under the button.

## Content

- The voice is first person, casual and self-deprecating. Keep it that way; do not rewrite the
  project stories into marketing copy.
- Project claims (speeds, versions, whether a bug is fixed) come from each repo's README. Check
  the README before changing a number; where the README defers to a doc (nnc's speeds live in
  `docs/models.md`), use that doc. Claims about work in other projects come from the commits,
  pull requests and release notes they link to. What only Zac can vouch for (writing the
  Windows on Arm proposal, Merrill Lynch in 2007) is told as his own account.
- The themes, and their order, are deliberate: Photos & video (Diffractor), AI & machine
  learning, Markets & money, Compilers & emulators, and Win32 apps last, ending on the games.
  Do not sort them. Each story runs roughly in the order things happened.

## Local Development

```powershell
.\dd.ps1 run              # http://localhost:8081
.\dd.ps1 format           # Black, on web/ and tools/
.\dd.ps1 format --check   # Black, no writes
.\.venv\Scripts\python.exe -m pip install -r tools\requirements.txt   # once, for the art script
.\.venv\Scripts\python.exe tools\hero_art.py --preview $env:TEMP     # redraw the title-screen art
```

`dd.ps1` uses `.venv` when present. Run `format` before committing Python changes.

`todo.md` holds review notes about private repos, so it is git-ignored like `parked/`. Keep it
out of commits.

## Deployment

```powershell
.\dd.ps1 deploy
```

The target App Engine project is hard-pinned in `dd.ps1`; do not parameterise it.

## Key Files

- [web/main.py](web/main.py) - Flask routes, security headers, asset and game versioning
- [web/app.yaml](web/app.yaml) - GAE runtime config (python313, gunicorn, static caching)
- [web/templates/index.html](web/templates/index.html) - The page
- [web/templates/error.html](web/templates/error.html) - 404 / 500 page, as C64 error screens
- [web/static/styles.css](web/static/styles.css) - All site styles, palette and animation
- [web/static/game/](web/static/game/) - The title screen's game; `boot.js` is the way in
- [tools/hero_art.py](tools/hero_art.py) - Draws the title-screen pixel art
