#!/usr/bin/env python3
"""Nova browser client gateway.

The only network entry point of Nova's browser client sidecar (a pinned, patched build of
moonlight-web-stream, https://github.com/MrCreativ3001/moonlight-web-stream, GPL-3.0). Nova starts
it when "Browser client" is switched on and stops it when it is switched off. It:

* serves HTTPS with Nova's own certificate, and accepts connections only from the addresses
  Nova's ``origin_web_ui_allowed`` allows, capped at LAN (private ranges, link-local and
  Tailscale's 100.64.0.0/10): ``pc`` means this computer only, ``lan`` and ``wan`` both mean LAN;
* signs people in with Nova's own web UI session: the ``nova_session`` cookie the browser got
  from Nova's sign-in page (cookies are shared across the ports of one host) is checked against
  Nova's ``/api/auth/session``. The gateway never sees a password. Signing out of Nova, or
  revoking the session in Nova, also ends browser client access (open streams within a minute);
* reverse-proxies an allowlist of moonlight-web-stream's pages and API calls, including the
  WebRTC signalling WebSocket, to moonlight-web-stream bound on 127.0.0.1. The signed-in user
  goes in a forwarded-user header whose name is random per start and known only to the two
  processes; any copy of it in a request is dropped;
* pairs the sidecar with Nova the first time a signed-in user opens it (a local PIN exchange
  approved with that user's session), so it shows up in Nova -> Devices as "Browser client" with
  its own permissions;
* resolves ``/nova/play?app=<name>&display=virtual|mirror`` to the stream page.

``run`` (what Nova starts) also writes moonlight-web-stream's config and runs it as a child.
Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import asyncio
import ctypes
import html
import ipaddress
import json
import logging
import os
import re
import secrets
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass

log = logging.getLogger("nova-web-client")

# The same address classes as Nova's src/network.cpp.
PC_NETWORKS = ("127.0.0.0/8", "::1/128")
LAN_NETWORKS = PC_NETWORKS + (
    "192.168.0.0/16", "172.16.0.0/12", "10.0.0.0/8", "100.64.0.0/10", "169.254.0.0/16",
    "fc00::/7", "fe80::/64",
)

DEFAULTS = {
    "listen_host": "0.0.0.0",
    "listen_port": 47995,
    "tls_cert": "",
    "tls_key": "",
    # moonlight-web-stream, loopback only.
    "upstream_host": "127.0.0.1",
    "upstream_port": 47996,
    # Nova's web UI as the gateway reaches it (sessions, pairing) ...
    "nova_web_url": "https://127.0.0.1:47990",
    # ... and its port as browsers reach it (the sign-in link).
    "nova_web_port": 47990,
    # PEM that pins Nova's web UI certificate; empty = don't verify (loopback only).
    "nova_web_cert": "",
    # Where moonlight-web-stream streams from.
    "nova_host_address": "127.0.0.1",
    "nova_http_port": 47989,
    # Name shown in Nova -> Devices, and the name moonlight-web-stream pairs with.
    "device_name": "Browser client",
    "pair_device_name": "nova-web-client",
    # Header carrying the signed-in user to moonlight-web-stream (random in `run`).
    "forwarded_header": "X-Nova-User",
    # origin_web_ui_allowed: pc, lan or wan (wan is treated as lan).
    "allow": "lan",
    # How long a checked Nova session is trusted before asking Nova again.
    "auth_cache_seconds": 10,
    # How often open streams re-check their session.
    "revalidate_seconds": 60,
    # moonlight-web-stream's static/ (the Nova theme's public fonts and logo) and its VERSION line.
    "static_dir": "",
    "version": "",
}

NOVA_COOKIE = "nova_session"
MAX_HEAD = 64 * 1024
MAX_BODY = 1024 * 1024
HEAD_TIMEOUT = 30
HOP_BY_HOP = {
    "connection", "keep-alive", "proxy-connection", "te", "trailer", "transfer-encoding", "upgrade",
    "proxy-authorization", "proxy-authenticate",
}
DISPLAY_MODES = ("virtual", "mirror")
TOKEN_RE = re.compile(r"^[A-Za-z0-9_\-.~+/=]{16,256}$")
LABEL = r"[A-Za-z0-9](?:[A-Za-z0-9\-]{0,61}[A-Za-z0-9])?"
HOSTNAME_RE = re.compile(rf"^{LABEL}(?:\.{LABEL})*$")

# moonlight-web-stream calls a browser may make through the gateway. Everything else under /api/
# (adding or editing hosts, pairing, users, roles, password logins) is refused: the sidecar only
# ever talks to this Nova, and it pairs through the gateway.
API_ALLOWED = {
    ("GET", "/api/authenticate"),
    ("GET", "/api/user"),
    ("GET", "/api/role"),  # the signed-in user's own role (what the stream page may do)
    ("GET", "/api/hosts"),
    ("GET", "/api/host"),
    ("GET", "/api/apps"),
    ("GET", "/api/app/image"),
    ("GET", "/api/settings/default"),
    ("GET", "/api/settings/permissions"),
    ("GET", "/api/host/stream"),  # WebRTC signalling WebSocket
    ("POST", "/api/host/cancel"),
    ("POST", "/api/host/wake"),
}


# ----------------------------------------------------------------------------------------------
# Policy helpers (pure, unit tested)

def networks_for(allow: str):
    """Allowed source networks for an origin_web_ui_allowed value (wan is capped at lan)."""
    values = PC_NETWORKS if allow == "pc" else LAN_NETWORKS
    return [ipaddress.ip_network(v) for v in values]


def address_allowed(address: str, networks) -> bool:
    """Whether a peer address is inside one of the networks (IPv4-mapped v6 unwrapped)."""
    try:
        ip = ipaddress.ip_address(address.split("%", 1)[0])
    except ValueError:
        return False
    if isinstance(ip, ipaddress.IPv6Address) and ip.ipv4_mapped:
        ip = ip.ipv4_mapped
    return any(ip.version == net.version and ip in net for net in networks)


def is_loopback(address: str) -> bool:
    return address_allowed(address, networks_for("pc"))


def api_allowed(method: str, path: str) -> bool:
    """Whether a proxied /api/ call is on the allowlist (HEAD counts as GET)."""
    return ((method if method != "HEAD" else "GET"), path.rstrip("/") or "/") in API_ALLOWED


def safe_path(target: str | None) -> str | None:
    """A same-origin relative target, or None."""
    if not target or not target.startswith("/") or target.startswith("//") or "\\" in target:
        return None
    if len(target) > 2048 or any(ord(c) < 0x21 or ord(c) == 0x7F for c in target):
        return None
    return target


def host_of(host_header: str | None) -> str | None:
    """Hostname (IPv6 in brackets) from a Host header, or None when it isn't a plain host."""
    if not host_header:
        return None
    value = host_header.strip()
    if value.startswith("["):
        end = value.find("]")
        if end < 0 or "%" in value[:end]:
            return None
        try:
            ipaddress.IPv6Address(value[1:end])
        except ValueError:
            return None
        rest = value[end + 1:]
        if rest and not re.fullmatch(r":\d{1,5}", rest):
            return None
        return value[:end + 1]
    name, _, port = value.partition(":")
    if (port and not port.isdigit()) or not HOSTNAME_RE.match(name):
        return None
    return name


