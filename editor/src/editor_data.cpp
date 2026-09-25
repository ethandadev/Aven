// Editor data files (editor/data/*.json): things contributors can extend without C++,
// like the Error Doctor's explanations and the words Ask Aven understands.

#include "editor.h"

#include "aven/core/fs.h"

#include <map>

namespace aven::editor {

stdfs::path editorDataDir() {
    std::error_code ec;
    // A build from source reads the source folder, so edits show up after a restart.
    stdfs::path source = stdfs::path(AVEN_SOURCE_DIR) / "editor" / "data";
    if (stdfs::exists(source, ec))
        return source;
    return fs::executableDir() / "data";
}

stdfs::path sourceDir() {
    std::error_code ec;
    stdfs::path source(AVEN_SOURCE_DIR);
    return stdfs::exists(source / "engine", ec) && stdfs::exists(source / "editor", ec) ? source : stdfs::path();
}

stdfs::path sdkDir() {
    std::error_code ec;
    stdfs::path source = stdfs::path(AVEN_SOURCE_DIR) / "sdk";
    if (stdfs::exists(source / "include" / "aven.h", ec))
        return source;
    return fs::executableDir() / "sdk";
}

const Json& editorData(const std::string& name) {
    struct Entry {
        stdfs::file_time_type time;
        Json json;
    };
    static std::map<std::string, Entry> cache;
    stdfs::path path = editorDataDir() / name;
    std::error_code ec;
    auto time = stdfs::last_write_time(path, ec);
    Entry& e = cache[name];
    if (ec || e.time != time || e.json.isNull()) {
        e.time = time;
        std::string error;
        auto text = fs::readText(path);
        e.json = text ? Json::parse(*text, &error) : Json::object();
        if (!error.empty()) {
            Log::warn(name, " couldn't be read: ", error);
            e.json = Json::object();
        }
    }
    return e.json;
}

} // namespace aven::editor
