#include "test_framework.h"

#include "aven/core/fs.h"
#include "aven/core/json.h"
#include "aven/core/update.h"

#include <filesystem>

using namespace aven;

AVEN_TEST(update_versions) {
    CHECK(update::isNewer("0.3.1", "0.3.0"));
    CHECK(update::isNewer("v0.10.0", "0.9.9"));
    CHECK(update::isNewer("1.0.0", "0.99.99"));
    CHECK(!update::isNewer("0.3.0", "0.3.0"));
    CHECK(!update::isNewer("v0.2.9", "0.3.0"));
    // Betas come before their release, after the one before it.
    CHECK(update::isNewer("0.4.0-beta.1", "0.3.0"));
    CHECK(update::isNewer("0.4.0", "0.4.0-beta.1"));
    CHECK(!update::isNewer("0.4.0-beta.1", "0.4.0"));
    CHECK(update::isNewer("0.4.0-beta.10", "0.4.0-beta.2"));
    CHECK(update::isNewer("0.4.0-rc.1", "0.4.0-beta.2"));
    CHECK(update::isNewer("0.4.0-beta.1", "0.4.0-beta"));
    // Nonsense is never newer.
    CHECK(!update::isNewer("latest", "0.3.0"));
    CHECK(!update::isNewer("0.4", "0.3.0"));
    CHECK(!update::isNewer("0.4.0x", "0.3.0"));
    CHECK(!update::isNewer("0.4.0", "dev"));
    CHECK(update::parseVersion("0.3.0+build.5").valid);
}

AVEN_TEST(update_picks_the_newest_release_for_this_system) {
    const char* text = R"([
      {"tag_name": "v0.5.0-beta.1", "prerelease": true, "draft": false, "body": "beta", "html_url": "https://x/b",
       "assets": [{"name": "aven-0.5.0-beta.1-linux-x64.zip", "size": 10, "browser_download_url": "https://x/b.zip"}]},
      {"tag_name": "v0.6.0", "prerelease": false, "draft": true, "assets": []},
      {"tag_name": "v0.4.0", "prerelease": false, "draft": false, "body": "## New\n- things", "html_url": "https://x/4",
       "assets": [
         {"name": "aven-0.4.0-windows-x64.zip", "size": 5, "browser_download_url": "https://x/w.zip"},
         {"name": "aven-0.4.0-linux-x64.zip", "size": 20409471, "digest": "sha256:C7E70C88",
          "browser_download_url": "https://x/l.zip"}]},
      {"tag_name": "v0.3.1", "prerelease": false, "draft": false, "assets": []},
      {"tag_name": "v0.3.0", "prerelease": false, "draft": false, "assets": []}
    ])";
    Json releases = Json::parse(text);
    update::Release r;
    CHECK(update::newestRelease(releases, "0.3.0", "linux-x64", false, r));
    CHECK_EQ(r.version, std::string("0.4.0"));
    CHECK_EQ(r.download, std::string("https://x/l.zip"));
    CHECK_EQ(r.fileName, std::string("aven-0.4.0-linux-x64.zip"));
    CHECK_EQ(r.size, uint64_t(20409471));
    CHECK_EQ(r.sha256, std::string("c7e70c88"));
    CHECK_EQ(r.page, std::string("https://x/4"));
    CHECK(!r.beta);
    CHECK(r.notes.find("things") != std::string::npos);
    // With betas on, the beta is newest.
    CHECK(update::newestRelease(releases, "0.3.0", "linux-x64", true, r));
    CHECK_EQ(r.version, std::string("0.5.0-beta.1"));
    CHECK(r.beta);
    // A newer release without this system's download is still found (to point at its page).
    CHECK(update::newestRelease(releases, "0.3.0", "macos-arm64", false, r));
    CHECK_EQ(r.version, std::string("0.4.0"));
    CHECK(r.download.empty());
    // Up to date, or a reply that isn't a list: nothing.
    CHECK(!update::newestRelease(releases, "0.4.0", "linux-x64", false, r));
    CHECK(!update::newestRelease(Json::parse(R"({"message": "API rate limit exceeded"})"), "0.3.0", "linux-x64", false, r));
}

AVEN_TEST(update_sha256) {
    CHECK_EQ(update::sha256("", 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(update::sha256("abc", 3), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // spans two blocks
    CHECK_EQ(update::sha256(two.data(), two.size()),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    std::string million(1000000, 'a');
    auto path = std::filesystem::temp_directory_path() / "aven_sha_test.bin";
    fs::writeBinary(path, million.data(), million.size());
    CHECK_EQ(update::sha256File(path), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    std::filesystem::remove(path);
    CHECK(update::sha256File(path).empty());
}

AVEN_TEST(update_swaps_files_in_and_back_out) {
    namespace stdfs = std::filesystem;
    std::error_code ec;
    stdfs::path root = stdfs::temp_directory_path() / "aven_update_test";
    stdfs::remove_all(root, ec);
    stdfs::path install = root / "Aven", fresh = root / "new", backup = root / "old";
    fs::writeText(install / "aven-editor", "old editor");
    fs::writeText(install / "templates/platformer/project.aven", "old");
    fs::writeText(install / "templates/gone/project.aven", "removed in the new version");
    fs::writeText(install / "My Game/project.aven", "the user's own game");
    fs::writeText(fresh / "aven-editor", "new editor");
    fs::writeText(fresh / "templates/platformer/project.aven", "new");
    fs::writeText(fresh / "players/linux-x64/aven-player", "new player");
    std::string error;
    CHECK(update::swapIn(install, fresh, backup, error));
    CHECK_EQ(fs::readText(install / "aven-editor").value_or(""), std::string("new editor"));
    CHECK_EQ(fs::readText(install / "templates/platformer/project.aven").value_or(""), std::string("new"));
    CHECK(!stdfs::exists(install / "templates/gone")); // folders are replaced whole
    CHECK(stdfs::exists(install / "players/linux-x64/aven-player"));
    CHECK_EQ(fs::readText(install / "My Game/project.aven").value_or(""), std::string("the user's own game"));
    CHECK_EQ(fs::readText(backup / "aven-editor").value_or(""), std::string("old editor"));
    CHECK(stdfs::exists(backup / "templates/gone/project.aven"));

    // Something that can't be moved: everything goes back as it was.
    stdfs::remove_all(root, ec);
    fs::writeText(install / "aven-editor", "old editor");
    fs::writeText(install / "b.txt", "old b");
    fs::writeText(fresh / "aven-editor", "new editor");
    fs::writeText(fresh / "b.txt", "new b");
    fs::writeText(fresh / "c.txt", "new c");
    fs::writeText(backup / "b.txt/in-the-way", "a folder where b.txt's backup should go");
    CHECK(!update::swapIn(install, fresh, backup, error));
    CHECK(error.find("b.txt") != std::string::npos);
    CHECK_EQ(fs::readText(install / "aven-editor").value_or(""), std::string("old editor"));
    CHECK_EQ(fs::readText(install / "b.txt").value_or(""), std::string("old b"));
    CHECK(!stdfs::exists(install / "c.txt"));
    CHECK_EQ(fs::readText(fresh / "aven-editor").value_or(""), std::string("new editor"));
    stdfs::remove_all(root, ec);
}