def nova_login_url(host_header: str | None, nova_web_port: int, target: str | None) -> str | None:
    """Nova's sign-in page on the host the browser used. It returns to Nova's browser client page,
    which sends the browser back to ``target`` here."""
    host = host_of(host_header)
    if host is None:
        return None
    nxt = "/browser"
    target = safe_path(target)
    if target and target != "/":
        nxt += "?" + urllib.parse.urlencode({"continue": target})
    return f"https://{host}:{int(nova_web_port)}/login?" + urllib.parse.urlencode({"next": nxt})


def play_target(host_id: int, app_id: int, display: str | None) -> str:
    """moonlight-web-stream's stream page for a host/app and an optional Nova display mode."""
    query = {"hostId": str(int(host_id)), "appId": str(int(app_id))}
    if display in DISPLAY_MODES:
        query["novaDisplay"] = display
    return "/stream.html?" + urllib.parse.urlencode(query)


def pick_app(apps, name: str | None, app_id: str | None):
    """Find an app by GameStream id or by exact (then case-insensitive) title."""
    if app_id and app_id.isdigit():
        for app in apps:
            if app.get("app_id") == int(app_id):
                return app
    if name:
        for app in apps:
            if app.get("title") == name:
                return app
        lowered = name.casefold()
        for app in apps:
            if str(app.get("title", "")).casefold() == lowered:
                return app
    return None


def pick_pairing(pending, device_name: str):
    """The pending Nova pairing that is the sidecar's: from loopback, under its device name."""
    ours = [p for p in pending
            if is_loopback(str(p.get("address", ""))) and p.get("name") in (device_name, "", None)]
    return ours[-1] if ours else None


def strip_cookie(raw: str, name: str) -> str:
    """A Cookie header without one cookie."""
    parts = [p.strip() for p in raw.split(";")]
    return "; ".join(p for p in parts if p and p.split("=", 1)[0].strip() != name)


@dataclass
class Identity:
    user: str
    csrf: str
    token: str


class SessionCache:
    """Nova sessions checked recently: token -> (checked at, identity or None)."""

    def __init__(self, ttl: float, clock=time.monotonic, limit: int = 256):
        self.ttl = ttl
        self.clock = clock
        self.limit = limit
        self.items: dict[str, tuple[float, Identity | None]] = {}

    def get(self, token: str):
        hit = self.items.get(token)
        if hit and self.clock() - hit[0] < self.ttl:
            return True, hit[1]
        return False, None

    def put(self, token: str, identity: Identity | None):
        if len(self.items) >= self.limit:
            now = self.clock()
            self.items = {k: v for k, v in self.items.items() if now - v[0] < self.ttl}
            if len(self.items) >= self.limit:
                self.items.clear()
        self.items[token] = (self.clock(), identity)


# ----------------------------------------------------------------------------------------------
# HTTP plumbing

class HttpError(Exception):
    pass


@dataclass
class Request:
    method: str
    target: str
    version: str
    headers: list[tuple[str, str]]
    peer: str

    def header(self, name: str) -> str | None:
        name = name.lower()
        for k, v in self.headers:
            if k.lower() == name:
                return v
        return None

    @property
    def path(self) -> str:
        return urllib.parse.urlsplit(self.target).path

    @property
    def query(self) -> dict[str, str]:
        return dict(urllib.parse.parse_qsl(urllib.parse.urlsplit(self.target).query))

    def cookie(self, name: str) -> str | None:
        found = None
        for raw in (v for k, v in self.headers if k.lower() == "cookie"):
            for part in raw.split(";"):
                k, _, v = part.strip().partition("=")
                if k == name:
                    found = v
        return found


async def read_head(reader: asyncio.StreamReader) -> bytes:
    try:
        return await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), HEAD_TIMEOUT)
    except asyncio.LimitOverrunError as e:
        raise HttpError("header too large") from e


def parse_request_head(raw: bytes, peer: str) -> Request:
    lines = raw.decode("latin-1").split("\r\n")
    parts = lines[0].split(" ")
    if len(parts) != 3 or not parts[2].startswith("HTTP/1."):
        raise HttpError("bad request line")
    headers = []
    for line in lines[1:]:
        if not line:
            continue
        if line[0] in " \t" or ":" not in line:
            raise HttpError("bad header")
        k, _, v = line.partition(":")
        if k != k.strip() or not k:
            raise HttpError("bad header name")
        headers.append((k, v.strip()))
    return Request(parts[0], parts[1], parts[2], headers, peer)


def response_bytes(status: int, reason: str, headers: list[tuple[str, str]], body: bytes = b"", *,
                   head: bool = False, cache: str = "no-store") -> bytes:
    """A complete response. ``head`` keeps the Content-Length of ``body`` but leaves it out (HEAD)."""
    base = [
        ("Content-Length", str(len(body))),
        ("Cache-Control", cache),
        ("X-Content-Type-Options", "nosniff"),
        ("Referrer-Policy", "same-origin"),
    ]
    top = f"HTTP/1.1 {status} {reason}\r\n" + "".join(f"{k}: {v}\r\n" for k, v in base + headers) + "\r\n"
    return top.encode("latin-1") + (b"" if head else body)


def text_response(status: int, reason: str, text: str) -> bytes:
    return response_bytes(status, reason, [("Content-Type", "text/plain; charset=utf-8")], text.encode())


def json_response(data, head: bool = False) -> bytes:
    return response_bytes(200, "OK", [("Content-Type", "application/json")], json.dumps(data).encode(), head=head)


PAGE_HEADERS = [
    ("Content-Type", "text/html; charset=utf-8"),
    ("Content-Security-Policy", "frame-ancestors 'none'; default-src 'none'; style-src 'unsafe-inline'; "
                                "font-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'"),
]

