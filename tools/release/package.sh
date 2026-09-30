#!/usr/bin/env bash
# Packs a built Rynax into a zip people can download and run: the editor and player, what the
# editor needs next to it (templates, data, quests, sdk), the web player for web exports, and
# a short read-me. Used by .github/workflows/release.yml; works locally too:
#
#   tools/release/package.sh build/bin rynax-0.2.0-linux-x64 [web-player-dir] [out-dir]
#
# The zip lands in out-dir (default: dist/).
set -euo pipefail
bin="$1"
name="$2"
web="${3:-}"
out="${4:-dist}"
root="$(cd "$(dirname "$0")/../.." && pwd)"

for exe in rynax-editor rynax-player; do
    if [ ! -f "$bin/$exe" ] && [ ! -f "$bin/$exe.exe" ]; then
        echo "No $exe in $bin. Build Rynax first (cmake --build build --config Release)." >&2
        exit 1
    fi
done

stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$bin"/. "$stage"/
# Things the build makes for testing only.
rm -rf "$stage"/rynax_tests* "$stage"/rynax_example_* "$stage"/rynax_test_module* "$stage"/web
rm -f "$stage"/*.pdb "$stage"/*.ilk "$stage"/*.exp "$stage"/*.lib "$stage"/imgui.ini
# The online multiplayer relay runs on a server: it comes with the Linux download only.
case "$name" in
    *linux*) ;;
    *) rm -f "$stage"/rynax-relay "$stage"/rynax-relay.exe ;;
esac
# Smaller downloads: drop debug symbols (Windows keeps them in the .pdb files removed above).
for exe in "$stage/rynax-editor" "$stage/rynax-player" "$stage/rynax-relay"; do
    if [ -f "$exe" ] && command -v strip >/dev/null; then
        strip "$exe" 2>/dev/null || strip -x "$exe" || true
    fi
done
if [ -n "$web" ]; then
    mkdir -p "$stage/web"
    cp "$web/rynax-player.js" "$web/rynax-player.wasm" "$web/index.html" "$stage/web/"
fi
cp "$root/README.md" "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$stage/"

cat > "$stage/START HERE.txt" <<'EOF'
Rynax - a 2D and 3D game engine for beginners

Open the editor:
  Windows  double-click rynax-editor.exe
           (if Windows SmartScreen warns you, click "More info", then "Run anyway")
  Linux    run ./rynax-editor
           (needs OpenGL 3.3 and the usual desktop libraries; tested on Ubuntu 24.04)

Installing instead: the releases page also has a Setup program for Windows
(Rynax-<version>-Setup.exe, with a Start menu shortcut), a disk image for macOS
(Rynax-<version>-macOS.dmg: drag Rynax into Applications) and a Flatpak for Linux:
  https://github.com/ethandadev/Rynax/releases

Rynax checks for new versions when it starts and offers to install them
(Help > Check for Updates).

Pick a template, press Play, and change things. The Learn button in the top right
walks you through the editor.

Keep the files in this folder together: the editor uses the templates, data, quests,
sdk and web folders next to it, and exports games by copying rynax-player.

Guides: https://github.com/ethandadev/Rynax/tree/main/docs
EOF

mkdir -p "$out"
rm -f "$out/$name.zip"
if command -v zip >/dev/null; then
    (cd "$out" && zip -qry "$name.zip" "$name") # keeps the programs runnable on macOS and Linux
else
    (cd "$out" && cmake -E tar cf "$name.zip" --format=zip "$name")
fi
echo "Packed $out/$name.zip"
