#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace aven::editor {

// What a script does, read from its code: used by Explain and the Error doctor.
struct ScriptFacts {
    bool parsed = false;
    std::string error;                                  // syntax error, if any
    int errorLine = 0;
    std::vector<std::pair<std::string, std::vector<std::string>>> handlers; // "When the game starts" -> sentences
    std::set<std::string> sends, receives;              // broadcast messages
    std::set<std::string> findsNames;                   // find("Player")
    std::set<std::string> tags;                         // find_all("enemy"), other.tag == "coin", is_touching("coin")
    std::set<std::string> gameVarsWritten, gameVarsRead;
    std::set<std::string> keys;                         // keys and input actions used
    std::set<std::string> spawns, scenes, sounds;        // files it uses
    std::set<std::string> functions;                    // functions it defines
    std::map<std::string, int> fileLines;               // file path -> first line using it (for the doctor)
    bool restartsScene = false, usesPhysics = false, readsInput = false;
};

// Reads EasyScript source (blocks: pass the compiled code, and `blocksJson` for friendlier sentences).
ScriptFacts analyzeScript(const std::string& source, const std::string& blocksJson = "");

} // namespace aven::editor