MWS_URL = "https://github.com/MrCreativ3001/moonlight-web-stream"

# Files under <static>/nova-ui/ that the gateway's own pages (sign-in, errors) use before anyone is
# signed in. Only these exact names are served without a session.
PUBLIC_ASSETS = {
    "fonts/geist-sans-400.woff2": "font/woff2",
    "fonts/geist-sans-500.woff2": "font/woff2",
    "fonts/geist-sans-600.woff2": "font/woff2",
    "star.svg": "image/svg+xml",
}

STAR_SVG = ('<svg class="star" width="28" height="28" viewBox="0 0 28 28" aria-hidden="true">'
            '<path d="M14 2.5c.9 5.6 5.9 10.6 11.5 11.5-5.6.9-10.6 5.9-11.5 11.5-.9-5.6-5.9-10.6-11.5-11.5'
            'C8.1 13.1 13.1 8.1 14 2.5z"/></svg>')

ICONS = {
    # 24px outline icons (lucide-style), drawn in currentColor.
    "lock": '<rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
    "alert": '<circle cx="12" cy="12" r="9"/><path d="M12 8v5M12 16h.01"/>',
    "search": '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
    "unplug": '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4M3 3l18 18"/>',
}


def icon(name: str) -> str:
    return (f'<svg class="glyph" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
            f'stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{ICONS[name]}</svg>')


