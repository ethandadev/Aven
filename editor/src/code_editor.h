#pragma once

#include <imgui.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace aven::editor {

// A code editor built for EasyScript: syntax colors, line numbers, auto-indent,
// bracket pairing, error markers and autocomplete.
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
    bool readOnly = false;
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
    std::string currentWord() const;
    float columnX(int line, int col) const;
    int columnAt(int line, float x) const;
    void drawLine(ImDrawList* dl, int index, ImVec2 pos, bool& inTripleString) const;
};

} // namespace aven::editor
