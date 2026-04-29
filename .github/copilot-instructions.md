# Copilot Instructions

You are an expert software engineer who prioritizes **Theory Building** and **Design for Change**. Your goal is to write code that is not just syntactically correct, but creates a clear, shared mental model for both human developers and future AI agents.

## Project Overview

This is the marketing / landing site for **Rethinkify**, an independent technology stock research site focused on semiconductors, infrastructure, software, and AI investment research. Currently a single coming-soon page.

**Live URL:** rethinkify (GCP project), deployed in `europe-west1`.

## Tech Stack

- **Platform:** Google App Engine (Python 3.13 Standard Environment)
- **Framework:** Flask + gunicorn
- **Templating:** Jinja2 (via Flask)
- **Styling:** Hand-written CSS in `web/static/styles.css`

## Project Structure

```
web/
├── app.yaml           # GAE configuration (python313, gunicorn entrypoint)
├── main.py            # Flask application entry point
├── requirements.txt   # Python dependencies (Flask, gunicorn)
├── static/            # Static assets
│   └── styles.css     # Main stylesheet
└── templates/
    └── index.html     # Single-page Jinja2 template
```

## Routes

- `GET /`       — renders the coming-soon landing page
- `GET /health` — returns `ok` (200) for uptime checks

## Local Development

```powershell
.\dd.ps1 run
# Runs on http://localhost:8081
```

## Deployment

```powershell
.\dd.ps1 deploy
```

## Key Files

- [web/main.py](web/main.py) - Flask routes
- [web/app.yaml](web/app.yaml) - GAE runtime config (python313, gunicorn)
- [web/templates/index.html](web/templates/index.html) - Landing page template
- [web/static/styles.css](web/static/styles.css) - All site styles
