from __future__ import annotations

import os
from datetime import date
from pathlib import Path

from flask import Flask, Response, render_template, send_from_directory

app = Flask(__name__)

_IS_PROD = os.getenv("GAE_ENV", "").startswith("standard")

# No scripts run on this site, so the policy can stay strict. JSON-LD is a data
# block rather than executable script, so script-src 'none' does not affect it.
_CSP = (
    "default-src 'none'; "
    "img-src 'self'; "
    "style-src 'self' https://fonts.googleapis.com; "
    "font-src https://fonts.gstatic.com; "
    "script-src 'none'; "
    "base-uri 'none'; "
    "form-action 'none'; "
    "frame-ancestors 'self'"
)


@app.after_request
def _security_headers(response: Response) -> Response:
    response.headers.setdefault("X-Content-Type-Options", "nosniff")
    response.headers.setdefault("X-Frame-Options", "SAMEORIGIN")
    response.headers.setdefault("Referrer-Policy", "strict-origin-when-cross-origin")
    response.headers.setdefault("Content-Security-Policy", _CSP)
    response.headers.setdefault(
        "Permissions-Policy",
        "geolocation=(), microphone=(), camera=(), payment=()",
    )
    if _IS_PROD:
        response.headers.setdefault(
            "Strict-Transport-Security", "max-age=31536000; includeSubDomains"
        )
    return response


# Canonical site origin used in absolute URLs (sitemap, canonical tags, OG, JSON-LD).
SITE_ORIGIN = "https://rethinkify.com"

_STYLESHEET = Path(app.static_folder or "static") / "styles.css"


def _asset_version() -> str:
    """Cache-busting token so a long static expiry never serves a stale stylesheet."""
    try:
        return str(int(_STYLESHEET.stat().st_mtime))
    except OSError:
        return "0"


_ASSET_VERSION = _asset_version()


@app.context_processor
def _template_globals() -> dict[str, object]:
    return {
        "site_origin": SITE_ORIGIN,
        "asset_version": _ASSET_VERSION if _IS_PROD else _asset_version(),
        "current_year": date.today().year,
    }


@app.get("/")
def index() -> str:
    return render_template(
        "index.html",
        canonical_url=f"{SITE_ORIGIN}/",
    )


@app.get("/favicon.ico")
def favicon() -> Response:
    return send_from_directory(
        app.static_folder or "static", "favicon.svg", mimetype="image/svg+xml"
    )


@app.errorhandler(404)
def not_found(_error: Exception) -> tuple[str, int]:
    return (
        render_template(
            "error.html",
            code="404",
            title="Page not found",
            message="That page has been refactored out of existence.",
        ),
        404,
    )


@app.errorhandler(500)
def server_error(_error: Exception) -> tuple[str, int]:
    return (
        render_template(
            "error.html",
            code="500",
            title="Something broke",
            message="Works on my machine. Try again in a moment.",
        ),
        500,
    )


@app.get("/health")
def health() -> tuple[str, int]:
    return "ok", 200


@app.get("/robots.txt")
def robots_txt() -> Response:
    body = (
        "User-agent: *\n"
        "Allow: /\n"
        "Disallow: /health\n"
        f"Sitemap: {SITE_ORIGIN}/sitemap.xml\n"
    )
    return Response(body, mimetype="text/plain")


@app.get("/sitemap.xml")
def sitemap_xml() -> Response:
    today = date.today().isoformat()
    pages = [
        {
            "loc": f"{SITE_ORIGIN}/",
            "lastmod": today,
            "changefreq": "weekly",
            "priority": "1.0",
        },
    ]
    xml = render_template("sitemap.xml", pages=pages)
    return Response(xml, mimetype="application/xml")


if __name__ == "__main__":
    # Local dev only; production uses gunicorn via app.yaml entrypoint.
    app.run(port=8081, host="localhost", debug=True)
