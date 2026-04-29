from __future__ import annotations

import os
from datetime import date

from flask import Flask, Response, render_template


app = Flask(__name__)

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
    if os.getenv("GAE_ENV", "").startswith("standard"):
        app.run()  # production
    else:
        app.run(port=8081, host="localhost", debug=True)  # localhost