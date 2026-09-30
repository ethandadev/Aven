#include "test_framework.h"

#include "rynax/assets/assets.h"
#include "rynax/core/fs.h"
#include "rynax/core/json.h"
#include "rynax/core/uuid.h"
#include "rynax/core/icons.h"
#include "rynax/core/zip.h"
#include "rynax/math/math.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace rynax;

RYNAX_TEST(math_vector_ops) {
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(a + b == Vec3(5, 7, 9));
    CHECK_NEAR(dot(a, b), 32, 1e-6);
    CHECK(cross(Vec3(1, 0, 0), Vec3(0, 1, 0)) == Vec3(0, 0, 1));
    CHECK_NEAR(length(normalize(b)), 1, 1e-6);
}

RYNAX_TEST(math_matrix_inverse) {
    Mat4 m = Mat4::trs({3, -2, 5}, Quat::fromEuler({30, 45, 60}), {2, 3, 0.5f});
    Mat4 id = m * inverse(m);
    for (int i = 0; i < 16; ++i)
        CHECK_NEAR(id.m[i], Mat4::identity().m[i], 1e-4);
}

RYNAX_TEST(math_euler_roundtrip) {
    for (Vec3 e : {Vec3(10, 20, 30), Vec3(-45, 170, 5), Vec3(0, 0, 90), Vec3(80, -30, -120)}) {
        Vec3 back = Quat::fromEuler(e).toEuler();
        Quat q1 = Quat::fromEuler(e), q2 = Quat::fromEuler(back);
        CHECK_NEAR(std::abs(dot(q1, q2)), 1, 1e-4);
        CHECK_NEAR(back.x, e.x, 1e-2);
    }
}

RYNAX_TEST(math_euler_order_matches_unity) {
    // Z is applied first, then X, then Y.
    Quat q = Quat::fromEuler({90, 90, 0});
    Vec3 v = rotate(q, {0, 0, 1});
    // X=90 turns +Z into -Y, then Y=90 leaves -Y unchanged.
    CHECK_NEAR(v.x, 0, 1e-5);
    CHECK_NEAR(v.y, -1, 1e-5);
    CHECK_NEAR(v.z, 0, 1e-5);
}

RYNAX_TEST(math_decompose) {
    Vec3 t{1, 2, 3}, s{2, 1, 4};
    Quat r = Quat::fromEuler({15, 25, 35});
    Vec3 t2, s2;
    Quat r2;
    CHECK(decompose(Mat4::trs(t, r, s), t2, r2, s2));
    CHECK_NEAR(t2.y, 2, 1e-5);
    CHECK_NEAR(s2.z, 4, 1e-4);
    CHECK_NEAR(std::abs(dot(r, r2)), 1, 1e-4);
}

RYNAX_TEST(math_rotation_of_a_damaged_matrix_is_not_nan) {
    // A matrix with nan in it (from maths gone wrong somewhere) reads as no rotation, not nan.
    Mat4 bad = Mat4::trs({0, 0, 0}, Quat::fromEuler({10, 20, 30}), {1, 1, 1});
    bad.m[0] = std::nanf("");
    Quat q = quatFromMatrix(bad);
    CHECK(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w));
    // Squashed flat or mirrored: still a real rotation.
    for (Vec3 scale : {Vec3{0, 0, 0}, Vec3{-1, -1, -1}}) {
        q = quatFromMatrix(Mat4::trs({0, 0, 0}, Quat::fromEuler({0, 180, 0}), scale));
        CHECK(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w));
    }
}

RYNAX_TEST(math_projection) {
    Mat4 p = Mat4::perspective(radians(90), 1, 0.1f, 100);
    Vec4 nearPt = p * Vec4(0, 0, -0.1f, 1);
    Vec4 farPt = p * Vec4(0, 0, -100, 1);
    CHECK_NEAR(nearPt.z / nearPt.w, -1, 1e-4);
    CHECK_NEAR(farPt.z / farPt.w, 1, 1e-4);
    Mat4 view = Mat4::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK_NEAR(transformPoint(view, {0, 0, 0}).z, -5, 1e-5);
}

