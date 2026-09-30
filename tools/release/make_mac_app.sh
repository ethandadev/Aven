#!/usr/bin/env bash
# Makes the macOS downloads from a release folder (package.sh's, with players/ added):
#
#   Rynax-<version>-macOS.dmg        the installer: open it and drag Rynax into Applications
#   rynax-<version>-macos-arm64.zip  Rynax.app zipped; what the editor's updater downloads
#
#   tools/release/make_mac_app.sh <package-folder> <version> <out-dir>
#   tools/release/make_mac_app.sh --app <Rynax.app> <version> <out-dir>
#
# --app signs, notarizes and packs a Rynax.app that's already made (a release's, published before
# a Developer ID was set up) without changing what's inside it. See notarize-mac.yml.
#
# Rynax.app holds the editor and player in Contents/MacOS and everything else (templates, data,
# players for other systems...) in Contents/Resources.
#
# Signing, all optional (without it the app is ad-hoc signed and opens with right-click > Open):
#   MACOS_SIGN_IDENTITY   a "Developer ID Application: Name (TEAMID)" certificate in the keychain
# Notarizing (needs MACOS_SIGN_IDENTITY), with an App Store Connect API key:
#   APPLE_API_KEY_PATH (the .p8 file), APPLE_API_KEY_ID, APPLE_API_ISSUER
# or with an Apple ID:
#   APPLE_ID, APPLE_APP_PASSWORD (an app-specific password from appleid.apple.com), APPLE_TEAM_ID
# MACOS_BUNDLE_ID changes the app's identifier (default io.github.ethandadev.rynax).
set -euo pipefail
existing=""
if [ "${1:-}" = "--app" ]; then
    shift
    existing="$1" # (in place of the package folder)
fi
pkg="$1"
version="$2"
out="$3"
root="$(cd "$(dirname "$0")/../.." && pwd)"
bundle_id="${MACOS_BUNDLE_ID:-io.github.ethandadev.rynax}"
identity="${MACOS_SIGN_IDENTITY:-}"
short="${version%%-*}" # CFBundleShortVersionString takes numbers only: 0.4.0-beta.1 -> 0.4.0

mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT # (also when a step fails)
app="$work/Rynax.app"
if [ -n "$existing" ]; then
    test -x "$existing/Contents/MacOS/rynax-editor" || { echo "$existing isn't a Rynax.app." >&2; exit 1; }
    ditto "$existing" "$app"
else
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$pkg/rynax-editor" "$pkg/rynax-player" "$app/Contents/MacOS/"
# (aven-editor starts rynax-editor: Aven 0.4 and 0.5, as Rynax was called, update by starting it.)
if [ -f "$pkg/aven-editor" ]; then cp "$pkg/aven-editor" "$app/Contents/MacOS/"; fi
for d in data templates quests sdk web players; do
    if [ -d "$pkg/$d" ]; then cp -R "$pkg/$d" "$app/Contents/Resources/"; fi
done
cp "$pkg/README.md" "$pkg/LICENSE" "$pkg/THIRD_PARTY_NOTICES.md" "$app/Contents/Resources/"
cp "$root/resources/icon/rynax.icns" "$app/Contents/Resources/AppIcon.icns"
printf 'APPL????' > "$app/Contents/PkgInfo"
cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Rynax</string>
  <key>CFBundleDisplayName</key><string>Rynax</string>
  <key>CFBundleIdentifier</key><string>$bundle_id</string>
  <key>CFBundleExecutable</key><string>rynax-editor</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$short</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.developer-tools</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
  <key>NSHumanReadableCopyright</key><string>Rynax is MIT licensed.</string>
</dict>
</plist>
EOF
fi

# Hardened runtime (needed for notarizing). Games and the editor load native modules people build
# themselves, which aren't signed by the same team, so library validation is off.
entitlements="$work/entitlements.plist"
cat > "$entitlements" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict>
</plist>
EOF

# Every Mach-O program inside, then the app (which seals everything else).
programs=("$app/Contents/MacOS/rynax-player")
if [ -f "$app/Contents/MacOS/aven-editor" ]; then
    programs+=("$app/Contents/MacOS/aven-editor")
fi
if [ -f "$app/Contents/Resources/players/macos-arm64/rynax-player" ]; then
    programs+=("$app/Contents/Resources/players/macos-arm64/rynax-player")
fi
if [ -n "$identity" ]; then
    echo "Signing with: $identity"
    for p in "${programs[@]}"; do
        codesign --force --timestamp --options runtime --entitlements "$entitlements" --sign "$identity" "$p"
    done
    codesign --force --timestamp --options runtime --entitlements "$entitlements" --sign "$identity" "$app"
else
    echo "No MACOS_SIGN_IDENTITY: ad-hoc signing (the app opens with right-click > Open the first time)."
    for p in "${programs[@]}"; do codesign --force --sign - "$p"; done
    codesign --force --sign - "$app"
fi
codesign --verify --deep --strict --verbose=2 "$app"

notarize() { # notarize <file>: sends it to Apple and waits; fails if Apple doesn't accept it
    local args
    if [ -n "${APPLE_API_KEY_PATH:-}" ]; then
        args=(--key "$APPLE_API_KEY_PATH" --key-id "$APPLE_API_KEY_ID" --issuer "$APPLE_API_ISSUER")
    else
        args=(--apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID")
    fi
    local result
    result="$(xcrun notarytool submit "$1" "${args[@]}" --wait --timeout 60m 2>&1)" || true
    echo "$result"
    if ! grep -q "status: Accepted" <<<"$result"; then
        local id
        id="$(grep -m1 -E '^ *id: ' <<<"$result" | awk '{print $2}')"
        if [ -n "$id" ]; then xcrun notarytool log "$id" "${args[@]}" || true; fi
        echo "Apple didn't accept $1 (see above)." >&2
        return 1
    fi
}
can_notarize=""
if [ -n "$identity" ] && { [ -n "${APPLE_API_KEY_PATH:-}" ] || [ -n "${APPLE_ID:-}" ]; }; then
    can_notarize=1
fi

if [ -n "$can_notarize" ]; then
    echo "Notarizing the app..."
    ditto -c -k --keepParent "$app" "$work/notarize.zip"
    notarize "$work/notarize.zip"
    xcrun stapler staple "$app"
fi

# For the updater: Rynax.app in a zip (zip keeps the programs runnable).
rm -f "$out/rynax-$version-macos-arm64.zip"
(cd "$work" && zip -qry -X "$out/rynax-$version-macos-arm64.zip" Rynax.app)

# The installer: a disk image with Rynax.app and a shortcut to Applications to drag it onto.
dmg_root="$work/dmg"
mkdir -p "$dmg_root"
ditto "$app" "$dmg_root/Rynax.app"
ln -s /Applications "$dmg_root/Applications"
# The mounted disk shows Rynax's icon (SetFile comes with Xcode's command line tools).
cp "$root/resources/icon/rynax.icns" "$dmg_root/.VolumeIcon.icns"
if command -v SetFile > /dev/null; then
    SetFile -c icnC "$dmg_root/.VolumeIcon.icns" || true
    SetFile -a C "$dmg_root" || true
fi
dmg="$out/Rynax-$version-macOS.dmg"
rm -f "$dmg"
hdiutil create -volname "Rynax" -srcfolder "$dmg_root" -ov -format UDZO "$dmg"
if [ -n "$identity" ]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi
if [ -n "$can_notarize" ]; then
    echo "Notarizing the installer..."
    notarize "$dmg"
    xcrun stapler staple "$dmg"
fi
echo "Made $dmg and $out/rynax-$version-macos-arm64.zip"
