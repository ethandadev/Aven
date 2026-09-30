#include "rynax/core/log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rynax {

namespace {

struct SinkEntry {
    int id;
    Log::Sink sink;
};

struct LogState {
    std::mutex mutex;
    // Held while sinks run, and by removeSink: once removeSink returns, that sink is never called
    // again, even by another thread that was logging at the time. Recursive: a sink may log.
    std::recursive_mutex calling;
    std::vector<SinkEntry> sinks;
    int nextId = 1;
    std::atomic<bool> echo{true};
};

LogState& state() {
    static LogState s = [] {
#if defined(_WIN32)
        // Messages are UTF-8 (names in any alphabet, emoji): a Windows console shows them as such.
        SetConsoleOutputCP(CP_UTF8);
#endif
        return LogState{};
    }();
    return s;
}

const char* levelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    }
    return "";
}

} // namespace

int Log::addSink(Sink sink) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    int id = s.nextId++;
    s.sinks.push_back({id, std::move(sink)});
    return id;
}

void Log::removeSink(int id) {
    auto& s = state();
    std::lock_guard wait(s.calling);
    std::lock_guard lock(s.mutex);
    std::erase_if(s.sinks, [id](const SinkEntry& e) { return e.id == id; });
}

void Log::setEchoToStdout(bool echo) {
    state().echo = echo;
}

void Log::write(const LogMessage& message) {
    auto& s = state();
    std::vector<SinkEntry> sinks;
    {
        std::lock_guard lock(s.mutex);
        sinks = s.sinks;
    }
    if (s.echo) {
        FILE* out = message.level >= LogLevel::Warning ? stderr : stdout;
        if (!message.file.empty())
            std::fprintf(out, "[%s] %s:%d: %s\n", levelTag(message.level), message.file.c_str(), message.line,
                         message.text.c_str());
        else
            std::fprintf(out, "[%s] %s\n", levelTag(message.level), message.text.c_str());
        std::fflush(out);
    }
    std::lock_guard calling(s.calling);
    for (auto& e : sinks) {
        {
            std::lock_guard lock(s.mutex); // removed since the copy was made (by an earlier sink, say)?
            if (std::none_of(s.sinks.begin(), s.sinks.end(), [&](const SinkEntry& live) { return live.id == e.id; }))
                continue;
        }
        e.sink(message);
    }
}

void Log::write(LogLevel level, std::string text, std::string file, int line) {
    write(LogMessage{level, std::move(text), std::move(file), line});
}

} // namespace rynax
