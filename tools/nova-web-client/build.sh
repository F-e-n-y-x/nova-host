#!/bin/bash
# Build the Nova browser client sidecar (patched moonlight-web-stream) from source.
#
# Usage: build.sh [--theme-only] [BUILD_DIR]
#   BUILD_DIR defaults to $NOVA_WEB_CLIENT_BUILD_DIR or ./build-nova-web-client.
#   --theme-only re-stages BUILD_DIR/dist/static from the last frontend build and applies Nova's
#   interface (theme/) again, without rebuilding anything (for work on the theme).
#   Output: BUILD_DIR/dist/{web-server,streamer,static/,VERSION,LICENSE.moonlight-web-stream,THIRD-PARTY.txt}
#   Package it with NOVA_WEB_CLIENT_DIST=BUILD_DIR/dist in the environment of the Nova build.
#
# Needs git, curl, a C toolchain, cmake, clang (bindgen), libssl-dev and Node/npm.
# Rust is taken from $CARGO_HOME/$RUSTUP_HOME (defaults inside BUILD_DIR); the pinned
# nightly is installed there with rustup if missing, never system-wide.
# On atom, run it through the build queue:
#   /DATA/blue/Projects/Nova-Nebula/tools/buildq nova-web-client ./build.sh /DATA/blue/...
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=upstream.env
. "$HERE/upstream.env"

THEME_ONLY=0
if [ "${1:-}" = "--theme-only" ]; then THEME_ONLY=1; shift; fi
BUILD_DIR=${1:-${NOVA_WEB_CLIENT_BUILD_DIR:-$PWD/build-nova-web-client}}
mkdir -p "$BUILD_DIR"
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
SRC=$BUILD_DIR/src
DIST=$BUILD_DIR/dist
export RUSTUP_HOME=${RUSTUP_HOME:-$BUILD_DIR/rustup}
export CARGO_HOME=${CARGO_HOME:-$BUILD_DIR/cargo}
export CARGO_BUILD_JOBS=${CARGO_BUILD_JOBS:-6}
export CARGO_TARGET_DIR=$BUILD_DIR/target
export PATH=$CARGO_HOME/bin:$PATH

log() { printf '[nova-web-client] %s\n' "$*"; }

# -- Nova's interface on top of the built frontend: fonts (pinned npm tarball) + theme/.
FONTS=$BUILD_DIR/fonts
stage_static() {
  if [ ! -f "$FONTS/package/files/geist-sans-latin-400-normal.woff2" ]; then
    log "fetching @fontsource/geist-sans@$GEIST_SANS_VERSION"
    rm -rf "$FONTS" && mkdir -p "$FONTS"
    (cd "$FONTS" && npm pack --silent "@fontsource/geist-sans@$GEIST_SANS_VERSION" >/dev/null)
    echo "$GEIST_SANS_SHA256  $FONTS/fontsource-geist-sans-$GEIST_SANS_VERSION.tgz" | sha256sum -c --quiet -
    tar -xzf "$FONTS/fontsource-geist-sans-$GEIST_SANS_VERSION.tgz" -C "$FONTS" package/LICENSE package/files
  fi
  rm -rf "$DIST/static"
  cp -r "$SRC/dist" "$DIST/static"
  python3 "$HERE/theme/apply-theme.py" "$DIST/static" "$FONTS/package/files"
}
if [ "$THEME_ONLY" = 1 ]; then
  [ -d "$SRC/dist" ] && [ -d "$DIST" ] || { echo "error: no earlier build in $BUILD_DIR" >&2; exit 1; }
  stage_static
  log "done: $DIST/static"
  exit 0
fi

# -- Rust toolchain (local to the build dir)
if ! command -v rustup >/dev/null 2>&1; then
  log "installing rustup into $CARGO_HOME"
  curl -sSfL https://static.rust-lang.org/rustup/dist/x86_64-unknown-linux-gnu/rustup-init -o "$BUILD_DIR/rustup-init"
  chmod +x "$BUILD_DIR/rustup-init"
  "$BUILD_DIR/rustup-init" -y --no-modify-path --profile minimal --default-toolchain none
fi
rustup toolchain install --profile minimal "$MWS_RUST_TOOLCHAIN" >/dev/null

# -- Source at the pinned commit
if [ ! -d "$SRC/.git" ]; then
  log "cloning $MWS_REPO ($MWS_TAG)"
  git clone --quiet --branch "$MWS_TAG" "$MWS_REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet --tags origin "$MWS_TAG" || true
git -C "$SRC" checkout --quiet --force "$MWS_COMMIT"
git -C "$SRC" clean -fdxq -e node_modules
actual=$(git -C "$SRC" rev-parse HEAD)
if [ "$actual" != "$MWS_COMMIT" ]; then
  echo "error: $MWS_TAG resolved to $actual, expected $MWS_COMMIT" >&2
  exit 1
fi
for p in "$HERE"/patches/*.patch; do
  log "applying $(basename "$p")"
  git -C "$SRC" apply --whitespace=nowarn "$p"
done

cd "$SRC"
# -- Frontend (generate-bindings runs the ts-rs export test, so it needs cargo too)
log "building web frontend"
npm ci --no-audit --no-fund --loglevel=error
npm run build

# -- Web server and streamer
log "building web-server and streamer (release)"
cargo build --release --locked --bin web-server --bin streamer

# -- Stage (stripped: the debug symbols would triple the package size)
rm -rf "$DIST"
mkdir -p "$DIST"
for bin in web-server streamer; do
  strip --strip-unneeded -o "$DIST/$bin" "$CARGO_TARGET_DIR/release/$bin"
  chmod 0755 "$DIST/$bin"
done
stage_static
cp "$SRC/LICENSE" "$DIST/LICENSE.moonlight-web-stream"
{
  echo "moonlight-web-stream $MWS_TAG ($MWS_COMMIT)"
  echo "source: $MWS_REPO"
  for p in "$HERE"/patches/*.patch; do echo "patch: $(basename "$p") sha256=$(sha256sum "$p" | cut -d' ' -f1)"; done
  echo "theme: Nova interface sha256=$(cd "$HERE/theme" && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
} > "$DIST/VERSION"
# Licences of every Rust crate linked into the two programs.
cargo metadata --format-version 1 --locked --offline --filter-platform x86_64-unknown-linux-gnu | python3 -c '
import json, sys
meta = json.load(sys.stdin)
print("Rust crates in moonlight-web-stream'"'"'s web-server and streamer (name version licence source):")
for p in sorted(meta["packages"], key=lambda p: (p["name"], p["version"])):
    print(p["name"], p["version"], p.get("license") or "see " + (p.get("license_file") or "crate"), p.get("repository") or "")
' > "$DIST/THIRD-PARTY.txt"
( cd "$DIST" && sha256sum web-server streamer > SHA256SUMS )
log "done: $DIST"
