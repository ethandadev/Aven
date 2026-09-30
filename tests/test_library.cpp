#include "test_framework.h"

#include "rynax/core/fs.h"
#include "rynax/core/json.h"
#include "rynax/render/model.h"

#include <filesystem>
#include <set>

using namespace rynax;

// The editor's Asset Library: every item's file exists, ids are unique, and every 3D model loads.
RYNAX_TEST(asset_library_is_complete) {
    namespace stdfs = std::filesystem;
    stdfs::path root = stdfs::path(RYNAX_SOURCE_DIR) / "editor" / "data" / "library";
    auto text = fs::readText(root / "library.json");
    CHECK(text.has_value());
    Json lib = Json::parse(*text);
    std::set<std::string> ids, dests, kinds;
    int models = 0;
    for (auto& item : lib["items"].elements()) {
        std::string id = item["id"].asString(""), file = item["file"].asString("");
        CHECK(ids.insert(id).second);
        CHECK(dests.insert(item["dest"].asString("")).second); // no two items overwrite each other in a project
        kinds.insert(item["kind"].asString(""));
        if (!stdfs::exists(root / file))
            std::printf("  missing %s\n", file.c_str());
        CHECK(stdfs::exists(root / file));
        if (item["kind"].asString("") == "model") {
            Model m;
            std::string error;
            bool ok = m.load(nullptr, root / file, &error);
            if (!ok)
                std::printf("  %s: %s\n", file.c_str(), error.c_str());
            CHECK(ok);
            CHECK(stdfs::exists(root / item["thumb"].asString("-")));
            ++models;
        }
    }
    CHECK(ids.size() >= 60);
    CHECK(models >= 15);
    CHECK_EQ(kinds.size(), size_t(5)); // sprites, animations, backgrounds, textures, models
}
