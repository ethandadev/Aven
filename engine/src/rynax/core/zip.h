#pragma once

// .zip files: packing a folder (web exports, sharing a whole project) and unpacking one.
// Text files are compressed (deflate); files that already are (images, sounds) are stored.
// Reading takes stored and deflated entries, which is what zip tools make by default, and
// refuses names that would land outside the folder ("../x", "/etc/x", "C:\x").

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rynax::zip {

namespace stdfs = std::filesystem;

uint32_t crc32(const void* data, size_t size);

// Every file in the folder (paths inside use '/'). include(relativePath) can leave some out.
// Files are marked as programs (executable once unzipped on macOS and Linux) when executable()
// says so, or, without it, when they're executable here.
bool write(const stdfs::path& zipPath, const stdfs::path& folder,
           const std::function<bool(const std::string&)>& include = {},
           const std::function<bool(const std::string&)>& executable = {});

struct Entry {
    std::string name; // "scenes/main.scene"
    std::vector<uint8_t> data;
    bool executable = false; // marked as a program (Unix permissions, from zips made on macOS/Linux)
};

// All the files in a zip held in memory. False (with a reason) for anything broken or unsafe. For
// small zips; extract() and list() read big ones piece by piece.
bool read(const std::vector<uint8_t>& zipBytes, std::vector<Entry>& files, std::string& error);

// The names of the files in a zip, without unpacking anything.
bool list(const stdfs::path& zipPath, std::vector<std::string>& names, std::string& error);

// Unpacks into a folder (made if needed), one file at a time, so only one is ever in memory. If
// every file sits in one top folder ("My Game/..."), that folder is dropped, so the files land
// straight in the destination. Programs stay runnable.
bool extract(const stdfs::path& zipPath, const stdfs::path& folder, std::string& error);

} // namespace rynax::zip
