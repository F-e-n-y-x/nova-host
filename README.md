<div align="center">
  <img src="sunshine.svg" alt="Nova icon" width="200"/>
  <h1 align="center">Nova</h1>
  <h4 align="center">Linux-first, self-hosted game stream host for Moonlight and Nebula.</h4>
</div>

<div align="center">
  <a href="https://github.com/F-e-n-y-x/nova-host/actions/workflows/nova-ci.yml"><img src="https://github.com/F-e-n-y-x/nova-host/actions/workflows/nova-ci.yml/badge.svg" alt="Nova CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-8b30d9?style=flat-square" alt="License"></a>
  <img src="https://img.shields.io/badge/platform-linux%20first-a855f7?style=flat-square" alt="Linux first">
  <a href="https://github.com/jacksonpate/zenith"><img src="https://img.shields.io/badge/forked%20from-jacksonpate%2Fzenith-6d1fb8?style=flat-square" alt="Forked from Zenith"></a>
  <a href="https://github.com/LizardByte/Sunshine"><img src="https://img.shields.io/badge/based%20on-LizardByte%2FSunshine-6d1fb8?style=flat-square" alt="Based on Sunshine"></a>
</div>

<br/>

<div align="center">
  <img src="docs/images/nova-fetch.svg" alt="Nova at a glance" width="760"/>
</div>

Nova streams your Linux PC's desktop and games to phones, tablets and TVs. It works with any
Moonlight client, and pairs best with [Nebula](https://github.com/F-e-n-y-x/nebula), its companion
Android app.

## Features

### Available now

**Displays**
- 🪞 **Mirror** — your desktop resizes to the device's exact screen size and refresh, then
  restores when you disconnect.
- 🖥️ **Virtual display** — a separate display created just for the stream, at the device's size.
  Your own desktop, windows and cursor stay untouched; the stream gets its own mouse, keyboard and
  audio.

**Streaming**
- 🎮 **NVENC on older GeForce cards** — built with CUDA 12.9, so GTX 900/1000 (Maxwell/Pascal)
  keep hardware H.264/HEVC encoding. Zero-copy NvFBC capture on NVIDIA.
- 🕹️ **Controllers that just work** — correct Xbox layout in browsers, Steam and SDL games.
- 🖱️ **Cursor always visible** in the stream, with or without a physical mouse on the PC.
- 🎤 **Remote microphone** — the device's mic appears on the PC as "Nova Mic".
- 📋 **Clipboard sync** — text and images, both ways.

**Library**
- 📚 **Game library** — add games from a folder, Lutris or Steam. Artwork, logos and details are
  fetched automatically (Steam, SteamGridDB, IGDB), with custom artwork by upload, URL or search.
- 🍷 **Windows games** — run through GE-Proton (umu) with a separate prefix per game; Lutris optional.

**Web UI & devices**
- 🎨 **Redesigned web UI** — live desktop preview, stream bar with a network graph, dark and light.
- 🔐 **Per-device permissions** — decide what each paired device may do; rename or unpair anytime.
- 🏷️ **Auto device names** — pairing fills in the name for you, e.g. *Nebula from Ayush's S25 Ultra*.
- 📈 **Session history & health checks** — what streamed, how well, and what needs attention.

### In progress

| Feature | Status |
|---|---|
| Virtual display desktop (wallpaper, icons, taskbar) and keep-alive across resolution changes | 🔨 Fixing |
| Desktop scaling for virtual displays (100–200 %) | 🔨 Building |
| Sleep PC / Wake-on-LAN from the device, host commands, security hardening | 🧪 Testing |
| Native NVENC with loss recovery (no full-frame refresh on packet loss) | 🔨 Building |
| Adaptive bitrate and a connection test | 🔨 Building |
| Local cursor drawn on the device (instant pointer) | 🔨 Building |
| Per-device display profiles and portrait streaming | 🔨 Building |

### Planned

Replay clips (save the last minute), streaming power mode, per-game performance profiles,
auto keyboard when a text field is focused, auto-update from releases, and a browser client.
See [ROADMAP.md](ROADMAP.md).

## Install

Download the [latest release](https://github.com/F-e-n-y-x/nova-host/releases/latest):

| Platform | Package | Install |
|----------|---------|---------|
| **Ubuntu / Debian / Mint** | `nova-host-*-amd64.deb` | `sudo apt install ./nova-host-*.deb` |
| **Fedora / Nobara / Bazzite** | `nova-host-fedora-*-x86_64.rpm` | `sudo dnf install ./nova-host-*.rpm` |

Then open `https://<host-ip>:47990`, create a login, and pair Nebula or Moonlight.
Settings live in `~/.config/nova-host/`. Nova replaces the `sunshine` and `zenith` packages and
copies their settings over on first start.

Building from source: see the [local build notes](docs/building_nova_local.md).

## Versioning

Numeric [semantic versioning](https://semver.org), tagged `nova-vX.Y.Z`. Everything before
**1.0.0** is a pre-release.

## Credits & license

Developed by [Fenyx](https://github.com/F-e-n-y-x).
Nova is a fork of [Zenith](https://github.com/jacksonpate/zenith) by Jackson Pate, itself a fork of
[LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) — go star them. Feature ideas from
[Apollo](https://github.com/ClassicOldSong/Apollo) and
[Sunshine-Foundation](https://github.com/AlkaidLab/foundation-sunshine), reimplemented for Linux.

Licensed [GPL-3.0](LICENSE). Third-party notices: [NOTICE](NOTICE).
