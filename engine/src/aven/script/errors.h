#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace aven::script {

// Every error a beginner can see goes through here, written in plain language
// with a hint when we can guess what they meant.
struct ScriptError : std::runtime_error {
    std::string file;
    int line = 0;
    std::string trace; // "in on_update (line 12)" lines, most recent first

    explicit ScriptError(const std::string& message, int line_ = 0) : std::runtime_error(message), line(line_) {}
};

[[noreturn]] void raise(const std::string& message);

// "Did you mean 'speed'?" suggestions based on edit distance.
std::string closestMatch(const std::string& word, const std::vector<std::string>& candidates);
std::string didYouMean(const std::string& word, const std::vector<std::string>& candidates);

} // namespace aven::script
