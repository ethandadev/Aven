#!/bin/sh
# Regenerates docs/easyscript-api.md from the engine (needs a built editor; uses xvfb-run when there's no display).
set -e
cd "$(dirname "$0")/../.."
editor=${1:-build/bin/rynax-editor}
run=""
[ -z "$DISPLAY" ] && command -v xvfb-run >/dev/null && run="xvfb-run -a"
$run "$editor" templates/blank-2d --panel "reference-md:$(pwd)/docs/easyscript-api.md" --screenshot /tmp/rynax-ref.png --frames 3
