#include "rynax/core/update.h"

#include "rynax/core/ed25519.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

namespace rynax::update {

Version parseVersion(std::string_view text) {
    Version v;
    size_t i = 0;
    if (i < text.size() && (text[i] == 'v' || text[i] == 'V'))
        ++i;
    int* parts[] = {&v.major, &v.minor, &v.patch};
    for (int p = 0; p < 3; ++p) {
        if (p > 0) {
            if (i >= text.size() || text[i] != '.')
                return {};
            ++i;
        }
        size_t start = i;
        long n = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && i - start < 9)
            n = n * 10 + (text[i++] - '0');
        if (i == start)
            return {};
        *parts[p] = static_cast<int>(n);
    }
    if (i < text.size() && text[i] == '-') {
        size_t end = text.find('+', i);
        v.pre = std::string(text.substr(i + 1, end == std::string_view::npos ? std::string_view::npos : end - i - 1));
        if (v.pre.empty())
            return {};
    } else if (i < text.size() && text[i] != '+') {
        return {};
    }
    v.valid = true;
    return v;
}

namespace {

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == '.')
            out.emplace_back();
        else
            out.back() += c;
    }
    return out;
}

bool numeric(const std::string& s) {
    return !s.empty() && s.size() < 10 && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

} // namespace

int compare(const Version& a, const Version& b) {
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() || b.pre.empty())
        return a.pre.empty() == b.pre.empty() ? 0 : (a.pre.empty() ? 1 : -1);
    // Semantic versioning: dot by dot, numbers as numbers, and numbers before words.
    auto x = split(a.pre), y = split(b.pre);
    for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
        bool nx = numeric(x[i]), ny = numeric(y[i]);
        if (nx && ny) {
            long p = std::stol(x[i]), q = std::stol(y[i]);
            if (p != q)
                return p < q ? -1 : 1;
        } else if (nx != ny) {
            return nx ? -1 : 1;
        } else if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
}

bool isNewer(std::string_view candidate, std::string_view current) {
    Version c = parseVersion(candidate), now = parseVersion(current);
    return c.valid && now.valid && compare(c, now) > 0;
}

bool newestRelease(const Json& releases, std::string_view current, std::string_view system, bool betas, Release& out) {
    Version now = parseVersion(current);
    if (!releases.isArray() || !now.valid)
        return false;
    bool found = false;
    Version best;
    for (auto& r : releases.elements()) {
        if (!r.isObject() || r["draft"].asBool(false))
            continue;
        bool beta = r["prerelease"].asBool(false);
        Version v = parseVersion(r["tag_name"].asString(""));
        if (!v.valid || (beta && !betas) || compare(v, now) <= 0 || (found && compare(v, best) <= 0))
            continue;
        Release rel;
        rel.version = r["tag_name"].asString("");
        if (!rel.version.empty() && (rel.version[0] == 'v' || rel.version[0] == 'V'))
            rel.version.erase(0, 1);
        rel.notes = r["body"].asString("");
        rel.page = r["html_url"].asString("");
        rel.beta = beta;
        // Release downloads are named rynax-<version>-<system>.zip, with a signature beside each
        // (<name>.sig). Only the exact name counts: the signed name is what ties a download to its version.
        std::string wanted = "rynax-" + rel.version + "-" + std::string(system) + ".zip";
        for (auto& a : r["assets"].elements())
            if (a["name"].asString("") == wanted + ".sig")
                rel.signatureUrl = a["browser_download_url"].asString("");
        for (auto& a : r["assets"].elements()) {
            std::string name = a["name"].asString("");
            if (name != wanted)
                continue;
            rel.fileName = name;
            rel.download = a["browser_download_url"].asString("");
            double size = a["size"].asNumber(0);
            rel.size = size > 0 ? static_cast<uint64_t>(size) : 0;
            std::string digest = a["digest"].asString("");
            if (digest.rfind("sha256:", 0) == 0)
                rel.sha256 = digest.substr(7);
            std::transform(rel.sha256.begin(), rel.sha256.end(), rel.sha256.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            break;
        }
        out = rel;
        best = v;
        found = true;
    }
    return found;
}

// --- SHA-256 (FIPS 180-4)

namespace {

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block[64];
    size_t used = 0;
    uint64_t length = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    void add(const uint8_t* p, size_t n) {
        length += n;
        while (n > 0) {
            size_t take = std::min(n, sizeof block - used);
            std::copy(p, p + take, block + used);
            used += take;
            p += take;
            n -= take;
            if (used == sizeof block) {
                compress(block);
                used = 0;
            }
        }
    }

    std::string finish() {
        uint64_t bits = length * 8;
        uint8_t pad = 0x80;
        add(&pad, 1);
        uint8_t zero = 0;
        while (used != 56)
            add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        add(len, 8);
        std::string hex;
        char buf[9];
        for (uint32_t v : h) {
            std::snprintf(buf, sizeof buf, "%08x", v);
            hex += buf;
        }
        return hex;
    }
};

} // namespace