def html_page(title: str, body: str, tone: str = "accent") -> bytes:
    """A page in the browser client's Nova look (dark, like Nebula): logo, one card, credit line.
    ``body`` is trusted markup; callers escape anything that came from a request."""
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark"><meta name="theme-color" content="#0A0A0B"><title>{html.escape(title)} · Nova</title>
<link rel="icon" href="/nova/assets/star.svg" type="image/svg+xml">
<style>
@font-face{{font-family:"Geist Sans";font-weight:400;font-display:swap;src:url(/nova/assets/fonts/geist-sans-400.woff2) format("woff2")}}
@font-face{{font-family:"Geist Sans";font-weight:500;font-display:swap;src:url(/nova/assets/fonts/geist-sans-500.woff2) format("woff2")}}
@font-face{{font-family:"Geist Sans";font-weight:600;font-display:swap;src:url(/nova/assets/fonts/geist-sans-600.woff2) format("woff2")}}
:root{{color-scheme:dark;--bg:#0A0A0B;--surface:#111113;--raised:#17171A;--border:#1E1E22;--border-strong:#2A2A30;
--text:#EDEDEF;--text-2:#A1A1A8;--muted:#8B8B93;--accent:#6B4EFF;--accent-hover:#5A3DF0;--accent-text:#B7A2FF;
--accent-tint:#1D1838;--danger:#F2766E;--danger-tint:#2A1413;--focus:#B7A2FF}}
*{{box-sizing:border-box}}
html{{background:var(--bg)}}
body{{margin:0;min-height:100vh;min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:center;
gap:28px;padding:max(24px,env(safe-area-inset-top)) 16px max(24px,env(safe-area-inset-bottom));color:var(--text);
font:15px/1.55 "Geist Sans",system-ui,-apple-system,"Segoe UI",sans-serif;-webkit-font-smoothing:antialiased;
background:radial-gradient(1200px 520px at 50% -140px,rgba(107,78,255,.16),transparent 70%),var(--bg)}}
.brand{{display:inline-flex;align-items:center;gap:10px;font-size:17px;font-weight:600;letter-spacing:-.01em}}
.star{{fill:var(--accent-text)}}
main{{width:100%;max-width:440px;background:var(--surface);border:1px solid var(--border);border-radius:16px;padding:32px;
box-shadow:0 24px 64px rgba(0,0,0,.45)}}
.badge{{display:grid;place-items:center;width:44px;height:44px;border-radius:12px;margin-bottom:20px;
background:var(--accent-tint);color:var(--accent-text)}}
.tone-danger .badge{{background:var(--danger-tint);color:var(--danger)}}
.note{{margin:16px 0 0;font-size:13px;color:var(--muted);text-align:center;font-variant-numeric:tabular-nums}}
h1{{margin:0 0 8px;font-size:22px;line-height:28px;font-weight:600;letter-spacing:-.015em;text-wrap:balance}}
p{{margin:0 0 20px;color:var(--text-2)}}
p strong{{color:var(--text);font-weight:500}}
.detail{{font:13px/1.5 ui-monospace,"SFMono-Regular",Menlo,monospace;color:var(--muted);background:var(--bg);
border:1px solid var(--border);border-radius:10px;padding:10px 12px;overflow-wrap:anywhere}}
.actions{{display:flex;flex-direction:column;gap:10px;margin-top:24px}}
.btn{{display:flex;align-items:center;justify-content:center;gap:8px;min-height:48px;padding:0 18px;border-radius:12px;
border:1px solid transparent;background:var(--accent);color:#fff;font:inherit;font-weight:500;text-decoration:none;
transition:background-color .15s ease-out,border-color .15s ease-out}}
.btn:hover{{background:var(--accent-hover)}}
.btn.secondary{{background:transparent;border-color:var(--border-strong);color:var(--text)}}
.btn.secondary:hover{{background:var(--raised)}}
a{{color:var(--accent-text);text-decoration:none}}a:hover{{text-decoration:underline}}
:focus-visible{{outline:2px solid var(--focus);outline-offset:2px}}
::selection{{background:var(--accent-tint);color:var(--text)}}
footer{{max-width:440px;text-align:center;font-size:13px;color:var(--muted)}}
@media (max-width:480px){{main{{padding:24px 20px;border-radius:14px}}h1{{font-size:20px;line-height:26px}}}}
@media (prefers-reduced-motion:reduce){{*{{transition:none!important}}}}
</style></head><body>
<span class="brand">{STAR_SVG}Nova</span>
<main class="tone-{tone}">{body}</main>
<footer>Streaming by <a href="{MWS_URL}">moonlight-web-stream</a> (GPL-3.0), run by Nova as a separate program.</footer>
</body></html>""".encode()


def message_page(title: str, heading: str, text: str, *, glyph: str = "alert", tone: str = "danger",
                 detail: str | None = None, actions: list[tuple[str, str, bool]] = ()) -> bytes:
    """An error or notice page: ``text``/``detail`` are plain text (escaped here); actions are
    (label, href, primary) with trusted hrefs."""
    parts = [f'<div class="badge">{icon(glyph)}</div>', f"<h1>{html.escape(heading)}</h1>", f"<p>{html.escape(text)}</p>"]
    if detail:
        parts.append(f'<div class="detail">{html.escape(detail)}</div>')
    if actions:
        parts.append('<div class="actions">' + "".join(
            f'<a class="btn{"" if primary else " secondary"}" href="{html.escape(href, quote=True)}">{html.escape(label)}</a>'
            for label, href, primary in actions) + "</div>")
    return html_page(title, "".join(parts), tone)


def describe_target(target: str | None) -> tuple[str | None, str | None]:
    """(game name, display label) when a sign-in was for a /nova/play link, for the sign-in page."""
    target = safe_path(target)
    if not target:
        return None, None
    parts = urllib.parse.urlsplit(target)
    if parts.path != "/nova/play":
        return None, None
    q = dict(urllib.parse.parse_qsl(parts.query))
    name = (q.get("app") or "").strip()[:80] or None
    label = {"virtual": "a virtual display", "mirror": "Mirror desktop"}.get(q.get("display") or "")
    return name, label


def signin_page(url: str, host: str | None, target: str | None) -> bytes:
    game, display = describe_target(target)
    if game:
        where = f" on {display}" if display else ""
        lead = (f"<p><strong>{html.escape(game)}</strong> starts{html.escape(where)} as soon as you are signed in. "
                "The browser client uses your Nova sign-in, so there is no separate password.</p>")
    else:
        lead = ("<p>The browser client uses your Nova sign-in, so there is no separate password. "
                "You come straight back here afterwards.</p>")
    where = f'<p class="note">Nova on {html.escape(host)}</p>' if host else ""
    body = (f'<div class="badge">{icon("lock")}</div><h1>Sign in to play</h1>{lead}'
            f'<div class="actions"><a class="btn" href="{html.escape(url, quote=True)}">Sign in with Nova</a></div>{where}')
    return html_page("Sign in", body)


def wants_html(req) -> bool:
    return req.method in ("GET", "HEAD") and not req.path.startswith("/api/") and "text/html" in (req.header("accept") or "")


def library_view(tree) -> list[dict]:
    """The part of Nova's apps.json the game picker may see: names, which artwork exists and the
    default display. Commands, environment, paths and everything else stay in Nova."""
    apps = tree.get("apps") if isinstance(tree, dict) else None
    if not isinstance(apps, list):
        return []
    running = tree.get("running_index")
    out = []
    for i, app in enumerate(apps):
        if not isinstance(app, dict) or not isinstance(app.get("name"), str):
            continue
        display = app.get("nova-display-mode")
        out.append({
            "index": i,
            "name": app["name"][:200],
            "poster": bool(app.get("image-path")),
            "hero": bool(app.get("nova-hero")),
            "logo": bool(app.get("nova-logo")),
            "display": display if display in DISPLAY_MODES else None,
            "running": isinstance(running, int) and running == i,
        })
    return out


ART_KINDS = ("poster", "hero", "logo")
ART_TYPES = ("image/png", "image/jpeg", "image/webp", "image/gif", "image/avif")
MAX_ART = 12 * 1024 * 1024


# ----------------------------------------------------------------------------------------------
# Nova and moonlight-web-stream calls (blocking; run in a thread)

class Backends:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        if cfg.get("nova_web_cert"):
            ctx.load_verify_locations(cfg["nova_web_cert"])
            ctx.verify_mode = ssl.CERT_REQUIRED
        else:
            ctx.verify_mode = ssl.CERT_NONE
        self.nova_ctx = ctx
        self.upstream = f"http://{cfg['upstream_host']}:{cfg['upstream_port']}"

    # -- Nova web UI, as the signed-in browser (its session cookie)
    def _nova(self, method: str, path: str, token: str, csrf: str = "", body=None, timeout=10):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.cfg["nova_web_url"] + path, data=data, method=method)
        req.add_header("Cookie", f"{NOVA_COOKIE}={token}")
        if csrf:
            req.add_header("X-CSRF-Token", csrf)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        with urllib.request.urlopen(req, context=self.nova_ctx, timeout=timeout) as resp:
            return json.loads(resp.read() or b"null")

    def nova_session(self, token: str) -> Identity | None:
        """Who a Nova session cookie belongs to, or None when it isn't signed in."""
        try:
            body = self._nova("GET", "/api/auth/session", token)
        except urllib.error.HTTPError as e:
            if e.code in (401, 403):
                return None
            raise
        if isinstance(body, dict) and body.get("authenticated") and body.get("username"):
            return Identity(str(body["username"]), str(body.get("csrf_token", "")), token)
        return None

    def nova_library(self, token: str):
        """Nova's library as the signed-in user sees it in the web UI, reduced by library_view."""
        return library_view(self._nova("GET", "/api/apps", token) or {})

    def nova_art(self, token: str, index: int, kind: str):
        """(content type, bytes) of one piece of artwork, or None when there is none."""
        path = f"/api/covers/{int(index)}" + ("" if kind == "poster" else f"/{kind}")
        req = urllib.request.Request(self.cfg["nova_web_url"] + path, method="GET")
        req.add_header("Cookie", f"{NOVA_COOKIE}={token}")
        try:
            with urllib.request.urlopen(req, context=self.nova_ctx, timeout=15) as resp:
                ctype = (resp.headers.get("Content-Type") or "").split(";")[0].strip().lower()
                data = resp.read(MAX_ART + 1)
        except urllib.error.HTTPError as e:
            if e.code in (400, 404):
                return None
            raise
        if ctype not in ART_TYPES or len(data) > MAX_ART:
            return None
        return ctype, data

    # -- moonlight-web-stream (loopback, as the forwarded user)
    def _mw(self, method: str, path: str, user: str, body=None, timeout=15):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.upstream + path, data=data, method=method)
        req.add_header(self.cfg["forwarded_header"], user)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        return urllib.request.urlopen(req, timeout=timeout)

    def mw_json(self, method: str, path: str, user: str, body=None):
        with self._mw(method, path, user, body) as resp:
            # Streamed responses are newline-delimited; the first line is the complete state.
            return json.loads(resp.readline() or b"null")

    def mw_hosts(self, user: str):
        return (self.mw_json("GET", "/api/hosts", user) or {}).get("hosts", [])

    def paired_host(self, user: str):
        for host in self.mw_hosts(user):
            if host.get("paired") == "Paired":
                return host
        return None

    def pair(self, who: Identity) -> str:
        """Pair the sidecar with Nova for ``who``, approving the PIN with their Nova session."""
        user = who.user
        if self.paired_host(user):
            return "already paired"
        hosts = self.mw_hosts(user)
        if hosts:
            host_id = hosts[0]["host_id"]
        else:
            added = self.mw_json("POST", "/api/host", user, {
                "address": self.cfg["nova_host_address"], "http_port": self.cfg["nova_http_port"]})
            host_id = added["host"]["host_id"]

        resp = self._mw("POST", "/api/pair", user, {"host_id": host_id}, timeout=90)
        try:
            first = json.loads(resp.readline() or b"null")
            if not isinstance(first, dict) or "Pin" not in first:
                raise RuntimeError(f"moonlight-web-stream did not return a PIN: {first!r}")
            pin = str(first["Pin"])
            pairing = None
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                pending = (self._nova("GET", "/api/pin", who.token) or {}).get("pairings", [])
                pairing = pick_pairing(pending, self.cfg["pair_device_name"])
                if pairing:
                    break
                time.sleep(0.3)
            if not pairing:
                raise RuntimeError("Nova did not report the pairing request")
            result = self._nova("POST", "/api/pin", who.token, who.csrf,
                                {"pairing_id": pairing["id"], "pin": pin, "name": self.cfg["device_name"]})
            if not result or not result.get("status"):
                raise RuntimeError(f"Nova refused the PIN: {result!r}")
            second = json.loads(resp.readline() or b"null")
            if isinstance(second, dict) and "Paired" in second:
                return "paired"
            raise RuntimeError(f"pairing failed: {second!r}")
        finally:
            resp.close()


# ----------------------------------------------------------------------------------------------
# Gateway

class Gateway:
    def __init__(self, cfg: dict, backends: Backends | None = None):
        self.cfg = cfg
        self.networks = networks_for(cfg.get("allow", "lan"))
        self.backends = backends or Backends(cfg)
        self.sessions = SessionCache(float(cfg["auth_cache_seconds"]))
        self.pair_lock = asyncio.Lock()
        self.paired_until: dict[str, float] = {}

    # -- connection loop
    async def handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        peername = writer.get_extra_info("peername") or ("", 0)
        peer = str(peername[0])
        if not address_allowed(peer, self.networks):
            log.warning("refused connection from %s (outside origin_web_ui_allowed, capped at LAN)", peer)
            writer.close()
            return
        try:
            while True:
                try:
                    raw = await read_head(reader)
                except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError):
                    return
                try:
                    req = parse_request_head(raw, peer)
                except HttpError:
                    writer.write(response_bytes(400, "Bad Request", [("Connection", "close")]))
                    await writer.drain()
                    return
                if not await self.dispatch(req, reader, writer):
                    return
        except (ConnectionError, asyncio.IncompleteReadError, ssl.SSLError):
            return
        except HttpError as e:
            log.info("%s: %s", peer, e)
        finally:
            try:
                writer.close()
            except Exception:
                pass

    async def read_body(self, req: Request, reader, limit: int) -> bytes:
        if req.header("transfer-encoding"):
            raise HttpError("chunked request bodies are not supported")
        try:
            length = int(req.header("content-length") or 0)
        except ValueError as e:
            raise HttpError("bad content-length") from e
        if length < 0 or length > limit:
            raise HttpError("request body too large")
        return await reader.readexactly(length) if length else b""

    async def send(self, writer, data: bytes):
        writer.write(data)
        await writer.drain()

    def same_origin(self, req: Request) -> bool:
        """Requests that change state (and WebSockets) must come from this origin."""
        origin = req.header("origin")
        if origin is None:
            return True
        return urllib.parse.urlsplit(origin).netloc == (req.header("host") or "")

    async def identity(self, req: Request) -> Identity | None:
        token = req.cookie(NOVA_COOKIE)
        if not token or not TOKEN_RE.match(token):
            return None
        known, who = self.sessions.get(token)
        if known:
            return who
        try:
            who = await asyncio.to_thread(self.backends.nova_session, token)
        except Exception as e:  # Nova down, TLS error...
            log.error("could not reach Nova to check a session: %s", e)
            return None
        self.sessions.put(token, who)
        return who

    async def dispatch(self, req: Request, reader, writer) -> bool:
        path = req.path
        if path == "/nova/health":
            await self.send(writer, response_bytes(200, "OK", [("Content-Type", "application/json"),
                                                               ("Access-Control-Allow-Origin", "*")], b'{"ok":true}'))
            return True

        if path.startswith("/nova/assets/"):
            return await self.public_asset(req, writer)

        who = await self.identity(req)
        if who is None:
            return await self.signed_out(req, writer)

        if not self.same_origin(req) and (req.method not in ("GET", "HEAD") or req.header("upgrade")):
            await self.send(writer, text_response(403, "Forbidden", "cross-origin request refused"))
            return True

        if path == "/nova/play":
            return await self.play(req, who, writer)
        if path == "/nova/session" and req.method in ("GET", "HEAD"):
            return await self.session_info(req, who, writer)
        if path == "/nova/library" and req.method in ("GET", "HEAD"):
            return await self.library(req, who, writer)
        if path == "/nova/art" and req.method in ("GET", "HEAD"):
            return await self.art(req, who, writer)
        if path.startswith("/nova/"):
            await self.send(writer, text_response(404, "Not Found", "not found"))
            return True
        if path.startswith("/api/"):
            if not api_allowed(req.method, path):
                log.warning("refused %s %s from %s (not on the allowlist)", req.method, path, req.peer)
                await self.send(writer, text_response(403, "Forbidden", "not available in Nova's browser client"))
                return True
        elif req.method not in ("GET", "HEAD"):
            await self.send(writer, response_bytes(405, "Method Not Allowed", [("Allow", "GET, HEAD")]))
            return True
        elif path in ("/", "/index.html"):
            # First visit: pair before moonlight-web-stream's host list shows an unpaired host.
            if not await self.ensure_paired(who, writer):
                return True
        return await self.proxy(req, reader, writer, who)

    async def signed_out(self, req: Request, writer) -> bool:
        if req.header("upgrade") or req.path.startswith("/api/") or req.method not in ("GET", "HEAD"):
            await self.send(writer, text_response(401, "Unauthorized", "sign in to Nova first"))
            return True
        url = nova_login_url(req.header("host"), self.cfg["nova_web_port"], req.target)
        if url is None:
            await self.send(writer, text_response(400, "Bad Request", "bad Host header"))
            return True
        page = signin_page(url, host_of(req.header("host")), req.target)
        await self.send(writer, response_bytes(200, "OK", PAGE_HEADERS, page))
        return True

    async def public_asset(self, req: Request, writer) -> bool:
        """Fonts and the logo for the gateway's own pages; the only thing served without a session."""
        name = req.path[len("/nova/assets/"):]
        ctype = PUBLIC_ASSETS.get(name)
        static = self.cfg.get("static_dir") or ""
        data = None
        if ctype and static and req.method in ("GET", "HEAD"):
            try:
                with open(os.path.join(static, "nova-ui", name), "rb") as f:
                    data = f.read(1024 * 1024)
            except OSError:
                data = None
        if data is None:
            await self.send(writer, text_response(404, "Not Found", "not found"))
            return True
        await self.send(writer, response_bytes(
            200, "OK", [("Content-Type", ctype), ("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'")],
            data, head=req.method == "HEAD", cache="public, max-age=86400"))
        return True

    async def session_info(self, req: Request, who: Identity, writer) -> bool:
        """Who is signed in and where Nova's web UI is, for the game picker's header."""
        host = host_of(req.header("host"))
        nova = f"https://{host}:{int(self.cfg['nova_web_port'])}" if host else None
        info = {
            "user": who.user,
            "nova_url": nova + "/" if nova else None,
            "signout_url": nova + "/logout" if nova else None,
            "device_name": self.cfg["device_name"],
            "upstream": {"name": "moonlight-web-stream", "version": self.cfg.get("version") or "", "source": MWS_URL,
                         "licence": "GPL-3.0-or-later"},
        }
        await self.send(writer, json_response(info, head=req.method == "HEAD"))
        return True

    async def library(self, req: Request, who: Identity, writer) -> bool:
        try:
            apps = await asyncio.to_thread(self.backends.nova_library, who.token)
        except Exception as e:
            log.info("library: %s", e)
            apps = None
        await self.send(writer, json_response({"apps": apps, "ok": apps is not None}, head=req.method == "HEAD"))
        return True

    async def art(self, req: Request, who: Identity, writer) -> bool:
        q = req.query
        index, kind = q.get("index", ""), q.get("kind", "poster")
        if not index.isdigit() or len(index) > 6 or kind not in ART_KINDS:
            await self.send(writer, text_response(400, "Bad Request", "index and kind (poster, hero or logo) required"))
            return True
        try:
            found = await asyncio.to_thread(self.backends.nova_art, who.token, int(index), kind)
        except Exception as e:
            log.info("art: %s", e)
            found = None
        if not found:
            await self.send(writer, text_response(404, "Not Found", "no artwork"))
            return True
        ctype, data = found
        await self.send(writer, response_bytes(
            200, "OK", [("Content-Type", ctype), ("Content-Security-Policy", "default-src 'none'; sandbox")],
            data, head=req.method == "HEAD", cache="private, max-age=300"))
        return True

    async def ensure_paired(self, who: Identity, writer) -> bool:
        """Pair on first use. Sends an error page and returns False when that fails."""
        if self.paired_until.get(who.user, 0) > time.monotonic():
            return True
        async with self.pair_lock:
            try:
                if not await asyncio.to_thread(self.backends.paired_host, who.user):
                    status = await asyncio.to_thread(self.backends.pair, who)
                    log.info("pairing with Nova for %r: %s", who.user, status)
                self.paired_until[who.user] = time.monotonic() + 30
                return True
            except Exception as e:
                log.error("pairing with Nova failed: %s", e)
                page = message_page("Pairing failed", "Couldn't pair with Nova",
                                    "The browser client pairs with Nova the first time you open it, and that didn't work. "
                                    "Check that Nova is running, then try again.", glyph="unplug", detail=str(e),
                                    actions=[("Try again", "/", True)])
                await self.send(writer, response_bytes(502, "Bad Gateway", PAGE_HEADERS, page))
                return False

    # -- play in browser
    async def play(self, req: Request, who: Identity, writer) -> bool:
        q = req.query
        display = q.get("display") or None
        if display not in (None, *DISPLAY_MODES):
            await self.send(writer, text_response(400, "Bad Request", "display must be virtual or mirror"))
            return True
        if not await self.ensure_paired(who, writer):
            return True

        def resolve():
            host = self.backends.paired_host(who.user)
            if not host:
                return None, None
            apps = (self.backends.mw_json("GET", f"/api/apps?host_id={int(host['host_id'])}", who.user) or {}).get("apps", [])
            return host, pick_app(apps, q.get("app"), q.get("id"))

        try:
            host, app = await asyncio.to_thread(resolve)
        except Exception as e:
            log.error("play: %s", e)
            await self.send(writer, self.unavailable(req))
            return True
        if host is None:
            self.paired_until.pop(who.user, None)
            page = message_page("Not paired", "Not paired with Nova",
                                "The browser client was removed from Nova's devices. Open it again to pair it again.",
                                glyph="unplug", actions=[("Pair again", "/", True)])
            await self.send(writer, response_bytes(409, "Conflict", PAGE_HEADERS, page))
            return True
        if app is None:
            page = message_page("Game not found", "Game not found",
                                f"Nova has no game called \u201c{(q.get('app') or q.get('id') or '?')[:80]}\u201d. "
                                "It may have been renamed or removed from the library.",
                                glyph="search", tone="accent", actions=[("Browse games", "/", True)])
            await self.send(writer, response_bytes(404, "Not Found", PAGE_HEADERS, page))
            return True
        await self.send(writer, response_bytes(302, "Found", [("Location", play_target(host["host_id"], app["app_id"], display))]))
        return True

    def unavailable(self, req: Request) -> bytes:
        if wants_html(req):
            page = message_page("Unavailable", "The browser client isn't responding",
                                "Its streaming service stopped or is still starting. Nova restarts it on its own; "
                                "try again in a few seconds.", glyph="unplug",
                                actions=[("Try again", safe_path(req.target) or "/", True)])
            return response_bytes(502, "Bad Gateway", PAGE_HEADERS, page)
        return text_response(502, "Bad Gateway", "moonlight-web-stream is not running")

    # -- reverse proxy
    def upstream_lines(self, req: Request, user: str, upgrade: bool) -> list[str]:
        secret = self.cfg["forwarded_header"].lower()
        lines = [f"{req.method} {req.target} HTTP/1.1"]
        for k, v in req.headers:
            lk = k.lower()
            if lk in HOP_BY_HOP or lk in ("host", "authorization", secret, "x-forwarded-user",
                                          "x-forwarded-for", "x-forwarded-proto", "x-forwarded-host",
                                          "x-real-ip", "forwarded", "content-length"):
                continue
            if lk == "cookie":
                v = strip_cookie(v, NOVA_COOKIE)  # Nova's session never reaches moonlight-web-stream
                if not v:
                    continue
            lines.append(f"{k}: {v}")
        lines.append(f"Host: {self.cfg['upstream_host']}:{self.cfg['upstream_port']}")
        lines.append(f"{self.cfg['forwarded_header']}: {user}")
        lines.append(f"X-Forwarded-For: {req.peer}")
        lines.append("X-Forwarded-Proto: https")
        if upgrade:
            lines.append("Connection: Upgrade")
            lines.append(f"Upgrade: {req.header('upgrade')}")
        else:
            lines.append("Connection: close")
        return lines

    async def proxy(self, req: Request, reader, writer, who: Identity) -> bool:
        upgrade = (req.header("upgrade") or "").lower() == "websocket"
        body = b"" if upgrade else await self.read_body(req, reader, MAX_BODY)
        try:
            up_r, up_w = await asyncio.open_connection(self.cfg["upstream_host"], self.cfg["upstream_port"])
        except OSError:
            await self.send(writer, self.unavailable(req))
            return True
        try:
            lines = self.upstream_lines(req, who.user, upgrade)
            if body or req.method in ("POST", "PUT", "PATCH"):
                lines.append(f"Content-Length: {len(body)}")
            up_w.write(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + body)
            await up_w.drain()
            if upgrade:
                await self.tunnel(reader, writer, up_r, up_w, who)
                return False
            return await self.relay_response(req, writer, up_r)
        finally:
            up_w.close()

    async def relay_response(self, req: Request, writer, up_r) -> bool:
        head = await read_head(up_r)
        lines = head.decode("latin-1").split("\r\n")
        status_line = lines[0]
        headers = []
        framed = False
        for line in lines[1:]:
            if not line:
                continue
            k, _, v = line.partition(":")
            lk = k.lower()
            if lk == "content-length" or (lk == "transfer-encoding" and "chunked" in v.lower()):
                framed = True
            if lk in ("connection", "keep-alive"):
                continue
            headers.append(f"{k}:{v}")
        code = int(status_line.split(" ")[1])
        no_body = req.method == "HEAD" or code in (204, 304) or 100 <= code < 200
        keep = framed or no_body
        headers.append("Connection: keep-alive" if keep else "Connection: close")
        writer.write((status_line + "\r\n" + "\r\n".join(headers) + "\r\n\r\n").encode("latin-1"))
        if not no_body:
            # The upstream request used "Connection: close", so the body ends at EOF; forwarding
            # the bytes verbatim keeps the original Content-Length or chunked framing.
            while chunk := await up_r.read(65536):
                writer.write(chunk)
                await writer.drain()
        await writer.drain()
        return keep

    async def still_signed_in(self, who: Identity) -> bool:
        known, still = self.sessions.get(who.token)
        if not known:
            try:
                still = await asyncio.to_thread(self.backends.nova_session, who.token)
            except Exception:
                return True  # Nova briefly unreachable: keep the stream
            self.sessions.put(who.token, still)
        return still is not None

    async def tunnel(self, reader, writer, up_r, up_w, who: Identity):
        async def pump(src, dst):
            try:
                while data := await src.read(65536):
                    dst.write(data)
                    await dst.drain()
            except (ConnectionError, ssl.SSLError):
                pass
            finally:
                try:
                    dst.close()
                except Exception:
                    pass

        async def watch():
            # A stream ends soon after its Nova session does (sign-out, revoke, password change).
            while True:
                await asyncio.sleep(float(self.cfg["revalidate_seconds"]))
                if not await self.still_signed_in(who):
                    log.info("closing a stream of %r: the Nova session ended", who.user)
                    for w in (writer, up_w):
                        try:
                            w.close()
                        except Exception:
                            pass
                    return

        watcher = asyncio.create_task(watch())
        try:
            await asyncio.gather(pump(reader, up_w), pump(up_r, writer))
        finally:
            watcher.cancel()


