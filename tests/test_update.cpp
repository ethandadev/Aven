#include "test_framework.h"

#include "aven/core/fs.h"
#include "aven/core/json.h"
#include "aven/core/ed25519.h"
#include "aven/core/update.h"

#include <filesystem>
#include <string>
#include <vector>

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
          "browser_download_url": "https://x/l.zip"},
         {"name": "aven-0.4.0-linux-x64.zip.sig", "browser_download_url": "https://x/l.zip.sig"},
         {"name": "aven-0.3.9-linux-x64.zip", "size": 1, "browser_download_url": "https://x/old.zip"}]},
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
    CHECK_EQ(r.signatureUrl, std::string("https://x/l.zip.sig"));
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


namespace {
std::vector<uint8_t> hexBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}
std::string hexOf(const uint8_t* p, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += digits[p[i] >> 4];
        s += digits[p[i] & 15];
    }
    return s;
}
} // namespace

AVEN_TEST(ed25519_sha512) {
    uint8_t out[64];
    ed25519::sha512(reinterpret_cast<const uint8_t*>("abc"), 3, out);
    CHECK_EQ(hexOf(out, 64), std::string("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
    ed25519::sha512(nullptr, 0, out);
    CHECK_EQ(hexOf(out, 64), std::string("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"));
    std::string two(200, 'x'); // spans two blocks
    ed25519::sha512(reinterpret_cast<const uint8_t*>(two.data()), two.size(), out);
    CHECK_EQ(hexOf(out, 64), std::string("ef978e23dc520404ae16fd17bde9ee5945610d671551d6863a5ffbc99433fc726726e51f989b886191be9325b8f8b03b1a63fe3e5eff23d126c2f41f07d2bf87"));
}

AVEN_TEST(ed25519_verifies_openssl_signatures) {
    // Keys, messages and signatures made by OpenSSL (openssl pkeyutl -sign -rawin).
    const char* vectors[][3] = {
        {"a20055602784c14a2af7057eedfa7ac10d5e87aa5fbe0b5f93b1cacd610b2e15",
         "21cb4b7d62cb7fa2ebc6803d7a61239e14e3a741681d53905f5c8be4d0e1ebc1444b8b0cdd12183f325fd74fbf5da30d45257e509a938f2de395798c5accbe522c0d6f2c677e5d3dd34b95cbc3870f63f83b5e2c8818c688e544c753109d90fe91274396792a873b7196b62dd017c91c4ac8aa44472b1d8ddc378acbc66cbb1c38ebd4438b0a4c2b8d16319c319b79e7d174478dd07d336802c4a5d993b2f4ad1722895241ac2478be807cf713b88a3850eb787610e8b197515b2d982e4ffa95f1af472f5d95fe23086d9678173334df255085897d1d52c8fb813ce0446030f5de2decb414602e15cab288f4f8156d",
         "08faccc5a03fff77fbad1581cfc332418c856e48ade111aadf53bbd47e1f3a9a01ef679e12bf34289ae6b3fb893fed76274c5d10341f5d90f981920092fc0309"},
        {"c3bc06437abdf8931d2c197df46b7dd0bd02177de72d6f2e284ebd353532ae92",
         "92570f56564ed02239029bf037e01bc1f3cc03f16606",
         "214556cbed6a497e155ff1e9933613e3aaecf193a05c8ad4690c42bac93be79c2fba5157e259337935a8d24a9b275c0cf1f8db58e8d5ee7551ecbb71498c790f"},
        {"4a0a95ba328727d80c7653f4e380ac0e8614e3049d0d23d6718c79961ea35f2a",
         "5c0108c8b8e8b31f0224539f4f14ae5e1f368e12740e24b24d9439bf674cabc91851d1012f7c6765de67bb02fbe1c001de41803d9bb7700d2d5e1d65fac4aa730e1f2f3ee687e29bf6daa73e582882df7a333d5197149e3b2bfa67d67cae763da86cf93c543e4b80d63d4e3dc93c6c5b7fe1ce671c2568ca2a0972d0a55a3ddfd9db7d7b4dddcbd23f67f57a1e1a78878d5865780f0a502bd9a1ac6da43ca5e03b86e2547a49adb69069450679dd13c4c81c7ce76fabd2192ea2180a8b31adfc65eec2d3090766feaf9c2dd5caf31ebe36438404d065a0983916bfd8c977b32d92e3a8f2bec7b81bff",
         "8f885da76db1982c47bb2c5d855e950e0e961af167ee0fa64f04e7005ae1ac7c583d9014c6460763d81f4980d650b90b498ea555c8856ed4e49c1dc086fff60c"},
        {"a13237e720318262661ae68bf76802a6c27fffb2d2e5a85e63edb7c9f9da6f8c",
         "1905f15eaf482f65e4792a8f19001e1a1ed6939e66c250fd2462e19040b4195e729fb934df9c251250183beb4692becc3f0ea2b9c04ec28c89c378f4303a22e2ab7f1706cf7b6f7206dd4073200b7c62184b4da93741a3e801475b8af56372b324dbd6428f8f2bfcf1467cc2c971703c85c3c25d2440e698baab110c038fe5c10ad5fb83880d6af311894856be3e469f9460a56bb00562744828eafe57b9767b66ee8181f63b",
         "1a4d2f7710a4bbe7640e804136df29d344ddf971ea064c9eb0e8b34d9fbdd311e0a6cfed24e1d53c321e34f32626e15aba96eb71c38bfb6f0c396ffb76a09a0e"},
        {"008976aa8a7a4e51efa66aff5b30215e5bb59bdc7e8021fdd1375517705b7e74",
         "e71150ee24c03fcdc54b3b75e016fe8bbad69ecfe9d6cf3ed3289f4854b35baa2b79abd98a8bc7523279a333acaf6f647f5d8f69f0b17f4f88be9c6e035ede7ce593e6eb6de82d678fc726a2aed71796a5cdf77b5e1656c6f589d40fed72932aa96f67530df02923eacda61da606dba3dfd32d12760c94469e25da8fd7b5275811f9e7185758383a715c7c88aa6fab957dd1c19ca5fdbff5485919325425b8c34e741ab99f6363a70fc112d5d0c553efd255543421b6221398db0f56b49068422a07979fd89269708c7b581de681b5e4e1e739b0d37a8600aa07",
         "a1c8d133d416d0a3663aeb96e0a71d942ac9ce59116c751a240c2a35afa595f9de56dea63fb46b0b668873fdec60a04a60279918359969070b0fbd17c5026101"},
        {"2c98d1882bcbf2943a3c58415144c17f9f754df9b4c66b63ef6ab72c6e0f6c45",
         "843271e0548c813af6da51aa24aba625a4b98757a3628b8bb49c58530e8049110a5d396b898f8904ce173a87a1c01067cba8b086872563c34f7abdab5088cb823f5b5d64c35cfeac0d43f86d31a2453d2d319019d0ebae3ea3ace85cd9410139b937c278d0deee30239dfb1b3eaffa9901fe4dd936fa8ad56e1570379d1486fd1a96c39aecccfb876ed5e0d5debadd395f5b97a2d5ed11f3438a015a9f5997a668b448ddd69e",
         "8a4f860ee73e244a10ade6d9c2bf894a44786daa0649c5097972526398afd1ea31ea07ab4aec35dc67b9951beb9dddf88779322dbcef86994e5b92284ef53509"},
    };
    for (auto& v : vectors) {
        auto pub = hexBytes(v[0]), msg = hexBytes(v[1]), sig = hexBytes(v[2]);
        CHECK(ed25519::verify(pub.data(), msg.data(), msg.size(), sig.data()));
        // Any change to the message, the signature or the key: refused.
        std::vector<uint8_t> longer = msg;
        longer.push_back(0);
        CHECK(!ed25519::verify(pub.data(), longer.data(), longer.size(), sig.data()));
        for (int i : {0, 17, 31, 32, 50, 63}) {
            auto bad = sig;
            bad[static_cast<size_t>(i)] ^= 0x04;
            CHECK(!ed25519::verify(pub.data(), msg.data(), msg.size(), bad.data()));
        }
        auto otherKey = pub;
        otherKey[3] ^= 1;
        CHECK(!ed25519::verify(otherKey.data(), msg.data(), msg.size(), sig.data()));
    }
    // S not reduced below L (the same signature, spelled S + L): refused.
    auto pub = hexBytes(vectors[0][0]), msg = hexBytes(vectors[0][1]), sig = hexBytes(vectors[0][2]);
    const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                           0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
    int carry = 0;
    for (int i = 0; i < 32; ++i) {
        int s = sig[static_cast<size_t>(32 + i)] + L[i] + carry;
        sig[static_cast<size_t>(32 + i)] = static_cast<uint8_t>(s);
        carry = s >> 8;
    }
    CHECK(!ed25519::verify(pub.data(), msg.data(), msg.size(), sig.data()));
    // All-zero and all-FF keys and signatures don't pass.
    std::vector<uint8_t> zeros(64, 0), ffs(64, 0xff);
    CHECK(!ed25519::verify(zeros.data(), msg.data(), msg.size(), zeros.data()));
    CHECK(!ed25519::verify(ffs.data(), msg.data(), msg.size(), ffs.data()));
}

AVEN_TEST(update_signature_ties_file_name_and_digest) {
    // Made by tools/release/update_key.py with a test key.
    const std::string key = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
    const std::string sig = "3c571dafbceaed4915ab245573ff95b1b5c0c9590eb68be3b73e2d743d18699a0962fdd7ced94ee2c4dccbdb280746abc2380ec6c0efe5639ef80d8888fed107\n";
    const std::string name = "aven-1.2.3-linux-x64.zip", sha(64, 'a');
    std::string digest;
    for (int i = 0; i < 32; ++i)
        digest += "ab";
    std::string error;
    CHECK(update::checkSignature(key, name, digest, sig, error));
    CHECK(!update::checkSignature(key, "aven-9.9.9-linux-x64.zip", digest, sig, error)); // another version
    CHECK(!update::checkSignature(key, name, sha, sig, error));                         // another file
    CHECK(error.find("isn't signed by") != std::string::npos);
    std::string otherKey = key;
    otherKey[0] = otherKey[0] == '0' ? '1' : '0';
    CHECK(!update::checkSignature(otherKey, name, digest, sig, error));
    CHECK(!update::checkSignature("", name, digest, sig, error) && error.find("no key") != std::string::npos);
    CHECK(!update::checkSignature(key, name, digest, "not hex", error) && error.find("damaged") != std::string::npos);
}