std::string sha256(const void* data, size_t size) {
    Sha256 s;
    s.add(static_cast<const uint8_t*>(data), size);
    return s.finish();
}

std::string sha256File(const stdfs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    Sha256 s;
    std::vector<char> buffer(1 << 16);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (in.gcount() > 0)
            s.add(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(in.gcount()));
    }
    return in.bad() ? std::string() : s.finish();
}

// --- Signed downloads

std::string signedText(const std::string& fileName, const std::string& sha256Hex) {
    // ("aven-update-v1": from when Rynax was called Aven. Installs of Aven 0.4 and 0.5 check it, and the
    // release key signs it, so it stays as it is.)
    return "aven-update-v1\n" + fileName + "\n" + sha256Hex + "\n";
}

bool checkSignature(const std::string& publicKeyHex, const std::string& fileName, const std::string& sha256Hex,
                    const std::string& signatureText, std::string& error) {
    auto fromHex = [](const std::string& text, size_t bytes, std::vector<uint8_t>& out) {
        std::string hex;
        for (char c : text)
            if (!std::isspace(static_cast<unsigned char>(c)))
                hex += c;
        if (hex.size() != bytes * 2)
            return false;
        out.clear();
        for (size_t i = 0; i < hex.size(); i += 2) {
            int value = 0;
            for (size_t j = i; j < i + 2; ++j) {
                char c = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[j])));
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                    return false;
                value = value * 16 + (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c - 'a' + 10);
            }
            out.push_back(static_cast<uint8_t>(value));
        }
        return true;
    };
    std::vector<uint8_t> key, signature;
    if (!fromHex(publicKeyHex, 32, key)) {
        error = "this copy of Rynax has no key to check updates with";
        return false;
    }
    if (!fromHex(signatureText, 64, signature)) {
        error = "the release's signature file is damaged";
        return false;
    }
    std::string text = signedText(fileName, sha256Hex);
    if (!ed25519::verify(key.data(), reinterpret_cast<const uint8_t*>(text.data()), text.size(), signature.data())) {
        error = "the download isn't signed by Rynax's release key";
        return false;
    }
    return true;
}

// --- Swapping the files in

bool swapIn(const stdfs::path& install, const stdfs::path& fresh, const stdfs::path& backup, std::string& error) {
    std::error_code ec;
    std::vector<std::string> names;
    for (auto& e : stdfs::directory_iterator(fresh, ec))
        names.push_back(e.path().filename().string());
    if (ec || names.empty()) {
        error = "the new version's files are missing";
        return false;
    }
    std::sort(names.begin(), names.end());
    stdfs::create_directories(backup, ec);
    struct Moved {
        std::string name;
        bool replaced;
    };
    std::vector<Moved> done;
    auto undo = [&] {
        std::error_code e;
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            stdfs::rename(install / it->name, fresh / it->name, e);
            if (it->replaced)
                stdfs::rename(backup / it->name, install / it->name, e);
        }
    };
    for (auto& name : names) {
        stdfs::path target = install / name;
        bool replaced = stdfs::exists(stdfs::symlink_status(target, ec));
        if (replaced) {
            stdfs::rename(target, backup / name, ec);
            if (ec) {
                error = "couldn't replace " + name + " (is it open in another program?): " + ec.message();
                undo();
                return false;
            }
        }
        stdfs::rename(fresh / name, target, ec);
        if (ec) {
            error = "couldn't move in the new " + name + ": " + ec.message();
            if (replaced) {
                std::error_code e;
                stdfs::rename(backup / name, target, e);
            }
            undo();
            return false;
        }
        done.push_back({name, replaced});
    }
    return true;
}

} // namespace rynax::update
