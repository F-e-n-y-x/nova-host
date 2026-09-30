#!/usr/bin/env python3
"""Put Nova's browser client interface on a built moonlight-web-stream frontend.

Usage: apply-theme.py STATIC_DIR FONTS_DIR

STATIC_DIR is moonlight-web-stream's built web frontend (its dist/, installed as static/).
FONTS_DIR holds the Geist Sans woff2 files (the files/ folder of @fontsource/geist-sans).

What it does, all on the built output (upstream's TypeScript is untouched):
* copies nova-ui/ (Nova's CSS, the game picker, the stream page overlay, fonts, icon);
* replaces index.html (upstream's host list) with Nova's game picker;
* edits stream.html: Nova's stylesheets instead of upstream's page styles, Nova's overlay script
  after upstream's stream.js, the Nova title and icon;
* removes admin.html (user and role management, which the gateway refuses anyway).

Every edit must match exactly once, so a moonlight-web-stream update that changes stream.html
fails the build here instead of shipping a half-themed page.
"""

import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = {"geist-sans-400.woff2": "geist-sans-latin-400-normal.woff2",
         "geist-sans-500.woff2": "geist-sans-latin-500-normal.woff2",
         "geist-sans-600.woff2": "geist-sans-latin-600-normal.woff2"}

STREAM_EDITS = [
    ("<title>Stream</title>", "<title>Nova</title>\n    <meta name=\"color-scheme\" content=\"dark\">\n    <meta name=\"theme-color\" content=\"#000000\">"),
    ('<link rel="icon" href="resources/moonlight.svg" sizes="any" type="image/svg+xml">',
     '<link rel="icon" href="nova-ui/star.svg" sizes="any" type="image/svg+xml">'),
    ('<link rel="stylesheet" href="styles/standard.css" id="style">',
     '<link rel="stylesheet" href="nova-ui/nova.css">\n    <link rel="stylesheet" href="nova-ui/stream.css" id="style">'),
    ('<script type="module" src="styles/index.js"></script>', ""),
    ('<script type="module" src="stream.js" defer></script>',
     '<script src="nova-ui/early.js"></script>\n    <script type="module" src="stream.js" defer></script>\n'
     '    <script type="module" src="nova-ui/stream.js"></script>'),
]


def edit_once(text: str, old: str, new: str, name: str) -> str:
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"apply-theme: expected one {old!r} in {name}, found {n}. "
                         "moonlight-web-stream's page changed: update theme/apply-theme.py.")
    return text.replace(old, new)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    static, fonts = (os.path.abspath(a) for a in argv[1:])
    for need in ("stream.html", "stream.js", "index.html"):
        if not os.path.exists(os.path.join(static, need)):
            raise SystemExit(f"apply-theme: {need} is missing from {static}")

    ui = os.path.join(static, "nova-ui")
    shutil.rmtree(ui, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, "nova-ui"), ui)
    os.makedirs(os.path.join(ui, "fonts"))
    for dst, src in FONTS.items():
        shutil.copyfile(os.path.join(fonts, src), os.path.join(ui, "fonts", dst))
    licence = os.path.join(fonts, "..", "LICENSE")
    if os.path.exists(licence):
        shutil.copyfile(licence, os.path.join(ui, "fonts", "LICENSE.geist"))

    shutil.copyfile(os.path.join(HERE, "index.html"), os.path.join(static, "index.html"))

    path = os.path.join(static, "stream.html")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    for old, new in STREAM_EDITS:
        text = edit_once(text, old, new, "stream.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)

    for gone in ("admin.html",):
        try:
            os.remove(os.path.join(static, gone))
        except FileNotFoundError:
            pass
    print(f"apply-theme: Nova interface applied to {static}")


if __name__ == "__main__":
    main(sys.argv)
