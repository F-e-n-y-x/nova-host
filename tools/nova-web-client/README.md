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
| Nova's changes | `patches/0001-nova-display-launch-parameter.patch`: passes the Virtual display / Mirror choice to Nova's `/launch`. `theme/`: Nova's interface on the built pages (below) |
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

## Nova's interface (theme/)

The pages people see are Nova's, in the same dark look as Nova's web UI and Nebula (Nova's tokens,
Geist Sans, the purple accent, rounded cards, game art tiles). They are laid over
moonlight-web-stream's **built** frontend, so upstream's TypeScript is not forked:

| Page | What Nova does |
|---|---|
| `index.html` (game picker) | Replaced by `theme/index.html` + `nova-ui/picker.js`: Nova header with the signed-in user (Open Nova, Sign out, About), the host card (state, quality, the default display), Now playing (Resume, Quit), the library as poster tiles, and a details view per game with the Virtual display / Desktop (Mirror) choice. Keyboard arrows and a gamepad move between tiles. |
| `stream.html` | Kept. `apply-theme.py` swaps upstream's page stylesheet for `nova-ui/nova.css` + `stream.css`, adds `nova-ui/early.js` before upstream's `stream.js` and `nova-ui/stream.js` after it. The overlay drives upstream's `ViewerApp` (exposed by upstream as `window.app`) through its public methods: loading and error screen, an auto-hiding top bar, the side menu (full screen, keyboard, mouse capture and mode, touch mode, stats, special keys, quality with reconnect, Disconnect / Quit game) and shortcuts. Upstream's sidebar and connection dialog stay in the DOM but are hidden. |
| Sign-in, errors | Drawn by the gateway itself (`html_page` / `message_page`), same look. The fonts and logo they need are the only files the gateway serves without a session (`/nova/assets/…`, an exact allowlist). |
| `admin.html` | Removed (user and role management; the gateway refuses those calls anyway). |

Shortcuts on the stream page: Ctrl+Alt+Shift+M menu, +F full screen, +Z capture or release the
mouse, +S stats overlay, +Q disconnect. On touch screens the handle at the top opens the bar and
menu; upstream's three-finger tap still opens the keyboard.

The gateway adds three signed-in JSON/image routes for the picker, all answered by the gateway and
never proxied: `/nova/session` (user, Nova's URL, the credit and version), `/nova/library` (names,
which artwork exists and the default display, from Nova's `/api/apps`; commands, paths and
environment are dropped) and `/nova/art?index=&kind=poster|hero|logo` (Nova's artwork, PNG/JPEG/WebP
only, sandboxed).

**Upstream bumps.** `apply-theme.py` edits `stream.html` with exact, match-once replacements and
fails the build if one no longer matches. The overlay depends on these upstream names: `window.app`
and its `getStream()`, `getInputConfig()`/`setInputConfig()`, `requestFullscreen()`,
`exitFullscreen()`, `isFullscreen()`, `requestPointerLock()`, `sidebar.getScreenKeyboard()`; the
stream's `getStats()`, `getInput()` (`sendKey`, `raiseAllKeys`), `stop()`; the `stream-info` event
types; the element ids `#input`, `#modal-overlay`, `#sidebar-root` and the classes styled in
`stream.css`. Check those when bumping `MWS_TAG`, then run the E2E test.

To work on the theme without rebuilding Rust: `build.sh --theme-only BUILD_DIR`.

## Building

`build.sh [BUILD_DIR]` clones the pinned commit, applies the patches and builds the web frontend
(Node) and the two Rust programs, using a toolchain it installs inside `BUILD_DIR`, then applies
Nova's interface (`theme/`, with Geist Sans from the pinned `@fontsource/geist-sans` npm tarball,
SIL OFL 1.1, checked against `GEIST_SANS_SHA256`). It writes `BUILD_DIR/dist`. Pass that folder to the Nova build as `NOVA_WEB_CLIENT_DIST` and CMake installs
it to `/usr/lib/<arch>/nova-host/web-client`. Without it the package works as before, and the web
UI says the browser client isn't included.

The gateway tests: `cd gateway && python3 -m unittest test_nova_web_gateway`.