RYNAX_TEST(json_roundtrip) {
    std::string err;
    Json j = Json::parse(R"({"name": "Player", "pos": [1.5, -2, 3], "tags": ["a", "b"], "on": true,
        "nested": {"x": null, "s": "line\nbreak é"}})",
                         &err);
    CHECK(err.empty());
    CHECK_EQ(j["name"].asString(), std::string("Player"));
    CHECK_NEAR(j["pos"][0].asNumber(), 1.5, 1e-9);
    CHECK_EQ(j["tags"].size(), size_t(2));
    CHECK(j["on"].asBool());
    CHECK(j["nested"]["x"].isNull());
    CHECK(j["missing"]["deep"].isNull());
    Json back = Json::parse(j.dump(2), &err);
    CHECK(err.empty());
    CHECK(back == j);
    CHECK_EQ(back["nested"]["s"].asString(), std::string("line\nbreak \xc3\xa9"));
}

RYNAX_TEST(json_errors_have_locations) {
    std::string err;
    Json::parse("{\n  \"a\": 1,\n  \"b\": }", &err);
    CHECK(err.find("line 3") != std::string::npos);
}

// Hand-edited files: // comments and trailing commas are forgiven.
RYNAX_TEST(json_is_forgiving_about_hand_edits) {
    std::string err;
    Json j = Json::parse("{\n  // the player\n  \"speed\": 5,\n  \"list\": [1, 2,],\n}", &err);
    CHECK(err.empty());
    CHECK_NEAR(j["speed"].asNumber(), 5, 1e-9);
    CHECK_EQ(j["list"].size(), size_t(2));
}

RYNAX_TEST(json_float_formatting) {
    Json j = Json(0.1f);
    CHECK_EQ(j.dump(), std::string("0.1"));
    CHECK_EQ(Json(42).dump(), std::string("42"));
    CHECK_EQ(Json(-3.25).dump(), std::string("-3.25"));
}

RYNAX_TEST(json_preserves_key_order) {
    Json j = Json::object();
    j["zebra"] = 1;
    j["apple"] = 2;
    CHECK_EQ(j.dump(), std::string(R"({"zebra":1,"apple":2})"));
}

RYNAX_TEST(uuid_string_roundtrip) {
    UUID id = UUID::generate();
    CHECK(static_cast<bool>(id));
    CHECK(UUID::fromString(id.toString()) == id);
}

// Paths from games and web requests stay inside their folder (the share server and scripts use this).
RYNAX_TEST(fs_inside_folder_blocks_escapes) {
    namespace stdfs = std::filesystem;
    stdfs::path root = stdfs::temp_directory_path() / "rynax_inside_test";
    std::error_code ec;
    stdfs::remove_all(root, ec);
    stdfs::create_directories(root / "images", ec);
    fs::writeText(root / "images/a.png", "x");
    CHECK(!fs::insideFolder(root, "images/a.png").empty());
    CHECK(!fs::insideFolder(root, "sounds/missing.wav").empty()); // missing is fine, just inside
    CHECK(!fs::insideFolder(root, "images/./a.png").empty());
    for (const char* bad : {"../secret.txt", "images/../../x", "/etc/passwd", "C:/Windows/win.ini", "images\\..\\..\\x",
                            "..", "", "/C:/x"})
        CHECK(fs::insideFolder(root, bad).empty());
    std::string withNul("images/a.png\0../../x", 20);
    CHECK(fs::insideFolder(root, withNul).empty());
#ifndef _WIN32
    stdfs::create_directory_symlink(stdfs::temp_directory_path(), root / "link", ec);
    if (!ec)
        CHECK(fs::insideFolder(root, "link/anything").empty()); // a link that leads out
#endif
}

// Import settings: saved in import.json with only what differs, read back, and forgotten when reset.
RYNAX_TEST(import_settings_roundtrip) {
    namespace stdfs = std::filesystem;
    stdfs::path root = stdfs::temp_directory_path() / "rynax_import_test";
    std::error_code ec;
    stdfs::remove_all(root, ec);
    stdfs::create_directories(root, ec);
    Assets assets;
    assets.setRoot(root);
    CHECK(assets.importFor("images/a.png") == ImportSettings{});
    ImportSettings s;
    s.filter = ImportSettings::Filter::Pixel;
    s.maxSize = 256;
    s.scale = 0.01f;
    int gen = assets.importGeneration();
    assets.setImport("images/a.png", s);
    CHECK(assets.importGeneration() != gen);
    auto text = fs::readText(root / "import.json");
    CHECK(text && text->find("\"max_size\": 256") != std::string::npos && text->find("mipmaps") == std::string::npos);
    Assets other; // another reader of the same project
    other.setRoot(root);
    ImportSettings back = other.importFor("images/a.png");
    CHECK(back == s);
    assets.setImport("images/a.png", {});
    CHECK(!stdfs::exists(root / "import.json")); // nothing left to say
}

