#pragma once

// Updating Rynax itself: which release is newer, checking a download, and swapping the new files
// in. The editor's updater (editor/src/updater.cpp) does the downloading and the asking; this is
// the part that can be tested without a network.

#include "rynax/core/json.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace rynax::update {

namespace stdfs = std::filesystem;

// "0.3.1", "v0.4.0-beta.2". Pre-releases come before their release: 0.4.0-beta.2 < 0.4.0.
struct Version {
    int major = 0, minor = 0, patch = 0;
    std::string pre; // "beta.2", or empty
    bool valid = false;
};
Version parseVersion(std::string_view text);
int compare(const Version& a, const Version& b); // <0, 0, >0
bool isNewer(std::string_view candidate, std::string_view current);

struct Release {
    std::string version; // "0.3.1"
    std::string notes;   // what changed (Markdown)
    std::string page;    // the release's web page
    bool beta = false;
    // This system's download; empty when the release doesn't have one.
    std::string download, fileName;
    uint64_t size = 0;
    std::string sha256; // lowercase hex, or empty when GitHub didn't say
    std::string signatureUrl; // <download>.sig: our Ed25519 signature (tools/release/update_key.py)
};

// The newest release above `current` in GitHub's list of releases (the JSON from
// /repos/<owner>/<repo>/releases), with the download for `system` ("windows-x64", "macos-arm64",
// "linux-x64"). Drafts are skipped, and betas unless `betas`. False when there's nothing newer.
bool newestRelease(const Json& releases, std::string_view current, std::string_view system, bool betas, Release& out);

// What a release's .sig signs: "aven-update-v1\n<file name>\n<SHA-256 hex>\n". The name carries the
// version, so an old signed download can't pose as a newer one.
std::string signedText(const std::string& fileName, const std::string& sha256Hex);
// True when `signatureText` (hex, as in the .sig file) is the release key's signature for this file.
// The key (hex) is built into the editor; see tools/release/update_key.py.
bool checkSignature(const std::string& publicKeyHex, const std::string& fileName, const std::string& sha256Hex,
                    const std::string& signatureText, std::string& error);

// SHA-256 as lowercase hex.
std::string sha256(const void* data, size_t size);
std::string sha256File(const stdfs::path& path); // empty if the file can't be read

// Moves everything in `fresh` into `install`; what it replaces goes into `backup`. All or nothing:
// if something can't be moved (a file in use, no permission), everything goes back as it was and
// `error` says what. Files in `install` that `fresh` doesn't have are left alone.
bool swapIn(const stdfs::path& install, const stdfs::path& fresh, const stdfs::path& backup, std::string& error);

} // namespace rynax::update
