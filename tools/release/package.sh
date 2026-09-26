#!/usr/bin/env bash
# Packs a built Aven into a zip people can download and run: the editor and player, what the
# editor needs next to it (templates, data, quests, sdk), the web player for web exports, and
# a short read-me. Used by .github/workflows/release.yml; works locally too:
#
#   tools/release/package.sh build/bin aven-0.2.0-linux-x64 [web-player-dir] [out-dir]
#
# The zip lands in out-dir (default: dist/).
set -euo pipefail
bin="$1"
name="$2"
web="${3:-}"
out="${4:-dist}"
root="$(cd "$(dirname "$0")/../.." && pwd)"

for exe in aven-editor aven-player; do
    if [ ! -f "$bin/$exe" ] && [ ! -f "$bin/$exe.exe" ]; then
        echo "No $exe in $bin. Build Aven first (cmake --build build --config Release)." >&2
        exit 1
    fi
done

stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$bin"/. "$stage"/
# Things the build makes for testing only.
rm -rf "$stage"/aven_tests* "$stage"/aven_example_* "$stage"/aven_test_module* "$stage"/web
rm -f "$stage"/*.pdb "$stage"/*.ilk "$stage"/*.exp "$stage"/*.lib "$stage"/imgui.ini
# Smaller downloads: drop debug symbols (Windows keeps them in the .pdb files removed above).
for exe in "$stage/aven-editor" "$stage/aven-player"; do
    if [ -f "$exe" ] && command -v strip >/dev/null; then
        strip "$exe" 2>/dev/null || strip -x "$exe" || true
    fi
done
if [ -n "$web" ]; then
    mkdir -p "$stage/web"
    cp "$web/aven-player.js" "$web/aven-player.wasm" "$web/index.html" "$stage/web/"
fi
cp "$root/README.md" "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$stage/"

cat > "$stage/START HERE.txt" <<'EOF'
Aven - a 2D and 3D game engine for beginners

Open the editor:
  Windows  double-click aven-editor.exe
           (if Windows SmartScreen warns you, click "More info", then "Run anyway")
  macOS    the first time, open Terminal in this folder and run:
             xattr -dr com.apple.quarantine .
           then double-click aven-editor (or run ./aven-editor)
  Linux    run ./aven-editor
           (needs OpenGL 3.3 and the usual desktop libraries; tested on Ubuntu 24.04)

Pick a template, press Play, and change things. The Learn button in the top right
walks you through the editor.

Keep the files in this folder together: the editor uses the templates, data, quests,
sdk and web folders next to it, and exports games by copying aven-player.

Guides: https://github.com/ethandadev/Aven/tree/main/docs
EOF

mkdir -p "$out"
rm -f "$out/$name.zip"
if command -v zip >/dev/null; then
    (cd "$out" && zip -qry "$name.zip" "$name") # keeps the programs runnable on macOS and Linux
else
    (cd "$out" && cmake -E tar cf "$name.zip" --format=zip "$name")
fi
echo "Packed $out/$name.zip"
