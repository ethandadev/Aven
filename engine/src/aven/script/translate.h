#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aven::script {

// The Code ladder: EasyScript translated into the languages of other engines, so a
// beginner can see what their script looks like in Unity, Godot, Roblox or Unreal.
// The output is meant for reading and learning. It follows each engine's usual style
// and is close to working code, and `notes` says where the engines differ.
enum class TargetLanguage { Unity, Godot, Roblox, Unreal };

struct TranslateOptions {
    std::string className = "MyScript"; // class / file name to use
    bool is3D = false;
};

struct Translation {
    bool ok = false;
    std::string error; // syntax error in the EasyScript
    int errorLine = 0;
    std::string code;
    std::vector<std::string> notes; // things that work differently in the other engine
};

Translation translate(std::string_view source, TargetLanguage language, const TranslateOptions& options = {});

const char* languageName(TargetLanguage language);   // "C#"
const char* languageEngine(TargetLanguage language); // "Unity"
const char* languageExtension(TargetLanguage language); // ".cs"
// A class name from a file name: "scripts/slime_enemy.es" -> "SlimeEnemy".
std::string classNameFor(const std::string& path);

} // namespace aven::script
