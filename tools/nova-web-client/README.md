# Nova browser client

Play Nova games in a web browser, with no app to install. Nova ships this as an optional sidecar
and keeps it switched off until you turn it on (web UI → **Play in browser**, or
`web_client = enabled` in the config).

## Credit

The streaming part is [moonlight-web-stream](https://github.com/MrCreativ3001/moonlight-web-stream)
by MrCreativ3001 and contributors, licensed GPL-3.0-or-later. It turns the Moonlight protocol into
WebRTC for the browser. Nova uses it unmodified apart from the patches in `patches/`. It runs as a
separate program and is never linked into Nova.

| | |
|---|---|
| Upstream | https://github.com/MrCreativ3001/moonlight-web-stream |
| Version | `v2.10.0`, commit `cd9d03cbf9a42b394f7b72a733a2f39cb5f0edd8` (see `upstream.env`) |
| Licence | GPL-3.0-or-later, the same as Nova. Installed as `/usr/share/doc/nova-host/moonlight-web-stream/LICENSE.moonlight-web-stream` |
| Nova's changes | `patches/0001-nova-display-launch-parameter.patch`: passes the Virtual display / Mirror choice to Nova's `/launch` |
| Rust crate licences | `THIRD-PARTY.txt`, generated at build time |

**Source code:** the corresponding source of the shipped binaries is the upstream tag above plus
the patches in this folder. `build.sh` rebuilds them exactly.

## How it fits together

```
browser ──HTTPS :47995──▶ gateway (nova_web_gateway.py) ──HTTP 127.0.0.1:47996──▶ moonlight-web-stream
   ▲                         │  checks the Nova session                              │ pairs and streams
   └──WebRTC UDP 49000-49019─┼───────────────────────────────────────────────────────┘ like any Moonlight client
                             └──▶ Nova web UI (/api/auth/session, /api/pin)          ──▶ Nova :47989/:47984/…
```

Ports follow Nova's `port` setting: the gateway is `port + 6`, moonlight-web-stream `port + 7`
(loopback only), and WebRTC uses UDP `port + 1011` to `port + 1030`.

- **Who can connect:** the addresses `origin_web_ui_allowed` allows, but never more than LAN.
  `pc` means this computer only; `lan` and `wan` both mean LAN, link-local and Tailscale
  (100.64.0.0/10). No STUN or TURN servers are configured, so nothing is relayed off the network.
- **Sign-in:** the Nova web UI session. The browser's `nova_session` cookie (shared between the
  ports of one host) is checked with Nova. The gateway never handles a password. Signing out of
  Nova or revoking the session ends access, and an open stream closes within a minute.
- **Pairing:** the first time a signed-in user opens the browser client, the sidecar asks Nova to
  pair from 127.0.0.1 and the gateway approves the PIN with that user's session. The sidecar then
  appears in **Devices** as "Browser client", with its own permissions, like any other device.
  Remove it there to revoke it.
- **What the browser can reach:** only an allowlist of moonlight-web-stream's pages and calls.
  Adding hosts, pairing, and user or role management are refused.
- **HTTPS:** the gateway uses Nova's own certificate, so a browser that trusts Nova's web UI also
  trusts the browser client. Browsers need HTTPS for gamepads, keyboard lock and video decoding.

## Building

`build.sh [BUILD_DIR]` clones the pinned commit, applies the patches and builds the web frontend
(Node) and the two Rust programs, using a toolchain it installs inside `BUILD_DIR`. It writes
`BUILD_DIR/dist`. Pass that folder to the Nova build as `NOVA_WEB_CLIENT_DIST` and CMake installs
it to `/usr/lib/<arch>/nova-host/web-client`. Without it the package works as before, and the web
UI says the browser client isn't included.

The gateway tests: `cd gateway && python3 -m unittest test_nova_web_gateway`.
