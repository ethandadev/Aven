#pragma once

#include <exception>
#include <string>
#include <vector>

namespace aven::script {

// Every error a beginner can see goes through here, written in plain language
// with a hint when we can guess what they meant.
// The message lives in the error itself (not in std::runtime_error's storage), so copies made
// while the error is reported always carry their own text.
struct ScriptError : std::exception {
    std::string message;
    std::string file;
    int line = 0;
    std::string trace; // "in on_update (line 12)" lines, most recent first

    explicit ScriptError(std::string message_, int line_ = 0) : message(std::move(message_)), line(line_) {}
    const char* what() const noexcept override { return message.c_str(); }
};

[[noreturn]] void raise(const std::string& message);

// "Did you mean 'speed'?" suggestions based on edit distance.
std::string closestMatch(const std::string& word, const std::vector<std::string>& candidates);
std::string didYouMean(const std::string& word, const std::vector<std::string>& candidates);

} // namespace aven::script
