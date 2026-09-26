#pragma once

// Code intelligence for the built-in code editor: suggestions (autocomplete), parameter hints,
// hover help, live problem checks, go to definition and an outline. It knows EasyScript and
// the C API of native modules, and uses a ProjectIndex of what the game contains (sound files,
// object names, tags...), so suggestions fit the project being edited.
//
// Positions are 0-based: `line` indexes the lines of the file, `col` is a byte offset in the line.

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace aven::script {

enum class SuggestionKind { Keyword, Function, Method, Property, Variable, Event, Snippet, File, Text, Component };

struct Suggestion {
    std::string label;  // what the list shows
    std::string insert; // replaces the typed part. "$0" marks the cursor, "\n\t" starts an indented line
    std::string detail; // short info on the right, like a signature
    std::string doc;    // a sentence about it
    SuggestionKind kind = SuggestionKind::Variable;
    int score = 0;
};

struct Diagnostic {
    int line = 0;   // 0-based
    int col = 0;    // first byte of the problem
    int endCol = 0; // one past the last byte
    std::string message;
    bool error = false; // errors stop the script; warnings are probably mistakes
};

struct SignatureHelp {
    std::string label;                          // e.g. play_sound(path, volume=1, pitch=1)
    std::vector<std::pair<int, int>> params;    // byte ranges of the parameters in label
    int active = 0;                             // the parameter being typed
    std::string doc;
};

struct OutlineItem {
    std::string name;
    std::string detail; // "def jump(power)" or "= 5"
    int line = 0;
    bool function = false;
};

enum class CodeKind { EasyScript, C };

// What scripts can use, and what the project contains.
struct ProjectIndex {
    struct Function {
        std::string name, signature;
        int minArgs = 0, maxArgs = -1; // -1 = any number
    };
    std::vector<Function> globals;                           // print(), spawn()...
    std::vector<std::string> globalValues;                   // pi, game
    std::vector<std::string> selfProperties;                 // x, velocity...
    std::vector<Function> selfMethods;                       // move(), say()...
    std::map<std::string, std::vector<Function>> typeMethods; // "list"/"text"/"dict"/"vec" -> methods
    std::vector<std::pair<std::string, std::string>> componentAliases;               // sprite -> SpriteRenderer
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> components; // name -> (field, type)
    std::vector<std::pair<std::string, std::string>> events; // on_update -> "on_update(dt)"
    std::vector<std::string> keyNames;                       // "space", "left", input actions...
    std::vector<std::string> colors, shapes, easings, tweenProperties;

    // Filled in by the editor from the project:
    std::vector<std::string> files;                      // project-relative paths, forward slashes
    std::vector<std::string> objectNames, tags;          // from the scenes
    std::vector<std::string> messages, gameValues, savedKeys, scriptFunctions; // from all scripts

    // C API (native modules): aven_* helpers with their prototypes.
    std::vector<Function> cFunctions;
    std::vector<std::string> cBehaviorFields; // on_start, on_update...

    std::unordered_map<std::string, std::string> docs; // one-line explanations (editor/data/api_docs.json)

    // Fills in everything the engine itself provides (not the project parts).
    void fillFromEngine();
    // Reads the C API helpers from aven.h's text.
    void readCApi(const std::string& header);
    // Adds names found in a script: messages, game values, saved keys and functions.
    void scanScript(const std::string& source);
    const Function* global(const std::string& name) const;
    std::string doc(const std::string& key) const;
};

class CodeIntel {
public:
    CodeIntel(const ProjectIndex& index, CodeKind kind) : index_(index), kind_(kind) {}

    // Suggestions at the cursor. `from` gets the column where the replaced text starts.
    // `manual` is true for Ctrl+Space (suggest even before anything is typed).
    std::vector<Suggestion> suggest(const std::vector<std::string>& lines, int line, int col, int& from, bool manual) const;
    // The call the cursor is inside, and which value is being typed.
    bool signature(const std::vector<std::string>& lines, int line, int col, SignatureHelp& out) const;
    // Help for the word at a position (empty if none). `from`/`to` get the word's columns.
    std::string hover(const std::vector<std::string>& lines, int line, int col, int& from, int& to) const;
    // Problems in the whole file: syntax errors, unknown names, missing files, misspelled keys...
    std::vector<Diagnostic> diagnose(const std::string& source) const;
    // Where the word at a position is defined in this file.
    bool definition(const std::vector<std::string>& lines, int line, int col, int& outLine, int& outCol) const;
    std::vector<OutlineItem> outline(const std::vector<std::string>& lines) const;

private:
    const ProjectIndex& index_;
    CodeKind kind_;
};

// How well `candidate` matches what was typed (higher is better, -1 = no match):
// prefix, then the first letters of words (kp -> key_pressed), then letters in order.
int matchScore(const std::string& candidate, const std::string& typed);

} // namespace aven::script
