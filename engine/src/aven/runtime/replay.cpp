#include "aven/runtime/replay.h"

#include "aven/core/fs.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/scene/scene.h"

#include <fstream>
#include <sstream>

namespace aven {

namespace {

Json vec2(Vec2 v) {
    Json a = Json::array();
    a.push(Json(v.x));
    a.push(Json(v.y));
    return a;
}

std::string headerLine(const Replay& r) {
    Json h = Json::object();
    h["aven"] = "replay";
    h["version"] = 1;
    h["seed"] = static_cast<double>(r.seed);
    h["scene"] = r.scenePath;
    h["project"] = r.project;
    h["start"] = r.startScene;
    return h.dump() + "\n";
}

std::string frameLine(const ReplayFrame& f) {
    Json j = Json::object();
    j["dt"] = f.dt;
    j["sz"] = vec2(f.screen);
    j["in"] = f.input;
    return j.dump() + "\n";
}

std::string eventLine(const ReplayEvent& e) {
    Json j = Json::object();
    j["event"] = e.kind;
    j["frame"] = e.frame;
    j["text"] = e.text;
    if (!e.file.empty()) {
        j["file"] = e.file;
        j["line"] = e.line;
    }
    return j.dump() + "\n";
}

} // namespace

float Replay::duration() const {
    float t = 0;
    for (auto& f : frames)
        t += f.dt;
    return t;
}

int Replay::frameAtSecondsBeforeEnd(float seconds) const {
    float t = 0;
    for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i) {
        t += frames[static_cast<size_t>(i)].dt;
        if (t >= seconds)
            return i;
    }
    return 0;
}

bool Replay::edited() const {
    for (auto& e : events)
        if (e.kind == "edit")
            return true;
    return false;
}

bool Replay::load(const std::filesystem::path& file, std::string* error) {
    *this = Replay{};
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "Can't open " + file.string();
        return false;
    }
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        std::string parseError;
        Json j = Json::parse(line, &parseError);
        if (!parseError.empty())
            break; // a crash can leave a half-written last line
        if (!header) {
            if (j["aven"].asString("") != "replay") {
                if (error)
                    *error = "This isn't an Aven replay.";
                return false;
            }
            seed = static_cast<uint32_t>(j["seed"].asNumber());
            scenePath = j["scene"].asString("");
            project = j["project"].asString("");
            startScene = j["start"];
            header = true;
        } else if (j.contains("dt")) {
            ReplayFrame f;
            f.dt = j["dt"].asFloat();
            f.screen = {j["sz"][0].asFloat(), j["sz"][1].asFloat()};
            f.input = j["in"];
            frames.push_back(std::move(f));
        } else if (j.contains("event")) {
            events.push_back({j["frame"].asInt(), j["event"].asString(""), j["text"].asString(""), j["file"].asString(""),
                              j["line"].asInt()});
        } else if (j.contains("end")) {
            cleanExit = true;
        }
    }
    if (!header && error)
        *error = "The replay file is empty.";
    return header;
}

bool Replay::save(const std::filesystem::path& file) const {
    std::string out = headerLine(*this);
    size_t e = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        while (e < events.size() && events[e].frame <= static_cast<int>(i))
            out += eventLine(events[e++]);
        out += frameLine(frames[i]);
    }
    for (; e < events.size(); ++e)
        out += eventLine(events[e]);
    if (cleanExit)
        out += "{\"end\": true}\n";
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return fs::writeText(file, out);
}

// ---------------------------------------------------------------- recorder

void ReplayRecorder::begin(const std::filesystem::path& file, const Replay& header) {
    file_ = file;
    replay_ = header;
    replay_.frames.clear();
    replay_.events.clear();
    replay_.cleanExit = false;
    recording_ = true;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream(file_, std::ios::trunc) << headerLine(replay_);
    pending_.clear();
}

void ReplayRecorder::addFrame(float dt, Vec2 screen, const Input& input) {
    if (!recording_ || replay_.frames.size() >= kMaxFrames)
        return;
    replay_.frames.push_back({dt, screen, input.snapshot()});
    pending_ += frameLine(replay_.frames.back());
    if (replay_.frames.size() % 60 == 0)
        flush();
}

void ReplayRecorder::addEvent(const std::string& kind, const std::string& text, const std::string& file, int line) {
    if (!recording_)
        return;
    replay_.events.push_back({static_cast<int>(replay_.frames.size()), kind, text, file, line});
    pending_ += eventLine(replay_.events.back());
    flush(); // errors are exactly what a crash report needs
}

void ReplayRecorder::end() {
    if (!recording_)
        return;
    replay_.cleanExit = true;
    pending_ += "{\"end\": true}\n";
    flush();
    recording_ = false;
}

void ReplayRecorder::flush() {
    if (pending_.empty())
        return;
    std::ofstream(file_, std::ios::app) << pending_;
    pending_.clear();
}

// ---------------------------------------------------------------- player

bool ReplayPlayer::start(Game& game, Input& input, const Replay& replay) {
    game_ = &game;
    input_ = &input;
    replay_ = &replay;
    frame_ = 0;
    time_ = 0;
    auto scene = std::make_unique<Scene>();
    std::string error;
    if (!scene->load(replay.startScene, &error))
        return false;
    input.reset();
    game.setRandomSeed(replay.seed);
    game.start(std::move(scene), replay.scenePath);
    return true;
}

bool ReplayPlayer::step() {
    if (!replay_ || frame_ >= static_cast<int>(replay_->frames.size()))
        return false;
    const ReplayFrame& f = replay_->frames[static_cast<size_t>(frame_)];
    input_->restore(f.input);
    game_->setScreenSize(f.screen);
    game_->update(f.dt);
    time_ += f.dt;
    ++frame_;
    return true;
}

} // namespace aven
