# Copilot Instructions

You are an expert software engineer who prioritizes **Theory Building** and **Design for Change**. Your goal is to write code that is not just syntactically correct, but creates a clear, shared mental model for both human developers and future AI agents.

## Project Overview

This is the personal site of **Zac Walker** — a single page covering his sites, open-source
projects, professional work and education. No JavaScript is served.

**Live URL:** rethinkify (GCP project), deployed in `europe-west1`.

## Tech Stack

- **Platform:** Google App Engine (Python 3.13 Standard Environment)
- **Framework:** Flask + gunicorn
- **Templating:** Jinja2 (via Flask)
- **Styling:** Hand-written CSS in `web/static/styles.css`

## Project Structure

```
web/
├── app.yaml           # GAE configuration (python313, gunicorn entrypoint, static caching)
├── main.py            # Flask application entry point
├── requirements.txt   # Python dependencies (Flask, gunicorn)
├── static/            # Static assets
│   ├── favicon.svg
│   ├── images/
│   └── styles.css     # Main stylesheet
└── templates/
    ├── index.html     # Single-page Jinja2 template
    ├── error.html     # 404 / 500 page
    └── sitemap.xml
```

## Routes

- `GET /`            — renders the page
- `GET /health`      — returns `ok` (200) for uptime checks
- `GET /robots.txt`  — generated
- `GET /sitemap.xml` — generated
- `GET /favicon.ico` — serves `static/favicon.svg`

## Conventions

- A strict `Content-Security-Policy` is set in `main.py` with `script-src 'none'`. Adding any
  script (analytics included) means changing that header.
- `styles.css` is requested with a `?v=` token derived from its mtime, so `app.yaml` can cache
  static files for days without serving a stale stylesheet.
- Every `<img>` carries explicit `width`/`height` to avoid layout shift.
- No CSS framework and no build step. Anything added to `styles.css` must be used; the file has
  been pruned of dead rules once already.

## Content

- The voice is first person, casual and self-deprecating. Keep it that way; do not rewrite the
  project stories into marketing copy.
- Project claims (speeds, versions, whether a bug is fixed) come from each repo's README. Check
  the README before changing a number.
- The order of **My Projects** is deliberate: Diffractor first, then AI/ML, systems and
  performance, Windows desktop apps, and retro projects last. The grouping is implicit — there
  are no group headings, so do not sort the list.

## Local Development

```powershell
.\dd.ps1 run              # http://localhost:8081
.\dd.ps1 format           # Black
.\dd.ps1 format --check   # Black, no writes
```

`dd.ps1` uses `.venv` when present. Run `format` before committing Python changes.

## Deployment

```powershell
.\dd.ps1 deploy
```

The target App Engine project is hard-pinned in `dd.ps1`; do not parameterise it.

## Key Files

- [web/main.py](web/main.py) - Flask routes, security headers, asset versioning
- [web/app.yaml](web/app.yaml) - GAE runtime config (python313, gunicorn, static caching)
- [web/templates/index.html](web/templates/index.html) - The page
- [web/templates/error.html](web/templates/error.html) - 404 / 500 page
- [web/static/styles.css](web/static/styles.css) - All site styles