# ----------------------------------------------------------------------------------------------
# moonlight-web-stream config and process (the `run` command)

def write_private(path: str, data) -> None:
    fd = os.open(path + ".tmp", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    os.replace(path + ".tmp", path)


def default_mw_config(lib_dir: str) -> dict:
    """Ask the pinned web-server for its full default config, so every field is present."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "config.json")
        subprocess.run([os.path.join(lib_dir, "web-server"), "--config-path", path,
                        "--disable-default-webrtc-ice-servers", "print-config"],
                       cwd=lib_dir, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        with open(path, encoding="utf-8") as f:
            return json.load(f)


def mw_config(defaults: dict, *, state_dir: str, lib_dir: str, upstream_port: int, udp_min: int, udp_max: int,
              nova_http_port: int, forwarded_header: str, pair_device_name: str) -> dict:
    """moonlight-web-stream's config for the sidecar: loopback only, no password logins (the
    gateway signs people in), no STUN/TURN (LAN and Tailscale only), a fixed UDP range."""
    mw = json.loads(json.dumps(defaults))
    mw["data_storage"]["path"] = os.path.join(state_dir, "data.json")
    mw["streamer_path"] = os.path.join(lib_dir, "streamer")
    ws = mw["web_server"]
    ws["bind_address"] = f"127.0.0.1:{upstream_port}"
    ws["certificate"] = None
    ws["first_login_create_admin"] = False
    ws["first_login_assign_global_hosts"] = False
    ws["default_user_id"] = None
    ws["session_cookie_secure"] = True
    ws["forwarded_header"] = {"username_header": forwarded_header, "auto_create_missing_user": True}
    rtc = mw["webrtc"]
    rtc["ice_servers"] = []
    rtc["ice_server_script"] = None
    rtc["port_range"] = {"min": udp_min, "max": udp_max}
    # IPv4 only: LAN and Tailscale both have it, and every IPv6 address of every interface would
    # need its own port from the small range (hosts with many interfaces ran out).
    rtc["network_types"] = ["udp4"]
    rtc["include_loopback_candidates"] = False
    mw["moonlight"]["default_http_port"] = nova_http_port
    mw["moonlight"]["pair_device_name"] = pair_device_name
    mw["log"]["file_path"] = None
    return mw


def set_parent_death_signal():
    """Get SIGTERM when the parent (Nova, or this gateway) exits."""
    try:
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(1, signal.SIGTERM)  # PR_SET_PDEATHSIG
    except Exception:
        pass


def wait_for_port(host: str, port: int, proc: subprocess.Popen, timeout: float = 30) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return False
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def make_ssl(cfg: dict) -> ssl.SSLContext | None:
    if not cfg.get("tls_cert"):
        return None
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(cfg["tls_cert"], cfg["tls_key"])
    ctx.set_alpn_protocols(["http/1.1"])
    return ctx


async def serve(cfg: dict, allow_plain_http: bool = False, child: subprocess.Popen | None = None) -> int:
    ctx = make_ssl(cfg)
    if ctx is None and not allow_plain_http:
        raise SystemExit("tls_cert/tls_key are required (browsers need HTTPS for gamepads and video decode)")
    gateway = Gateway(cfg)
    server = await asyncio.start_server(gateway.handle, cfg["listen_host"], int(cfg["listen_port"]),
                                        ssl=ctx, limit=MAX_HEAD)
    addrs = ", ".join(str(s.getsockname()[:2]) for s in server.sockets)
    log.info("listening on %s (%s), allowing %s", addrs, "https" if ctx else "http",
             "this computer only" if cfg.get("allow") == "pc" else "LAN and Tailscale")
    loop = asyncio.get_running_loop()
    stop = loop.create_future()
    for sig in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(sig, lambda: stop.done() or stop.set_result("signal"))

    async def watch_child():
        while child is not None and child.poll() is None:
            await asyncio.sleep(0.5)
        if child is not None and not stop.done():
            stop.set_result(f"moonlight-web-stream exited with {child.returncode}")

    watcher = asyncio.create_task(watch_child())
    async with server:
        reason = await stop
    watcher.cancel()
    log.info("stopping: %s", reason)
    return 0 if reason == "signal" else 1


def stop_child(child: subprocess.Popen | None):
    if child is None or child.poll() is not None:
        return
    child.terminate()
    try:
        child.wait(5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()


def read_version(lib_dir: str) -> str:
    try:
        with open(os.path.join(lib_dir, "VERSION"), encoding="utf-8") as f:
            return f.readline().strip()
    except OSError:
        return "unknown version"


def run(args) -> int:
    set_parent_death_signal()
    parent = os.getppid()
    lib_dir = os.path.abspath(args.lib_dir)
    state_dir = os.path.abspath(args.state_dir)
    os.makedirs(state_dir, mode=0o700, exist_ok=True)
    os.chmod(state_dir, 0o700)
    for name in ("web-server", "streamer"):
        if not os.access(os.path.join(lib_dir, name), os.X_OK):
            log.error("%s is missing from %s", name, lib_dir)
            return 2

    header = f"X-Nova-User-{secrets.token_hex(16)}"
    mw = mw_config(default_mw_config(lib_dir), state_dir=state_dir, lib_dir=lib_dir,
                   upstream_port=args.upstream_port, udp_min=args.udp_min, udp_max=args.udp_max,
                   nova_http_port=args.nova_http_port, forwarded_header=header,
                   pair_device_name=DEFAULTS["pair_device_name"])
    mw_path = os.path.join(state_dir, "config.json")
    write_private(mw_path, mw)

    cfg = dict(DEFAULTS)
    cfg.update({
        "listen_host": args.listen_host,
        "listen_port": args.listen_port,
        "tls_cert": args.tls_cert,
        "tls_key": args.tls_key,
        "upstream_port": args.upstream_port,
        "nova_web_url": f"https://{args.nova_address_url}:{args.nova_web_port}",
        "nova_web_port": args.nova_web_port,
        "nova_web_cert": args.nova_web_cert or "",
        "nova_host_address": args.nova_address,
        "nova_http_port": args.nova_http_port,
        "forwarded_header": header,
        "allow": args.allow,
        "static_dir": os.path.join(lib_dir, "static"),
        "version": read_version(lib_dir),
    })

    log.info("starting moonlight-web-stream %s", read_version(lib_dir))
    child = subprocess.Popen([os.path.join(lib_dir, "web-server"), "--config-path", mw_path, "run"],
                             cwd=lib_dir, stdin=subprocess.DEVNULL, preexec_fn=set_parent_death_signal)
    try:
        if os.getppid() != parent:
            return 1  # Nova went away while we were starting
        if not wait_for_port("127.0.0.1", args.upstream_port, child):
            log.error("moonlight-web-stream did not start (exit code %s)", child.poll())
            return 1
        return asyncio.run(serve(cfg, child=child))
    finally:
        stop_child(child)


def load_config(path: str | None) -> dict:
    cfg = dict(DEFAULTS)
    if path:
        with open(path, encoding="utf-8") as f:
            cfg.update(json.load(f))
    return cfg


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-v", "--verbose", action="store_true")
    sub = ap.add_subparsers(dest="command", required=True)

    r = sub.add_parser("run", help="write moonlight-web-stream's config, start it and serve (what Nova runs)")
    r.add_argument("--lib-dir", required=True, help="web-server, streamer and static/")
    r.add_argument("--state-dir", required=True, help="config and pairing (0700)")
    r.add_argument("--listen-host", default="0.0.0.0")
    r.add_argument("--listen-port", type=int, required=True)
    r.add_argument("--upstream-port", type=int, required=True)
    r.add_argument("--udp-min", type=int, required=True)
    r.add_argument("--udp-max", type=int, required=True)
    r.add_argument("--nova-address", default="127.0.0.1", help="address moonlight-web-stream streams from")
    r.add_argument("--nova-address-url", default="127.0.0.1", help="the same, as it goes in a URL")
    r.add_argument("--nova-web-port", type=int, required=True)
    r.add_argument("--nova-http-port", type=int, required=True)
    r.add_argument("--nova-web-cert", default="")
    r.add_argument("--tls-cert", required=True)
    r.add_argument("--tls-key", required=True)
    r.add_argument("--allow", choices=("pc", "lan", "wan"), default="lan")

    s = sub.add_parser("serve", help="serve with a JSON config (tests)")
    s.add_argument("-c", "--config")
    s.add_argument("--insecure-http", action="store_true", help="plain HTTP (tests only)")

    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, stream=sys.stderr,
                        format="nova-web-client: %(levelname)s %(message)s")
    if args.command == "run":
        return run(args)
    return asyncio.run(serve(load_config(args.config), args.insecure_http))


if __name__ == "__main__":
    sys.exit(main())
