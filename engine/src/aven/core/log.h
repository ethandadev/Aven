#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <utility>

namespace aven {

enum class LogLevel { Trace, Info, Warning, Error };

struct LogMessage {
    LogLevel level = LogLevel::Info;
    std::string text;
    std::string file; // source file the message refers to (e.g. a script), may be empty
    int line = 0;     // 1-based line in `file`, 0 if unknown
};

template <class... Args> std::string concat(Args&&... args) {
    std::ostringstream ss;
    (ss << ... << std::forward<Args>(args));
    return ss.str();
}

class Log {
public:
    using Sink = std::function<void(const LogMessage&)>;

    static int addSink(Sink sink);
    static void removeSink(int id);
    static void setEchoToStdout(bool echo);

    static void write(const LogMessage& message);
    static void write(LogLevel level, std::string text, std::string file = {}, int line = 0);

    template <class... A> static void trace(A&&... a) { write(LogLevel::Trace, concat(std::forward<A>(a)...)); }
    template <class... A> static void info(A&&... a) { write(LogLevel::Info, concat(std::forward<A>(a)...)); }
    template <class... A> static void warn(A&&... a) { write(LogLevel::Warning, concat(std::forward<A>(a)...)); }
    template <class... A> static void error(A&&... a) { write(LogLevel::Error, concat(std::forward<A>(a)...)); }
};

} // namespace aven
