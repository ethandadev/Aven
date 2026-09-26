#pragma once

#include "aven/script/intel.h"

#include <imgui.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace aven::editor {

// Syntax colors for the code editor (chosen in Preferences).
struct CodePalette {
    std::string name;
    ImU32 background, text, keyword, string, number, comment, function, builtin, self;
    ImU32 lineNumber, currentLineNumber, currentLine, selection, cursor, gutterLine;
    static const std::vector<CodePalette>& presets();
    static const CodePalette& find(const std::string& name);
};

// Which language a code view colors (the Code ladder shows other engines' code).
enum class CodeLanguage { EasyScript, CSharp, GDScript, Luau, Cpp };

// A code editor built for EasyScript (and C for native modules): syntax colors, line numbers,
// auto-indent, bracket pairing and matching, indent guides, line moving, zoom, and with a
// CodeIntel attached: suggestions by context, parameter hints, hover help, live problem
// underlines and go to definition.
class CodeEditor {
public:
    struct Completion {
        std::string word;
        std::string detail; // shown next to the word, e.g. the signature
    };

    CodeEditor();
    void setText(const std::string& text);
    std::string text() const;
    // Draws the editor filling `size`. Returns true when the text changed this frame.
    bool draw(const char* id, ImVec2 size);
    void setError(int line, const std::string& message); // line 0 clears
    void gotoLine(int line);
    void openFind(bool replace); // Ctrl+F / Ctrl+H
    void openGoto();             // Ctrl+G
    bool readOnly = false;
    CodeLanguage language = CodeLanguage::EasyScript;
    const script::CodeIntel* intel = nullptr; // null: simple word completion only
    static CodePalette palette; // shared by every code view
    static float zoom;          // text size, shared by every code view (Ctrl+wheel, Ctrl+= and Ctrl+-)
    const std::vector<script::Diagnostic>& problems() const { return diags_; }
    std::vector<script::OutlineItem> outline() const;
    void gotoPosition(int line, int col); // 0-based
    void recheck() { diagTimer_ = 0.01f; }
    ImFont* font = nullptr;
    std::vector<Completion> completions;
    std::unordered_set<std::string> highlightWords; // functions to color (engine API)

private:
    struct Pos {
        int line = 0;
        int col = 0;
        bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
        bool operator!=(const Pos& o) const { return !(*this == o); }
        bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
    };
    struct Snapshot {
        std::vector<std::string> lines;
        Pos cursor;
    };
    // Code intelligence state.
    std::vector<script::Suggestion> suggestions_;
    int suggestFrom_ = 0, suggestScroll_ = 0;
    ImVec2 popupMin_{0, 0}, popupMax_{0, 0}; // last frame's suggestion list, for the mouse
    script::SignatureHelp sig_;
    bool sigOpen_ = false;
    std::vector<script::Diagnostic> diags_;
    float diagTimer_ = -1;
    ImVec2 lastMouse_{0, 0};
    float mouseStill_ = 0;
    float fontSize_ = 16;
    bool problemsOpen_ = false;
    Pos sigCursor_{-1, -1}; // where parameter hints were last worked out
    int pendingScrollLine_ = -1;
    ImFont* uiFont_ = nullptr; // the editor's normal font, for help text next to the code

    std::vector<std::string> lines_{""};
    Pos cursor_, anchor_;
    bool focused_ = false;
    bool dragging_ = false;
    std::vector<Snapshot> undo_, redo_;
    bool groupTyping_ = false;
    int errorLine_ = 0;
    std::string errorMessage_;
    int gotoLine_ = -1;
    bool scrollToCursor_ = false;
    float blink_ = 0;
    bool completionOpen_ = false;
    int completionIndex_ = 0;
    std::vector<const Completion*> matches_;
    float charWidth_ = 8, lineHeight_ = 16;
    bool findOpen_ = false, replaceOpen_ = false, gotoOpen_ = false, findFocus_ = false, findCase_ = false;
    std::string findText_, replaceText_, gotoText_;
    bool drawFindBar(); // returns true when the text changed (replace)
    bool findNext(bool backwards);
    bool matchesAt(const std::string& line, size_t col) const;

    bool hasSelection() const { return cursor_ != anchor_; }
    Pos selStart() const { return cursor_ < anchor_ ? cursor_ : anchor_; }
    Pos selEnd() const { return cursor_ < anchor_ ? anchor_ : cursor_; }
    std::string selectedText() const;
    void deleteSelection();
    void insert(const std::string& text);
    void pushUndo(bool typing = false);
    void undoStep();
    void redoStep();
    void moveCursor(Pos p, bool select);
    Pos clamp(Pos p) const;
    int prevChar(int line, int col) const;
    int nextChar(int line, int col) const;
    int wordStart(int line, int col) const;
    int wordEnd(int line, int col) const;
    void handleKeys(bool& changed);
    void handleTyping(bool& changed);
    void newline();
    void indentSelection(bool outdent);
    void toggleComment();
    void updateCompletions();
    void openSuggestions(bool manual);
    void acceptSuggestion();
    void updateSignature();
    void moveLines(int dir);
    void duplicateLines(bool up);
    void deleteLines();
    void goToDefinition(Pos at);
    bool matchingBracket(Pos& a, Pos& b) const;
    void drawSuggestions(ImVec2 cursorScreen);
    void drawSignature(ImVec2 cursorScreen);
    void drawStatusBar();
    std::string currentWord() const;
    float columnX(int line, int col) const;
    int columnAt(int line, float x) const;
    void drawLine(ImDrawList* dl, int index, ImVec2 pos, bool& inTripleString) const;
};

} // namespace aven::editor
