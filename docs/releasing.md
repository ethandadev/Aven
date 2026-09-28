# Releasing Aven, and signing it

Pushing a tag like `v0.4.0` runs `.github/workflows/release.yml`, which makes:

| System | Installer | Also (what the built-in updater downloads) |
|---|---|---|
| Windows | `Aven-<version>-Setup.exe`: installs for you (no administrator), Start menu shortcut, uninstaller | `aven-<version>-windows-x64.zip` |
| macOS | `Aven-<version>-macOS.dmg`: drag Aven into Applications | `aven-<version>-macos-arm64.zip` (Aven.app) |
| Linux | `Aven-<version>-linux.flatpak` | `aven-<version>-linux-x64.zip` |

The steps for a release are in [CONTRIBUTING.md](../CONTRIBUTING.md#making-a-release). Everything
works without signing; signed apps just open without warnings. Signing is set up once, as
**secrets** in the GitHub repository: **Settings > Secrets and variables > Actions > New repository
secret**. Nothing secret goes in the code.

## macOS: Developer ID and notarization

With a paid Apple Developer account, Aven.app is signed with your **Developer ID** and
**notarized** (checked by Apple), so it opens with a double-click on any Mac. Without it, the app
is ad-hoc signed, and people have to right-click > Open it the first time.

1. **Make the certificate** (once, on your Mac). Only the account holder can make one. Either:
   - **In Xcode** (easiest): **Xcode > Settings > Accounts**, add your Apple ID if it isn't there,
     select your team, **Manage Certificates...**, click **+** and choose **Developer ID
     Application**. Xcode makes it and puts it in your keychain.
   - **On the website**: at [developer.apple.com/account/resources/certificates](https://developer.apple.com/account/resources/certificates/list)
     click **+**, pick **Developer ID Application** (G2 Sub-CA), and upload a certificate signing
     request. Make the request in **Keychain Access > Certificate Assistant > Request a Certificate
     From a Certificate Authority...** (your email, "Saved to disk"). Download the `.cer` and
     double-click it to add it to your keychain.

   (It's *Developer ID Application*, for apps shared outside the App Store; not "Apple
   Development" or "Mac App Distribution". Your Team ID is under **Membership details** at
   [developer.apple.com/account](https://developer.apple.com/account).)
2. **Export it**: open **Keychain Access**, choose **login** and **My Certificates**, find
   "Developer ID Application: *your name* (*TEAMID*)" (it has a triangle: the private key is inside),
   right-click > **Export...** as a `.p12`, and choose a password.
3. **Add two secrets**:
   - `MACOS_CERTIFICATE`: the `.p12` as text. In Terminal: `base64 -i Certificates.p12 | pbcopy`,
     then paste.
   - `MACOS_CERTIFICATE_PASSWORD`: the password from step 2.
4. **Notarizing**. Either:
   - an **App Store Connect API key** (recommended): in [App Store Connect](https://appstoreconnect.apple.com),
     **Users and Access > Integrations > App Store Connect API > Team Keys > +**, with the
     Developer role. Download the `.p8` (only possible once) and add:
     `APPLE_API_KEY` (`base64 -i AuthKey_XXXX.p8 | pbcopy`), `APPLE_API_KEY_ID` (the key's ID) and
     `APPLE_API_ISSUER` (the Issuer ID above the list of keys);
   - or your **Apple ID**: `APPLE_ID` (your email), `APPLE_APP_PASSWORD` (an app-specific password
     from [appleid.apple.com](https://appleid.apple.com) > Sign-In and Security) and `APPLE_TEAM_ID`
     (the 10 characters in the certificate's name).

The next release is signed and notarized. To try it first, run the Release workflow by hand
(**Actions > Release > Run workflow**): the "macOS app" job's log shows the signing and Apple's
answer, and the .dmg is attached to the run.

To sign locally instead: `MACOS_SIGN_IDENTITY="Developer ID Application: ..." tools/release/make_mac_app.sh <folder> <version> <out>`
(the same environment variables as above for notarizing, with `APPLE_API_KEY_PATH` pointing at the
`.p8` file).

## Windows: code signing

Unsigned programs get a blue "Windows protected your PC" (SmartScreen) warning; people click
**More info > Run anyway**. Signing shows your name instead, and the warnings stop as the
certificate builds a reputation. Windows code signing needs a certificate from a company Microsoft
trusts; there's no free one from Microsoft like Apple's with a developer account. The options:

- **Azure Trusted Signing** (Microsoft's service, about $10 a month; for individuals in the US and
  Canada, and organizations in more countries). After Microsoft checks who you are, add:
  `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET` (an app registration with the
  "Trusted Signing Certificate Profile Signer" role on the account), `AZURE_SIGNING_ENDPOINT`
  (for example `https://eus.codesigning.azure.net/`), `AZURE_SIGNING_ACCOUNT` and
  `AZURE_SIGNING_PROFILE`.
- **A certificate as a `.pfx` file**: `WINDOWS_CERTIFICATE` (the file as base64:
  `[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))` in PowerShell) and
  `WINDOWS_CERTIFICATE_PASSWORD`. (Certificates bought since mid-2023 come on a hardware key or in
  a cloud service instead of a file, so this is mostly for older ones.)
- **SignPath Foundation** signs open-source projects for free, through their own GitHub
  integration (not wired into this workflow yet).

With either secret set, the workflow signs `aven-editor.exe`, `aven-player.exe` (both copies) and
the installer. Games exported from Aven are separate: Build & Share signs them with *your*
certificate if you set one in the Desktop apps tab.

## Linux: Flatpak

Nothing to set up. The workflow builds `Aven-<version>-linux.flatpak` from the Linux download;
people install it with `flatpak install Aven-<version>-linux.flatpak` (or by opening it in their
software center). A Flatpak can't replace its own files, so for it the Update window links to the
new download instead of installing it. Publishing on Flathub would make updates automatic; the
manifest (`tools/release/flatpak/`) is the starting point for that.