// Zip files: a folder packs and unpacks the same (text compressed, the rest stored); a zip made by
// another tool (Python's zipfile, deflated, in a top folder) reads; unsafe or broken ones don't.
RYNAX_TEST(zip_roundtrip_and_safety) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "rynax_zip_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "src/scenes", ec);
    std::string scene(5000, 'a');
    for (size_t i = 0; i < scene.size(); i += 7)
        scene[i] = static_cast<char>('a' + i % 13);
    fs::writeText(dir / "src/scenes/main.scene", scene);
    std::vector<uint8_t> png(3000);
    for (size_t i = 0; i < png.size(); ++i)
        png[i] = static_cast<uint8_t>(i * 131 + 7);
    fs::writeBinary(dir / "src/hero.png", png.data(), png.size());
    fs::writeText(dir / "src/skip.tmp", "left out");
    CHECK(zip::write(dir / "out.zip", dir / "src", [](const std::string& f) { return f != "skip.tmp"; }));
    auto bytes = fs::readBinary(dir / "out.zip");
    CHECK(bytes && bytes->size() < scene.size() + png.size()); // the scene got compressed
    std::vector<zip::Entry> files;
    std::string error;
    CHECK(zip::read(*bytes, files, error));
    CHECK_EQ(files.size(), size_t(2));
    CHECK(zip::extract(dir / "out.zip", dir / "back", error));
    CHECK(fs::readText(dir / "back/scenes/main.scene").value_or("") == scene);
    CHECK(fs::readBinary(dir / "back/hero.png").value_or(std::vector<uint8_t>{}) == png);
    CHECK(!stdfs::exists(dir / "back/skip.tmp"));
    // Programs stay programs: marked in the zip, runnable once unpacked (on macOS and Linux).
    fs::writeText(dir / "src/run-me", "#!/bin/sh\n");
    CHECK(zip::write(dir / "prog.zip", dir / "src", {}, [](const std::string& f) { return f == "run-me"; }));
    CHECK(zip::read(fs::readBinary(dir / "prog.zip").value_or(std::vector<uint8_t>{}), files, error));
    for (auto& f : files)
        CHECK_EQ(f.executable, f.name == "run-me");
    CHECK(zip::extract(dir / "prog.zip", dir / "prog", error));
#if !defined(_WIN32)
    CHECK((stdfs::status(dir / "prog/run-me").permissions() & stdfs::perms::owner_exec) != stdfs::perms::none);
    CHECK((stdfs::status(dir / "prog/hero.png").permissions() & stdfs::perms::owner_exec) == stdfs::perms::none);
