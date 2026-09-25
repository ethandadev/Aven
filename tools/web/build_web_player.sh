#!/usr/bin/env bash
# Builds the web player (aven-player.js + .wasm) with Emscripten and puts it where the
# editor looks for it (build/bin/web), so "Build & Export > Web" works.
#
#   source /path/to/emsdk/emsdk_env.sh
#   tools/web/build_web_player.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
if ! command -v emcmake >/dev/null; then
    echo "Emscripten isn't set up. Install it from https://emscripten.org and run: source emsdk_env.sh" >&2
    exit 1
fi
emcmake cmake -S "$root" -B "$root/build-web" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build-web" --target aven_player
out="${1:-$root/build/bin/web}"
mkdir -p "$out"
cp "$root/build-web/bin/aven-player.js" "$root/build-web/bin/aven-player.wasm" "$root/build-web/bin/index.html" "$out/"
echo "Web player ready in $out"
