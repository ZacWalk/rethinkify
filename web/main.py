from __future__ import annotations

import os
from datetime import date

from flask import Flask, Response, render_template


app = Flask(__name__)

_IS_PROD = os.getenv("GAE_ENV", "").startswith("standard")


@app.after_request
def _security_headers(response: Response) -> Response:
    response.headers.setdefault("X-Content-Type-Options", "nosniff")
    response.headers.setdefault("X-Frame-Options", "SAMEORIGIN")
    response.headers.setdefault("Referrer-Policy", "strict-origin-when-cross-origin")
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


@app.get("/")
def index() -> str:
    return render_template(
        "index.html",
        canonical_url=f"{SITE_ORIGIN}/",
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
        {"loc": f"{SITE_ORIGIN}/", "lastmod": today, "changefreq": "weekly", "priority": "1.0"},
    ]
    xml = render_template("sitemap.xml", pages=pages)
    return Response(xml, mimetype="application/xml")


if __name__ == "__main__":
    # Local dev only; production uses gunicorn via app.yaml entrypoint.
    app.run(port=8081, host="localhost", debug=True)