#endif

    // Made by Python: deflated, everything inside "My Game/", with a folder entry.
    static const char kPython[] =
    "\x50\x4b\x03\x04\x14\x00\x00\x00\x08\x00\x66\xa1\x3b\x5d\xbd\x98\x9a\x8c\x18\x00\x00\x00\x39\x00\x00\x00\x14\x00\x00\x00\x4d\x79\x20\x47\x61\x6d\x65\x2f\x70\x72\x6f\x6a\x65\x63\x74\x2e\x61\x76\x65\x6e\xab\x56\xca\x4b\xcc\x4d\x55\xb2\x52\x50\x8a\xca\x2c\x28"
    "\x48\x4d\x51\xaa\xe5\xaa\x26\x4a\x08\x00\x50\x4b\x03\x04\x14\x00\x00\x00\x08\x00\x66\xa1\x3b\x5d\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x0f\x00\x00\x00\x4d\x79\x20\x47\x61\x6d\x65\x2f\x73\x63\x65\x6e\x65\x73\x2f\x03\x00\x50\x4b\x03\x04\x14\x00\x00"
    "\x00\x08\x00\x66\xa1\x3b\x5d\xc5\x59\xab\x2b\x15\x00\x00\x00\xd8\x00\x00\x00\x19\x00\x00\x00\x4d\x79\x20\x47\x61\x6d\x65\x2f\x73\x63\x65\x6e\x65\x73\x2f\x6d\x61\x69\x6e\x2e\x73\x63\x65\x6e\x65\xab\x56\x4a\xcd\x2b\xc9\x2c\xc9\x4c\x2d\x56\xb2\x52\x88\x8e\xad"
    "\x55\x18\x26\x00\x00\x50\x4b\x01\x02\x14\x03\x14\x00\x00\x00\x08\x00\x66\xa1\x3b\x5d\xbd\x98\x9a\x8c\x18\x00\x00\x00\x39\x00\x00\x00\x14\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x00\x4d\x79\x20\x47\x61\x6d\x65\x2f\x70\x72\x6f\x6a\x65"
    "\x63\x74\x2e\x61\x76\x65\x6e\x50\x4b\x01\x02\x14\x03\x14\x00\x00\x00\x08\x00\x66\xa1\x3b\x5d\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x0f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x10\x00\xfd\x41\x4a\x00\x00\x00\x4d\x79\x20\x47\x61\x6d\x65\x2f\x73\x63\x65"
    "\x6e\x65\x73\x2f\x50\x4b\x01\x02\x14\x03\x14\x00\x00\x00\x08\x00\x66\xa1\x3b\x5d\xc5\x59\xab\x2b\x15\x00\x00\x00\xd8\x00\x00\x00\x19\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x79\x00\x00\x00\x4d\x79\x20\x47\x61\x6d\x65\x2f\x73\x63\x65\x6e\x65\x73"
    "\x2f\x6d\x61\x69\x6e\x2e\x73\x63\x65\x6e\x65\x50\x4b\x05\x06\x00\x00\x00\x00\x03\x00\x03\x00\xc6\x00\x00\x00\xc5\x00\x00\x00\x00\x00";
    std::vector<uint8_t> py(kPython, kPython + sizeof kPython - 1);
    fs::writeBinary(dir / "py.zip", py.data(), py.size());
    CHECK(zip::extract(dir / "py.zip", dir / "py", error));
    CHECK(fs::readText(dir / "py/project.aven").value_or("").find("Zipped") != std::string::npos); // (made when Rynax was Aven)
    CHECK(stdfs::exists(dir / "py/scenes/main.scene"));

    // "../evil.txt" is refused.
    static const char kEvil[] =
    "\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x66\xa1\x3b\x5d\x83\x16\xdc\x8c\x01\x00\x00\x00\x01\x00\x00\x00\x0b\x00\x00\x00\x2e\x2e\x2f\x65\x76\x69\x6c\x2e\x74\x78\x74\x78\x50\x4b\x01\x02\x14\x03\x14\x00\x00\x00\x00\x00\x66\xa1\x3b\x5d\x83\x16\xdc\x8c\x01\x00"
    "\x00\x00\x01\x00\x00\x00\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x00\x2e\x2e\x2f\x65\x76\x69\x6c\x2e\x74\x78\x74\x50\x4b\x05\x06\x00\x00\x00\x00\x01\x00\x01\x00\x39\x00\x00\x00\x2a\x00\x00\x00\x00\x00";
    std::vector<uint8_t> evil(kEvil, kEvil + sizeof kEvil - 1);
    CHECK(!zip::read(evil, files, error));
    CHECK(error.find("outside") != std::string::npos);
    // Damage: a flipped byte in the data fails the checksum; a cut-off file fails too.
    std::vector<uint8_t> bad = *bytes;
    bad[40] ^= 0x55;
    CHECK(!zip::read(bad, files, error));
    std::vector<uint8_t> cut(bytes->begin(), bytes->begin() + static_cast<long>(bytes->size() / 2));
    CHECK(!zip::read(cut, files, error));
    CHECK(!zip::read(std::vector<uint8_t>{'h', 'i'}, files, error));
    // list() reads just the names, from the file.
    std::vector<std::string> names;
    CHECK(zip::list(dir / "out.zip", names, error));
    CHECK(names.size() == 2 && names[0] == "hero.png" && names[1] == "scenes/main.scene");
    // A "zip bomb": an entry claiming to unpack to far more than deflate can compress. Refused
    // before anything is set aside for it (here: 3000 bytes of deflate claiming to be 900 MB).
    {
        std::vector<uint8_t> bomb = *bytes;
        auto put32 = [&](size_t at, uint32_t v) {
            for (int i = 0; i < 4; ++i)
                bomb[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
        };
        size_t central = 0;
        for (size_t i = 0; i + 4 <= bomb.size(); ++i)
            if (bomb[i] == 'P' && bomb[i + 1] == 'K' && bomb[i + 2] == 1 && bomb[i + 3] == 2) {
                central = i;
                break;
            }
        CHECK(central > 0);
        // The first entry ("hero.png", stored) becomes deflated, with a huge claimed size.
        bomb[central + 10] = 8;
        bomb[central + 11] = 0;
        put32(central + 24, 900u << 20);
        CHECK(!zip::read(bomb, files, error));
        CHECK(error.find("claims to unpack") != std::string::npos);
        fs::writeBinary(dir / "bomb.zip", bomb.data(), bomb.size());
        CHECK(!zip::extract(dir / "bomb.zip", dir / "bomb", error));
    }
    stdfs::remove_all(dir, ec);
}

