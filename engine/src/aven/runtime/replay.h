#pragma once

#include "aven/core/json.h"
#include "aven/math/math.h"

#include <filesystem>
#include <string>
#include <vector>

namespace aven {

class Game;
class Input;

// Bug replay: everything needed to play a game session again exactly. A game started with the
// same scene and random seed, fed the same inputs and frame times, does the same thing.
struct ReplayFrame {
    float dt = 0;
    Vec2 screen;
    Json input; // Input::snapshot()
};

struct ReplayEvent {
    int frame = 0;
    std::string kind; // "error", "warning", "edit" (changed while paused), "info"
    std::string text;
    std::string file;
    int line = 0;
};

struct Replay {
    uint32_t seed = 0;
    std::string scenePath;
    Json startScene; // the scene as it was when Play was pressed
    std::string project;
    std::vector<ReplayFrame> frames;
    std::vector<ReplayEvent> events;
    bool cleanExit = false; // false: the editor closed (or crashed) while recording
    bool empty() const { return frames.empty(); }
    float duration() const;
    // First frame of the last `seconds` of the recording.
    int frameAtSecondsBeforeEnd(float seconds) const;
    bool edited() const; // changed while paused, so a replay may not match exactly

    // Files are JSON lines: a header, then one line per frame or event, so recordings can be
    // appended to as they happen and survive a crash.
    bool load(const std::filesystem::path& file, std::string* error = nullptr);
    bool save(const std::filesystem::path& file) const;
};

// Writes a recording to disk as it grows (flushes every second or so).
class ReplayRecorder {
public:
    void begin(const std::filesystem::path& file, const Replay& header);
    void addFrame(float dt, Vec2 screen, const Input& input);
    void addEvent(const std::string& kind, const std::string& text, const std::string& file = "", int line = 0);
    void end(); // marks a clean exit
    bool recording() const { return recording_; }
    const Replay& replay() const { return replay_; }
    static constexpr size_t kMaxFrames = 60 * 60 * 30; // 30 minutes

private:
    void flush();
    std::filesystem::path file_;
    Replay replay_;
    std::string pending_;
    bool recording_ = false;
};

// Plays a recording back into a game, one frame at a time.
class ReplayPlayer {
public:
    // Starts `game` from the recording's scene with its seed.
    bool start(Game& game, Input& input, const Replay& replay);
    // Advances one recorded frame; false when the recording is over.
    bool step();
    int frame() const { return frame_; }
    int frameCount() const { return replay_ ? static_cast<int>(replay_->frames.size()) : 0; }
    float time() const { return time_; }

private:
    Game* game_ = nullptr;
    Input* input_ = nullptr;
    const Replay* replay_ = nullptr;
    int frame_ = 0;
    float time_ = 0;
};

} // namespace aven