// App icons: resizing keeps a picture's look; .icns files are laid out right; the icon inside a
// Windows program can be swapped (tested on rynax-player.exe on Windows, or RYNAX_TEST_EXE).
RYNAX_TEST(icons_resize_icns_and_exe) {
    // A 40 x 20 picture: the middle 20 x 20 square is red, the sides blue.
    std::vector<uint8_t> px(40 * 20 * 4);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x) {
            uint8_t* p = &px[(y * 40 + x) * 4];
            bool middle = x >= 10 && x < 30;
            p[0] = middle ? 255 : 0;
            p[2] = middle ? 0 : 255;
            p[3] = 255;
        }
    auto small = icons::resize(px.data(), 40, 20, 8);
    CHECK(small[0] == 255 && small[2] == 0 && small[3] == 255); // cropped to the red square
    auto big = icons::resize(px.data(), 40, 20, 64);
    CHECK(big[(32 * 64 + 32) * 4] == 255);
    auto png = icons::encodePng(big.data(), 64, 64);
    CHECK(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    auto icns = icons::makeIcns({{64, png}, {128, png}});
    CHECK(icns.size() == 8 + 2 * (8 + png.size()) + (8 + png.size())); // icp6, ic12 and ic07
    CHECK(std::string(icns.begin(), icns.begin() + 4) == "icns");
    CHECK_EQ((icns[4] << 24 | icns[5] << 16 | icns[6] << 8 | icns[7]), static_cast<int>(icns.size()));

    std::string error;
    std::vector<uint8_t> notExe(100, 0);
    CHECK(!icons::replaceExeIcon(notExe, png, error));
    std::filesystem::path exePath = std::getenv("RYNAX_TEST_EXE") ? std::filesystem::path(std::getenv("RYNAX_TEST_EXE"))
                                                                  : fs::executableDir() / "rynax-player.exe";
    auto exe = fs::readBinary(exePath);
    if (!exe) {
        std::printf("  (no Windows program to try the icon on here)\n");
        return;
    }
    CHECK(icons::replaceExeIcon(*exe, png, error));
    std::vector<uint8_t> huge(400000, 1);
    std::vector<uint8_t> copy = *exe;
    CHECK(!icons::replaceExeIcon(copy, huge, error) && error.find("too big") != std::string::npos);
    {
        // A signed program: the signature (the certificate table, at the end) goes, since it no longer matches.
        std::vector<uint8_t> signedExe = *exe;
        size_t pe = signedExe[0x3C] | (signedExe[0x3D] << 8);
        size_t opt = pe + 24;
        size_t dirs = (signedExe[opt] | (signedExe[opt + 1] << 8)) == 0x20b ? opt + 112 : opt + 96;
        uint32_t at = static_cast<uint32_t>(signedExe.size());
        auto put32 = [&](size_t p, uint32_t v) {
            for (int i = 0; i < 4; ++i)
                signedExe[p + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        put32(dirs + 32, at);
        put32(dirs + 36, 1024);
        signedExe.resize(at + 1024, 0x5A);
        CHECK(icons::replaceExeIcon(signedExe, png, error));
        CHECK_EQ(signedExe.size(), static_cast<size_t>(at));
        CHECK(signedExe[dirs + 32] == 0 && signedExe[dirs + 36] == 0 && signedExe[dirs + 37] == 0);
    }
    std::filesystem::path out = std::filesystem::temp_directory_path() / "rynax_icon_test.exe";
    CHECK(fs::writeBinary(out, exe->data(), exe->size()));
#ifdef _WIN32
    // Windows reads it back the way Explorer does.
    HMODULE m = LoadLibraryExW(out.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    CHECK(m != nullptr);
    if (m) {
        HRSRC group = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14));
        CHECK(group != nullptr);
        HRSRC icon = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(3));
        CHECK(icon != nullptr && SizeofResource(m, icon) == png.size());
        if (icon) {
            const void* data = LockResource(LoadResource(m, icon));
            CHECK(data && std::memcmp(data, png.data(), png.size()) == 0);
        }
        FreeLibrary(m);
    }
#endif
}
