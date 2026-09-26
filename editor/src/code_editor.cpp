#include "code_editor.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace aven::editor {

namespace {


bool isKeyword(const std::string& w, CodeLanguage lang = CodeLanguage::EasyScript) {
    static const std::unordered_set<std::string> easy = {"def", "if", "elif", "else", "while", "for", "in", "return", "break",
                                                          "continue", "pass", "and", "or", "not", "True", "False", "None",
                                                          "global", "true", "false", "null"};
    static const std::unordered_set<std::string> cs = {
        "using", "public", "private", "protected", "class", "static", "void", "float", "int", "bool", "string", "var", "new",
        "if", "else", "while", "for", "foreach", "in", "return", "break", "continue", "true", "false", "null", "object",
        "yield", "override", "virtual", "const", "auto", "struct", "nullptr", "this", "int32", "typedef", "namespace",
        "IEnumerator", "sizeof", "enum", "operator", "template", "typename"};
    static const std::unordered_set<std::string> gd = {"extends", "func", "var", "if", "elif", "else", "while", "for", "in",
                                                        "return", "break", "continue", "pass", "and", "or", "not", "true",
                                                        "false", "null", "await", "signal", "const", "class_name", "is"};
    static const std::unordered_set<std::string> lua = {"local", "function", "if", "then", "elseif", "else", "end", "while",
                                                         "do", "for", "in", "return", "break", "continue", "and", "or", "not",
                                                         "true", "false", "nil", "repeat", "until"};
    switch (lang) {
    case CodeLanguage::CSharp:
    case CodeLanguage::Cpp: return cs.count(w) > 0;
    case CodeLanguage::GDScript: return gd.count(w) > 0;
    case CodeLanguage::Luau: return lua.count(w) > 0;
    default: return easy.count(w) > 0;
    }
}

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

} // namespace

const std::vector<CodePalette>& CodePalette::presets() {
    // background, text, keyword, string, number, comment, function, builtin, self,
    // line number, current line number, current line, selection, cursor, gutter line
    static const std::vector<CodePalette> list = {
        {"Aven Dark", IM_COL32(30, 33, 39, 255), IM_COL32(220, 223, 228, 255), IM_COL32(198, 120, 221, 255),
         IM_COL32(152, 195, 121, 255), IM_COL32(209, 154, 102, 255), IM_COL32(110, 118, 129, 255),
         IM_COL32(97, 175, 239, 255), IM_COL32(229, 192, 123, 255), IM_COL32(224, 108, 117, 255),
         IM_COL32(95, 102, 115, 255), IM_COL32(200, 205, 215, 255), IM_COL32(255, 255, 255, 10),
         IM_COL32(80, 120, 200, 110), IM_COL32(230, 230, 240, 255), IM_COL32(60, 65, 75, 255)},
        {"Paper", IM_COL32(250, 250, 247, 255), IM_COL32(40, 42, 48, 255), IM_COL32(166, 38, 164, 255),
         IM_COL32(80, 161, 79, 255), IM_COL32(152, 104, 1, 255), IM_COL32(160, 161, 167, 255),
         IM_COL32(64, 120, 242, 255), IM_COL32(193, 132, 1, 255), IM_COL32(228, 86, 73, 255),
         IM_COL32(170, 172, 178, 255), IM_COL32(60, 62, 70, 255), IM_COL32(0, 0, 0, 12),
         IM_COL32(100, 150, 240, 80), IM_COL32(40, 42, 48, 255), IM_COL32(220, 220, 222, 255)},
        {"Midnight Neon", IM_COL32(13, 15, 26, 255), IM_COL32(214, 222, 255, 255), IM_COL32(255, 92, 205, 255),
         IM_COL32(126, 250, 175, 255), IM_COL32(255, 196, 92, 255), IM_COL32(98, 108, 150, 255),
         IM_COL32(92, 200, 255, 255), IM_COL32(255, 238, 120, 255), IM_COL32(255, 120, 120, 255),
         IM_COL32(70, 78, 110, 255), IM_COL32(190, 200, 240, 255), IM_COL32(120, 140, 255, 16),
         IM_COL32(120, 90, 255, 100), IM_COL32(255, 92, 205, 255), IM_COL32(40, 44, 70, 255)},
        {"Forest", IM_COL32(24, 32, 27, 255), IM_COL32(218, 228, 214, 255), IM_COL32(242, 181, 94, 255),
         IM_COL32(170, 214, 128, 255), IM_COL32(236, 140, 106, 255), IM_COL32(112, 132, 112, 255),
         IM_COL32(128, 204, 188, 255), IM_COL32(226, 214, 132, 255), IM_COL32(230, 120, 110, 255),
         IM_COL32(88, 108, 92, 255), IM_COL32(200, 214, 196, 255), IM_COL32(255, 255, 255, 9),
         IM_COL32(90, 150, 110, 100), IM_COL32(220, 230, 210, 255), IM_COL32(48, 60, 52, 255)},
        {"Monokai", IM_COL32(39, 40, 34, 255), IM_COL32(248, 248, 242, 255), IM_COL32(249, 38, 114, 255),
         IM_COL32(230, 219, 116, 255), IM_COL32(174, 129, 255, 255), IM_COL32(117, 113, 94, 255),
         IM_COL32(166, 226, 46, 255), IM_COL32(102, 217, 239, 255), IM_COL32(253, 151, 31, 255),
         IM_COL32(117, 113, 94, 255), IM_COL32(220, 220, 210, 255), IM_COL32(255, 255, 255, 10),
         IM_COL32(73, 72, 62, 255), IM_COL32(248, 248, 240, 255), IM_COL32(60, 61, 54, 255)},
        {"High Contrast", IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255), IM_COL32(255, 220, 0, 255),
         IM_COL32(0, 255, 140, 255), IM_COL32(0, 220, 255, 255), IM_COL32(170, 170, 170, 255),
         IM_COL32(120, 190, 255, 255), IM_COL32(255, 170, 60, 255), IM_COL32(255, 110, 110, 255),
         IM_COL32(150, 150, 150, 255), IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 22),
         IM_COL32(0, 110, 255, 150), IM_COL32(255, 255, 0, 255), IM_COL32(90, 90, 90, 255)},
    };
    return list;
}

const CodePalette& CodePalette::find(const std::string& name) {
    for (auto& p : presets())
        if (p.name == name)
            return p;
    return presets().front();
}

CodePalette CodeEditor::palette = CodePalette::presets().front();
float CodeEditor::zoom = 1.0f;

CodeEditor::CodeEditor() = default;

void CodeEditor::setText(const std::string& text) {
    lines_.clear();
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string clean;
        for (char c : line) {
            if (c == '\r')
                continue;
            if (c == '\t')
                clean += "    ";
            else
                clean += c;
        }
        lines_.push_back(clean);
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    if (lines_.empty())
        lines_.push_back("");
    cursor_ = anchor_ = {};
    undo_.clear();
    redo_.clear();
    diags_.clear();
    diagTimer_ = 0.01f; // check it straight away
    completionOpen_ = sigOpen_ = false;
}

std::string CodeEditor::text() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i)
            out += '\n';
        out += lines_[i];
    }
    if (out.empty() || out.back() != '\n')
        out += '\n';
    return out;
}

void CodeEditor::setError(int line, const std::string& message) {
    errorLine_ = line;
    errorMessage_ = message;
}

void CodeEditor::gotoLine(int line) {
    gotoLine_ = line;
}

CodeEditor::Pos CodeEditor::clamp(Pos p) const {
    p.line = std::clamp(p.line, 0, static_cast<int>(lines_.size()) - 1);
    p.col = std::clamp(p.col, 0, static_cast<int>(lines_[static_cast<size_t>(p.line)].size()));
    return p;
}

int CodeEditor::prevChar(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    if (col <= 0)
        return 0;
    --col;
    while (col > 0 && isContinuation(s[static_cast<size_t>(col)]))
        --col;
    return col;
}

int CodeEditor::nextChar(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    if (col >= static_cast<int>(s.size()))
        return static_cast<int>(s.size());
    ++col;
    while (col < static_cast<int>(s.size()) && isContinuation(s[static_cast<size_t>(col)]))
        ++col;
    return col;
}

int CodeEditor::wordStart(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    while (col > 0 && isWordChar(s[static_cast<size_t>(col - 1)]))
        --col;
    return col;
}

int CodeEditor::wordEnd(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    while (col < static_cast<int>(s.size()) && isWordChar(s[static_cast<size_t>(col)]))
        ++col;
    return col;
}

std::string CodeEditor::selectedText() const {
    Pos a = selStart(), b = selEnd();
    std::string out;
    for (int l = a.line; l <= b.line; ++l) {
        const std::string& s = lines_[static_cast<size_t>(l)];
        int from = l == a.line ? a.col : 0;
        int to = l == b.line ? b.col : static_cast<int>(s.size());
        out += s.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
        if (l != b.line)
            out += '\n';
    }
    return out;
}

void CodeEditor::pushUndo(bool typing) {
    if (typing && groupTyping_ && !undo_.empty())
        return;
    undo_.push_back({lines_, cursor_});
    if (undo_.size() > 300)
        undo_.erase(undo_.begin());
    redo_.clear();
    groupTyping_ = typing;
}

void CodeEditor::undoStep() {
    if (undo_.empty())
        return;
    redo_.push_back({lines_, cursor_});
    lines_ = undo_.back().lines;
    cursor_ = anchor_ = clamp(undo_.back().cursor);
    undo_.pop_back();
    groupTyping_ = false;
}

void CodeEditor::redoStep() {
    if (redo_.empty())
        return;
    undo_.push_back({lines_, cursor_});
    lines_ = redo_.back().lines;
    cursor_ = anchor_ = clamp(redo_.back().cursor);
    redo_.pop_back();
    groupTyping_ = false;
}

void CodeEditor::deleteSelection() {
    if (!hasSelection())
        return;
    Pos a = selStart(), b = selEnd();
    std::string tail = lines_[static_cast<size_t>(b.line)].substr(static_cast<size_t>(b.col));
    lines_[static_cast<size_t>(a.line)] = lines_[static_cast<size_t>(a.line)].substr(0, static_cast<size_t>(a.col)) + tail;
    lines_.erase(lines_.begin() + a.line + 1, lines_.begin() + b.line + 1);
    cursor_ = anchor_ = a;
}

void CodeEditor::insert(const std::string& text) {
    deleteSelection();
    std::string& line = lines_[static_cast<size_t>(cursor_.line)];
    std::string tail = line.substr(static_cast<size_t>(cursor_.col));
    line = line.substr(0, static_cast<size_t>(cursor_.col));
    size_t start = 0;
    int l = cursor_.line;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string part = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string clean;
        for (char c : part)
            if (c == '\t')
                clean += "    ";
            else if (c != '\r')
                clean += c;
        lines_[static_cast<size_t>(l)] += clean;
        if (nl == std::string::npos)
            break;
        lines_.insert(lines_.begin() + l + 1, "");
        ++l;
        start = nl + 1;
    }
    cursor_.line = l;
    cursor_.col = static_cast<int>(lines_[static_cast<size_t>(l)].size());
    lines_[static_cast<size_t>(l)] += tail;
    anchor_ = cursor_;
    scrollToCursor_ = true;
}

void CodeEditor::moveCursor(Pos p, bool select) {
    cursor_ = clamp(p);
    if (!select)
        anchor_ = cursor_;
    scrollToCursor_ = true;
    blink_ = 0;
    groupTyping_ = false;
}

void CodeEditor::newline() {
    const std::string& line = lines_[static_cast<size_t>(cursor_.line)];
    int indent = 0;
    while (indent < static_cast<int>(line.size()) && line[static_cast<size_t>(indent)] == ' ')
        ++indent;
    std::string before = line.substr(0, static_cast<size_t>(cursor_.col));
    size_t last = before.find_last_not_of(' ');
    // A line ending in ':' starts a block: indent the next line automatically.
    if (last != std::string::npos && before[last] == ':')
        indent += 4;
    std::string trimmed = before.substr(before.find_first_not_of(' ') == std::string::npos ? before.size() : before.find_first_not_of(' '));
    if (trimmed == "return" || trimmed.rfind("return ", 0) == 0 || trimmed == "pass" || trimmed == "break" ||
        trimmed == "continue")
        indent = std::max(0, indent - 4);
    insert("\n" + std::string(static_cast<size_t>(std::min(indent, static_cast<int>(before.size()) + 4)), ' '));
}

void CodeEditor::indentSelection(bool outdent) {
    Pos a = selStart(), b = selEnd();
    int last = b.col == 0 && b.line > a.line ? b.line - 1 : b.line;
    for (int l = a.line; l <= last; ++l) {
        std::string& s = lines_[static_cast<size_t>(l)];
        if (outdent) {
            int n = 0;
            while (n < 4 && n < static_cast<int>(s.size()) && s[static_cast<size_t>(n)] == ' ')
                ++n;
            s.erase(0, static_cast<size_t>(n));
            if (l == cursor_.line)
                cursor_.col = std::max(0, cursor_.col - n);
            if (l == anchor_.line)
                anchor_.col = std::max(0, anchor_.col - n);
        } else {
            s.insert(0, "    ");
            if (l == cursor_.line)
                cursor_.col += 4;
            if (l == anchor_.line)
                anchor_.col += 4;
        }
    }
}

void CodeEditor::toggleComment() {
    Pos a = selStart(), b = selEnd();
    bool allCommented = true;
    for (int l = a.line; l <= b.line; ++l) {
        const std::string& s = lines_[static_cast<size_t>(l)];
        size_t first = s.find_first_not_of(' ');
        if (first != std::string::npos && s[first] != '#')
            allCommented = false;
    }
    for (int l = a.line; l <= b.line; ++l) {
        std::string& s = lines_[static_cast<size_t>(l)];
        size_t first = s.find_first_not_of(' ');
        if (first == std::string::npos)
            continue;
        if (allCommented) {
            s.erase(first, s.compare(first, 2, "# ") == 0 ? 2 : 1);
        } else {
            s.insert(first, "# ");
        }
    }
    cursor_ = clamp(cursor_);
    anchor_ = clamp(anchor_);
}

std::string CodeEditor::currentWord() const {
    int start = wordStart(cursor_.line, cursor_.col);
    return lines_[static_cast<size_t>(cursor_.line)].substr(static_cast<size_t>(start),
                                                            static_cast<size_t>(cursor_.col - start));
}

void CodeEditor::updateCompletions() {
    if (intel) {
        openSuggestions(false);
        return;
    }
    matches_.clear();
    std::string word = currentWord();
    if (word.size() < 2) {
        completionOpen_ = false;
        return;
    }
    const std::string& line = lines_[static_cast<size_t>(cursor_.line)];
    // Don't complete inside comments or strings.
    int quotes = 0;
    for (int i = 0; i < cursor_.col; ++i) {
        char c = line[static_cast<size_t>(i)];
        if (c == '#' && quotes % 2 == 0)
            return;
        if (c == '"' || c == '\'')
            ++quotes;
    }
    if (quotes % 2)
        return;
    for (auto& c : completions)
        if (c.word.size() > word.size() && c.word.compare(0, word.size(), word) == 0)
            matches_.push_back(&c);
    // Words already in the file (variables, functions) complete too.
    std::sort(matches_.begin(), matches_.end(), [](auto* a, auto* b) { return a->word < b->word; });
    if (matches_.size() > 12)
        matches_.resize(12);
    completionOpen_ = !matches_.empty();
    completionIndex_ = std::clamp(completionIndex_, 0, std::max(0, static_cast<int>(matches_.size()) - 1));
}

void CodeEditor::handleKeys(bool& changed) {
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl || io.KeySuper, shift = io.KeyShift;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };

    int count = static_cast<int>(intel ? suggestions_.size() : matches_.size());
    if (completionOpen_ && count > 0) {
        if (pressed(ImGuiKey_DownArrow)) {
            completionIndex_ = (completionIndex_ + 1) % count;
            return;
        }
        if (pressed(ImGuiKey_UpArrow)) {
            completionIndex_ = (completionIndex_ + count - 1) % count;
            return;
        }
        if (pressed(ImGuiKey_PageDown)) {
            completionIndex_ = std::min(count - 1, completionIndex_ + 9);
            return;
        }
        if (pressed(ImGuiKey_PageUp)) {
            completionIndex_ = std::max(0, completionIndex_ - 9);
            return;
        }
        if (pressed(ImGuiKey_Escape)) {
            completionOpen_ = false;
            return;
        }
        if ((pressed(ImGuiKey_Tab) || pressed(ImGuiKey_Enter)) && !readOnly && intel) {
            acceptSuggestion();
            changed = true;
            return;
        }
        if ((pressed(ImGuiKey_Tab) || pressed(ImGuiKey_Enter)) && !readOnly) {
            const Completion* c = matches_[static_cast<size_t>(completionIndex_)];
            pushUndo();
            std::string word = currentWord();
            insert(c->word.substr(word.size()));
            completionOpen_ = false;
            changed = true;
            return;
        }
    }

    if (sigOpen_ && pressed(ImGuiKey_Escape)) {
        sigOpen_ = false;
        return;
    }
    if (ctrl && pressed(ImGuiKey_Space)) {
        if (intel)
            openSuggestions(true);
        else
            updateCompletions();
        return;
    }
    // Zoom: Ctrl+= / Ctrl+- / Ctrl+0
    if (ctrl && (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd))) {
        zoom = std::min(2.5f, zoom + 0.1f);
        return;
    }
    if (ctrl && (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract))) {
        zoom = std::max(0.6f, zoom - 0.1f);
        return;
    }
    if (ctrl && pressed(ImGuiKey_0)) {
        zoom = 1.0f;
        return;
    }
    if (pressed(ImGuiKey_F12)) {
        goToDefinition(cursor_);
        return;
    }
    if (ctrl && pressed(ImGuiKey_L)) { // select the whole line
        anchor_ = {cursor_.line, 0};
        cursor_ = cursor_.line + 1 < static_cast<int>(lines_.size()) ? Pos{cursor_.line + 1, 0}
                                                                     : Pos{cursor_.line, static_cast<int>(lines_[static_cast<size_t>(cursor_.line)].size())};
        return;
    }
    if (!readOnly && io.KeyAlt && !ctrl && (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow))) {
        bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
        if (shift)
            duplicateLines(up); // Shift+Alt+Up/Down
        else
            moveLines(up ? -1 : 1); // Alt+Up/Down
        changed = true;
        completionOpen_ = false;
        return;
    }
    if (!readOnly && ctrl && shift && pressed(ImGuiKey_K)) {
        deleteLines();
        changed = true;
        return;
    }
    if (!readOnly && ctrl && (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))) {
        // A new line below (or above with Shift), wherever the cursor is in this one.
        pushUndo();
        if (shift) {
            cursor_ = anchor_ = {cursor_.line, 0};
            std::string indent(static_cast<size_t>(std::min(lines_[static_cast<size_t>(cursor_.line)].find_first_not_of(' '),
                                                            lines_[static_cast<size_t>(cursor_.line)].size())), ' ');
            insert(indent + "\n");
            cursor_ = anchor_ = {cursor_.line - 1, static_cast<int>(indent.size())};
        } else {
            cursor_ = anchor_ = {cursor_.line, static_cast<int>(lines_[static_cast<size_t>(cursor_.line)].size())};
            newline();
        }
        changed = true;
        completionOpen_ = false;
        return;
    }
    if (ctrl && pressed(ImGuiKey_F)) {
        openFind(false);
        return;
    }
    if (ctrl && pressed(ImGuiKey_H)) {
        openFind(true);
        return;
    }
    if (ctrl && pressed(ImGuiKey_G)) {
        openGoto();
        return;
    }
    if (pressed(ImGuiKey_F3)) {
        findNext(shift);
        return;
    }

    Pos p = cursor_;
    if (pressed(ImGuiKey_LeftArrow)) {
        if (hasSelection() && !shift)
            p = selStart();
        else if (p.col > 0)
            p.col = ctrl ? wordStart(p.line, prevChar(p.line, p.col)) : prevChar(p.line, p.col);
        else if (p.line > 0)
            p = {p.line - 1, static_cast<int>(lines_[static_cast<size_t>(p.line - 1)].size())};
        moveCursor(p, shift);
        completionOpen_ = false;
    } else if (pressed(ImGuiKey_RightArrow)) {
        if (hasSelection() && !shift)
            p = selEnd();
        else if (p.col < static_cast<int>(lines_[static_cast<size_t>(p.line)].size()))
            p.col = ctrl ? wordEnd(p.line, nextChar(p.line, p.col)) : nextChar(p.line, p.col);
        else if (p.line + 1 < static_cast<int>(lines_.size()))
            p = {p.line + 1, 0};
        moveCursor(p, shift);
        completionOpen_ = false;
    } else if (pressed(ImGuiKey_UpArrow)) {
        float x = columnX(p.line, p.col);
        if (p.line > 0) {
            --p.line;
            p.col = columnAt(p.line, x);
        }
        moveCursor(p, shift);
    } else if (pressed(ImGuiKey_DownArrow)) {
        float x = columnX(p.line, p.col);
        if (p.line + 1 < static_cast<int>(lines_.size())) {
            ++p.line;
            p.col = columnAt(p.line, x);
        }
        moveCursor(p, shift);
    } else if (pressed(ImGuiKey_Home)) {
        const std::string& s = lines_[static_cast<size_t>(p.line)];
        int first = static_cast<int>(std::min(s.find_first_not_of(' '), s.size()));
        p.col = ctrl ? 0 : (p.col == first ? 0 : first);
        if (ctrl)
            p.line = 0;
        moveCursor(p, shift);
    } else if (pressed(ImGuiKey_End)) {
        if (ctrl)
            p.line = static_cast<int>(lines_.size()) - 1;
        p.col = static_cast<int>(lines_[static_cast<size_t>(p.line)].size());
        moveCursor(p, shift);
    } else if (pressed(ImGuiKey_PageUp)) {
        p.line = std::max(0, p.line - 20);
        moveCursor(p, shift);
    } else if (pressed(ImGuiKey_PageDown)) {
        p.line = std::min(static_cast<int>(lines_.size()) - 1, p.line + 20);
        moveCursor(p, shift);
    } else if (ctrl && pressed(ImGuiKey_A)) {
        anchor_ = {0, 0};
        cursor_ = {static_cast<int>(lines_.size()) - 1, static_cast<int>(lines_.back().size())};
    } else if (ctrl && pressed(ImGuiKey_C)) {
        if (hasSelection())
            ImGui::SetClipboardText(selectedText().c_str());
    }
    if (readOnly)
        return;

    if (ctrl && pressed(ImGuiKey_X)) {
        if (hasSelection()) {
            ImGui::SetClipboardText(selectedText().c_str());
            pushUndo();
            deleteSelection();
            changed = true;
        }
    } else if (ctrl && pressed(ImGuiKey_V)) {
        if (const char* clip = ImGui::GetClipboardText()) {
            pushUndo();
            insert(clip);
            changed = true;
        }
    } else if (ctrl && !shift && pressed(ImGuiKey_Z)) {
        undoStep();
        changed = true;
    } else if (ctrl && (pressed(ImGuiKey_Y) || (shift && pressed(ImGuiKey_Z)))) {
        redoStep();
        changed = true;
    } else if (ctrl && pressed(ImGuiKey_Slash)) {
        pushUndo();
        toggleComment();
        changed = true;
    } else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        pushUndo();
        newline();
        changed = true;
    } else if (pressed(ImGuiKey_Tab)) {
        pushUndo();
        if (hasSelection() && selStart().line != selEnd().line)
            indentSelection(shift);
        else if (shift)
            indentSelection(true);
        else
            insert(std::string(static_cast<size_t>(4 - cursor_.col % 4), ' '));
        changed = true;
    } else if (pressed(ImGuiKey_Backspace)) {
        pushUndo(true);
        if (hasSelection()) {
            deleteSelection();
        } else if (cursor_.col > 0) {
            std::string& s = lines_[static_cast<size_t>(cursor_.line)];
            int from = prevChar(cursor_.line, cursor_.col);
            // Backspace in leading spaces removes a whole indent level.
            bool allSpaces = s.find_first_not_of(' ') >= static_cast<size_t>(cursor_.col);
            if (allSpaces && cursor_.col % 4 == 0 && cursor_.col >= 4)
                from = cursor_.col - 4;
            // Deleting an opening bracket also deletes its partner right after it.
            char open = s[static_cast<size_t>(from)];
            char close = cursor_.col < static_cast<int>(s.size()) ? s[static_cast<size_t>(cursor_.col)] : 0;
            int extra = ((open == '(' && close == ')') || (open == '[' && close == ']') || (open == '{' && close == '}') ||
                         (open == '"' && close == '"') || (open == '\'' && close == '\''))
                            ? 1
                            : 0;
            s.erase(static_cast<size_t>(from), static_cast<size_t>(cursor_.col - from + extra));
            cursor_.col = from;
            anchor_ = cursor_;
        } else if (cursor_.line > 0) {
            int prevLen = static_cast<int>(lines_[static_cast<size_t>(cursor_.line - 1)].size());
            lines_[static_cast<size_t>(cursor_.line - 1)] += lines_[static_cast<size_t>(cursor_.line)];
            lines_.erase(lines_.begin() + cursor_.line);
            cursor_ = anchor_ = {cursor_.line - 1, prevLen};
        }
        changed = true;
        scrollToCursor_ = true;
        updateCompletions();
    } else if (pressed(ImGuiKey_Delete)) {
        pushUndo(true);
        if (hasSelection()) {
            deleteSelection();
        } else if (cursor_.col < static_cast<int>(lines_[static_cast<size_t>(cursor_.line)].size())) {
            int to = nextChar(cursor_.line, cursor_.col);
            lines_[static_cast<size_t>(cursor_.line)].erase(static_cast<size_t>(cursor_.col), static_cast<size_t>(to - cursor_.col));
        } else if (cursor_.line + 1 < static_cast<int>(lines_.size())) {
            lines_[static_cast<size_t>(cursor_.line)] += lines_[static_cast<size_t>(cursor_.line + 1)];
            lines_.erase(lines_.begin() + cursor_.line + 1);
        }
        changed = true;
    }
}

void CodeEditor::handleTyping(bool& changed) {
    if (readOnly)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeySuper)
        return;
    for (ImWchar c : io.InputQueueCharacters) {
        if (c < 32 || c == 127)
            continue;
        pushUndo(true);
        std::string s;
        if (c < 0x80) {
            s = static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (c >> 12));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
        const std::string& line = lines_[static_cast<size_t>(cursor_.line)];
        char next = cursor_.col < static_cast<int>(line.size()) ? line[static_cast<size_t>(cursor_.col)] : 0;
        // Typing a closing bracket or quote that is already there just steps over it.
        if (!hasSelection() && (c == ')' || c == ']' || c == '}' || c == '"' || c == '\'') && next == static_cast<char>(c)) {
            cursor_.col++;
            anchor_ = cursor_;
            continue;
        }
        const char* pair = c == '(' ? ")" : c == '[' ? "]" : c == '{' ? "}" : (c == '"' || c == '\'') ? (c == '"' ? "\"" : "'") : nullptr;
        bool wordAfter = next && isWordChar(next);
        if (pair && !wordAfter) {
            insert(s + pair);
            cursor_.col--;
            anchor_ = cursor_;
        } else {
            insert(s);
        }
        changed = true;
    }
    if (changed && !io.InputQueueCharacters.empty()) {
        ImWchar last = io.InputQueueCharacters.back();
        const std::string& line = lines_[static_cast<size_t>(cursor_.line)];
        std::string before = line.substr(0, static_cast<size_t>(cursor_.col));
        // "else:" and "elif ...:" line up with their "if" as soon as the ':' is typed.
        if (last == ':' && language == CodeLanguage::EasyScript) {
            size_t first = before.find_first_not_of(' ');
            std::string t = first == std::string::npos ? "" : before.substr(first);
            if (t == "else:" || (t.rfind("elif ", 0) == 0 && t.back() == ':')) {
                int indent = static_cast<int>(first);
                for (int l = cursor_.line - 1; l >= 0; --l) {
                    const std::string& p = lines_[static_cast<size_t>(l)];
                    size_t pf = p.find_first_not_of(' ');
                    if (pf == std::string::npos || static_cast<int>(pf) >= indent)
                        continue;
                    std::string pt = p.substr(pf);
                    if (pt.rfind("if ", 0) == 0 || pt.rfind("elif ", 0) == 0)
                        {
                            int target = static_cast<int>(pf);
                            lines_[static_cast<size_t>(cursor_.line)].erase(0, static_cast<size_t>(indent - target));
                            cursor_.col -= indent - target;
                            anchor_ = cursor_;
                        }
                    break;
                }
            }
        }
        if (intel) {
            bool word = isWordChar(static_cast<char>(last));
            if (word || last == '.' || last == '"' || last == '\'' || last == '>')
                openSuggestions(false);
            else if (last == ' ' && before.size() >= 4 && before.compare(before.size() - 4, 4, "def ") == 0)
                openSuggestions(true); // the events Aven calls
            else
                completionOpen_ = false;
            updateSignature();
        } else {
            updateCompletions();
        }
    }
}

float CodeEditor::columnX(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    ImFont* f = font ? font : ImGui::GetFont();
    return f->CalcTextSizeA(fontSize_, FLT_MAX, 0, s.c_str(), s.c_str() + std::min<size_t>(static_cast<size_t>(col), s.size())).x;
}

int CodeEditor::columnAt(int line, float x) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    int col = 0;
    while (col < static_cast<int>(s.size())) {
        int next = nextChar(line, col);
        float mid = (columnX(line, col) + columnX(line, next)) * 0.5f;
        if (x < mid)
            break;
        col = next;
    }
    return col;
}

void CodeEditor::drawLine(ImDrawList* dl, int index, ImVec2 pos, bool& inTriple) const {
    const std::string& s = lines_[static_cast<size_t>(index)];
    ImFont* f = font ? font : ImGui::GetFont();
    float size = fontSize_;
    float x = pos.x;
    auto emit = [&](size_t from, size_t to, ImU32 color) {
        if (to <= from)
            return;
        dl->AddText(f, size, {x, pos.y}, color, s.c_str() + from, s.c_str() + to);
        x += f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str() + from, s.c_str() + to).x;
    };
    size_t i = 0;
    if (inTriple) {
        size_t end = s.find("\"\"\"");
        if (end == std::string::npos) {
            emit(0, s.size(), CodeEditor::palette.string);
            return;
        }
        emit(0, end + 3, CodeEditor::palette.string);
        i = end + 3;
        inTriple = false;
    }
    std::string prevWord;
    bool cLike = language == CodeLanguage::CSharp || language == CodeLanguage::Cpp;
    while (i < s.size()) {
        char c = s[i];
        bool commentStart = cLike ? s.compare(i, 2, "//") == 0
                            : language == CodeLanguage::Luau ? s.compare(i, 2, "--") == 0
                                                              : c == '#';
        if (commentStart) {
            emit(i, s.size(), CodeEditor::palette.comment);
            return;
        }
        if (language == CodeLanguage::Cpp && c == '#') { // #include, #pragma
            size_t start = i++;
            while (i < s.size() && isWordChar(s[i]))
                ++i;
            emit(start, i, CodeEditor::palette.keyword);
            continue;
        }
        if (c == '`' && language == CodeLanguage::Luau) {
            size_t start = i++;
            while (i < s.size() && s[i] != '`') {
                if (s[i] == '\\')
                    ++i;
                ++i;
            }
            i = std::min(i + 1, s.size());
            emit(start, i, CodeEditor::palette.string);
            continue;
        }
        if (c == '$' && language == CodeLanguage::CSharp && i + 1 < s.size() && s[i + 1] == '"') {
            emit(i, i + 1, CodeEditor::palette.string);
            ++i;
            continue;
        }
        if (s.compare(i, 3, "\"\"\"") == 0) {
            size_t end = s.find("\"\"\"", i + 3);
            if (end == std::string::npos) {
                emit(i, s.size(), CodeEditor::palette.string);
                inTriple = true;
                return;
            }
            emit(i, end + 3, CodeEditor::palette.string);
            i = end + 3;
            continue;
        }
        if (c == '"' || c == '\'' || ((c == 'f' || c == 'F') && i + 1 < s.size() && (s[i + 1] == '"' || s[i + 1] == '\''))) {
            size_t start = i;
            if (c == 'f' || c == 'F')
                ++i;
            char q = s[i++];
            while (i < s.size() && s[i] != q) {
                if (s[i] == '\\')
                    ++i;
                ++i;
            }
            i = std::min(i + 1, s.size());
            emit(start, i, CodeEditor::palette.string);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == '_'))
                ++i;
            emit(start, i, CodeEditor::palette.number);
            continue;
        }
        if (isWordChar(c)) {
            size_t start = i;
            while (i < s.size() && isWordChar(s[i]))
                ++i;
            std::string word = s.substr(start, i - start);
            ImU32 color = CodeEditor::palette.text;
            bool selfWord = language == CodeLanguage::Luau ? (word == "part" || word == "script" || word == "game" || word == "workspace")
                            : cLike                        ? (word == "this" || word == "gameObject" || word == "transform")
                                                           : (word == "self" || word == "game");
            if (isKeyword(word, language))
                color = CodeEditor::palette.keyword;
            else if (selfWord)
                color = CodeEditor::palette.self;
            else if (prevWord == "def" || prevWord == "func" || prevWord == "function")
                color = CodeEditor::palette.function;
            else if (cLike && std::isupper(static_cast<unsigned char>(word[0])) && !(i < s.size() && s[i] == '('))
                color = CodeEditor::palette.builtin; // types and classes: Vector3, GameObject, FVector
            else if (language == CodeLanguage::GDScript && std::isupper(static_cast<unsigned char>(word[0])))
                color = CodeEditor::palette.builtin;
            else if (highlightWords.count(word))
                color = CodeEditor::palette.builtin;
            else if (i < s.size() && s[i] == '(')
                color = CodeEditor::palette.function;
            emit(start, i, color);
            prevWord = word;
            continue;
        }
        size_t start = i++;
        while (i < s.size() && !isWordChar(s[i]) && s[i] != '"' && s[i] != '\'' && s[i] != '#' && s[i] != '`' && s[i] != '$' &&
               !(s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') && !(s[i] == '-' && i + 1 < s.size() && s[i + 1] == '-') &&
               !std::isdigit(static_cast<unsigned char>(s[i])))
            ++i;
        emit(start, i, CodeEditor::palette.text);
    }
}

void CodeEditor::openFind(bool replace) {
    findOpen_ = true;
    replaceOpen_ = replace;
    gotoOpen_ = false;
    findFocus_ = true;
    if (hasSelection() && selStart().line == selEnd().line)
        findText_ = selectedText();
}

void CodeEditor::openGoto() {
    gotoOpen_ = true;
    findOpen_ = false;
    findFocus_ = true;
    gotoText_.clear();
}

bool CodeEditor::matchesAt(const std::string& line, size_t col) const {
    if (findText_.empty() || col + findText_.size() > line.size())
        return false;
    for (size_t i = 0; i < findText_.size(); ++i) {
        char a = line[col + i], b = findText_[i];
        if (!findCase_) {
            a = static_cast<char>(std::tolower(static_cast<unsigned char>(a)));
            b = static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
        }
        if (a != b)
            return false;
    }
    return true;
}

bool CodeEditor::findNext(bool backwards) {
    if (findText_.empty())
        return false;
    int n = static_cast<int>(lines_.size());
    Pos from = backwards ? selStart() : selEnd();
    for (int step = 0; step <= n; ++step) {
        int l = backwards ? ((from.line - step) % n + n) % n : (from.line + step) % n;
        const std::string& line = lines_[static_cast<size_t>(l)];
        if (!backwards) {
            size_t start = step == 0 ? static_cast<size_t>(from.col) : 0;
            for (size_t c = start; c + findText_.size() <= line.size(); ++c)
                if (matchesAt(line, c)) {
                    anchor_ = {l, static_cast<int>(c)};
                    cursor_ = {l, static_cast<int>(c + findText_.size())};
                    scrollToCursor_ = true;
                    return true;
                }
        } else {
            int end = step == 0 ? from.col - 1 : static_cast<int>(line.size());
            for (int c = std::min(end, static_cast<int>(line.size()) - 1); c >= 0; --c)
                if (matchesAt(line, static_cast<size_t>(c))) {
                    anchor_ = {l, c};
                    cursor_ = {l, c + static_cast<int>(findText_.size())};
                    scrollToCursor_ = true;
                    return true;
                }
        }
    }
    return false;
}

bool CodeEditor::drawFindBar() {
    bool changed = false;
    if (!findOpen_ && !gotoOpen_)
        return false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 3});
    if (gotoOpen_) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Go to line");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (findFocus_) {
            ImGui::SetKeyboardFocusHere();
            findFocus_ = false;
        }
        if (ImGui::InputText("##goto", &gotoText_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
            gotoLine(std::atoi(gotoText_.c_str()));
            gotoOpen_ = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("of %d", static_cast<int>(lines_.size()));
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            gotoOpen_ = false;
        ImGui::PopStyleVar();
        return false;
    }
    int count = 0;
    for (auto& line : lines_)
        for (size_t c = 0; c + findText_.size() <= line.size() && !findText_.empty(); ++c)
            if (matchesAt(line, c))
                ++count;
    ImGui::SetNextItemWidth(220);
    if (findFocus_) {
        ImGui::SetKeyboardFocusHere();
        findFocus_ = false;
    }
    if (ImGui::InputTextWithHint("##find", "Find", &findText_, ImGuiInputTextFlags_EnterReturnsTrue))
        findNext(ImGui::GetIO().KeyShift), findFocus_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%d found", count);
    ImGui::SameLine();
    if (ImGui::SmallButton("<"))
        findNext(true);
    ImGui::SameLine();
    if (ImGui::SmallButton(">"))
        findNext(false);
    ImGui::SameLine();
    ImGui::Checkbox("Aa", &findCase_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Match upper and lower case");
    ImGui::SameLine();
    if (!readOnly && ImGui::SmallButton(replaceOpen_ ? "Hide replace" : "Replace..."))
        replaceOpen_ = !replaceOpen_;
    ImGui::SameLine();
    if (ImGui::SmallButton("x"))
        findOpen_ = false;
    if (replaceOpen_ && !readOnly) {
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##replace", "Replace with", &replaceText_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Replace")) {
            if (hasSelection() && selStart().line == selEnd().line &&
                matchesAt(lines_[static_cast<size_t>(selStart().line)], static_cast<size_t>(selStart().col))) {
                pushUndo();
                deleteSelection();
                insert(replaceText_);
                changed = true;
            }
            findNext(false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Replace all") && !findText_.empty()) {
            pushUndo();
            for (auto& line : lines_) {
                std::string out;
                for (size_t c = 0; c < line.size();) {
                    if (matchesAt(line, c)) {
                        out += replaceText_;
                        c += findText_.size();
                    } else {
                        out += line[c++];
                    }
                }
                line = out;
            }
            cursor_ = anchor_ = clamp(cursor_);
            changed = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        findOpen_ = false;
    ImGui::PopStyleVar();
    return changed;
}


// ---------------------------------------------------------------- code intelligence

namespace {

int indentWidth(const std::string& s) {
    int n = 0;
    while (n < static_cast<int>(s.size()) && s[static_cast<size_t>(n)] == ' ')
        ++n;
    return n;
}

// A letter and a color for each kind of suggestion.
std::pair<const char*, ImU32> kindBadge(script::SuggestionKind k) {
    using K = script::SuggestionKind;
    switch (k) {
    case K::Keyword: return {"k", IM_COL32(148, 163, 184, 255)};
    case K::Function: return {"f", IM_COL32(96, 165, 250, 255)};
    case K::Method: return {"m", IM_COL32(167, 139, 250, 255)};
    case K::Property: return {"p", IM_COL32(45, 212, 191, 255)};
    case K::Variable: return {"v", IM_COL32(251, 146, 60, 255)};
    case K::Event: return {"e", IM_COL32(244, 114, 182, 255)};
    case K::Snippet: return {"s", IM_COL32(74, 222, 128, 255)};
    case K::File: return {"F", IM_COL32(250, 204, 21, 255)};
    case K::Text: return {"T", IM_COL32(163, 230, 53, 255)};
    case K::Component: return {"C", IM_COL32(56, 189, 248, 255)};
    }
    return {"?", IM_COL32(150, 150, 150, 255)};
}

} // namespace

std::vector<script::OutlineItem> CodeEditor::outline() const {
    return intel ? intel->outline(lines_) : std::vector<script::OutlineItem>{};
}

void CodeEditor::gotoPosition(int line, int col) {
    cursor_ = anchor_ = clamp({line, col});
    focused_ = true;
    scrollToCursor_ = true;
    gotoLine_ = -1;
    pendingScrollLine_ = line;
}

void CodeEditor::openSuggestions(bool manual) {
    if (!intel || readOnly) {
        completionOpen_ = false;
        return;
    }
    int from = cursor_.col;
    suggestions_ = intel->suggest(lines_, cursor_.line, cursor_.col, from, manual);
    suggestFrom_ = std::clamp(from, 0, cursor_.col);
    const std::string& line = lines_[static_cast<size_t>(cursor_.line)];
    std::string typed = line.substr(static_cast<size_t>(suggestFrom_), static_cast<size_t>(cursor_.col - suggestFrom_));
    // Already typed in full: nothing to offer (Ctrl+Space still shows the list).
    if (!manual && !suggestions_.empty() && suggestions_.front().label == typed)
        suggestions_.clear();
    completionOpen_ = !suggestions_.empty();
    completionIndex_ = 0;
    suggestScroll_ = 0;
}

void CodeEditor::acceptSuggestion() {
    if (completionIndex_ < 0 || completionIndex_ >= static_cast<int>(suggestions_.size()))
        return;
    script::Suggestion sug = suggestions_[static_cast<size_t>(completionIndex_)];
    pushUndo();
    anchor_ = {cursor_.line, std::clamp(suggestFrom_, 0, cursor_.col)};
    deleteSelection();
    std::string indent(static_cast<size_t>(indentWidth(lines_[static_cast<size_t>(cursor_.line)])), ' ');
    std::string text;
    for (size_t i = 0; i < sug.insert.size(); ++i) {
        if (sug.insert[i] == '\n') {
            text += "\n" + indent;
            if (i + 1 < sug.insert.size() && sug.insert[i + 1] == '\t') {
                text += "    ";
                ++i;
            }
        } else {
            text += sug.insert[i];
        }
    }
    // Parentheses that are already there aren't added again.
    const std::string& now = lines_[static_cast<size_t>(cursor_.line)];
    char next = cursor_.col < static_cast<int>(now.size()) ? now[static_cast<size_t>(cursor_.col)] : 0;
    size_t paren = text.find("($0)");
    if (next == '(' && paren != std::string::npos)
        text = text.substr(0, paren);
    else if (next == '(' && text.size() > 2 && text.compare(text.size() - 2, 2, "()") == 0)
        text.resize(text.size() - 2);
    size_t mark = text.find("$0");
    if (mark == std::string::npos) {
        insert(text);
    } else {
        insert(text.substr(0, mark));
        Pos at = cursor_;
        insert(text.substr(mark + 2));
        cursor_ = anchor_ = at;
    }
    completionOpen_ = false;
    updateSignature();
    // After picking a function, show what goes in the brackets.
    if (sug.kind == script::SuggestionKind::Event)
        sigOpen_ = false;
}

void CodeEditor::updateSignature() {
    sigCursor_ = cursor_;
    sigOpen_ = intel && !readOnly && intel->signature(lines_, cursor_.line, cursor_.col, sig_);
}

void CodeEditor::moveLines(int dir) {
    int a = selStart().line, b = selEnd().line;
    if (hasSelection() && selEnd().col == 0 && b > a)
        --b;
    if ((dir < 0 && a == 0) || (dir > 0 && b + 1 >= static_cast<int>(lines_.size())))
        return;
    pushUndo();
    if (dir < 0)
        std::rotate(lines_.begin() + a - 1, lines_.begin() + a, lines_.begin() + b + 1);
    else
        std::rotate(lines_.begin() + a, lines_.begin() + b + 1, lines_.begin() + b + 2);
    cursor_.line += dir;
    anchor_.line += dir;
    scrollToCursor_ = true;
}

void CodeEditor::duplicateLines(bool up) {
    int a = selStart().line, b = selEnd().line;
    if (hasSelection() && selEnd().col == 0 && b > a)
        --b;
    pushUndo();
    std::vector<std::string> copy(lines_.begin() + a, lines_.begin() + b + 1);
    lines_.insert(lines_.begin() + b + 1, copy.begin(), copy.end());
    if (!up) {
        cursor_.line += b - a + 1;
        anchor_.line += b - a + 1;
    }
    scrollToCursor_ = true;
}

void CodeEditor::deleteLines() {
    int a = selStart().line, b = selEnd().line;
    if (hasSelection() && selEnd().col == 0 && b > a)
        --b;
    pushUndo();
    lines_.erase(lines_.begin() + a, lines_.begin() + b + 1);
    if (lines_.empty())
        lines_.push_back("");
    cursor_ = anchor_ = clamp({a, cursor_.col});
    scrollToCursor_ = true;
}

void CodeEditor::goToDefinition(Pos at) {
    int l = 0, c = 0;
    if (intel && intel->definition(lines_, at.line, at.col, l, c))
        gotoPosition(l, c);
}

bool CodeEditor::matchingBracket(Pos& a, Pos& b) const {
    auto charAt = [&](Pos p) -> char {
        const std::string& s = lines_[static_cast<size_t>(p.line)];
        return p.col >= 0 && p.col < static_cast<int>(s.size()) ? s[static_cast<size_t>(p.col)] : 0;
    };
    const char* opens = "([{";
    const char* closes = ")]}";
    Pos candidates[2] = {cursor_, {cursor_.line, cursor_.col - 1}};
    for (Pos p : candidates) {
        char c = charAt(p);
        if (!c)
            continue;
        const char* o = std::strchr(opens, c);
        const char* cl = std::strchr(closes, c);
        if (!o && !cl)
            continue;
        char open = o ? c : opens[cl - closes], close = o ? closes[o - opens] : c;
        int dir = o ? 1 : -1, depth = 0, budget = 5000;
        Pos q = p;
        while (budget-- > 0) {
            char x = charAt(q);
            if (x == open)
                depth += dir;
            else if (x == close)
                depth -= dir;
            if (depth == 0 && (x == open || x == close) && q != p) {
                a = p;
                b = q;
                return true;
            }
            // step
            if (dir > 0) {
                if (q.col + 1 < static_cast<int>(lines_[static_cast<size_t>(q.line)].size()))
                    ++q.col;
                else if (q.line + 1 < static_cast<int>(lines_.size()))
                    q = {q.line + 1, 0};
                else
                    break;
            } else {
                if (q.col > 0)
                    --q.col;
                else if (q.line > 0)
                    q = {q.line - 1, std::max(0, static_cast<int>(lines_[static_cast<size_t>(q.line - 1)].size()) - 1)};
                else
                    break;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------- drawing

bool CodeEditor::draw(const char* id, ImVec2 size) {
    bool changed = false;
    if (findOpen_ || gotoOpen_) {
        float before = ImGui::GetCursorPosY();
        changed |= drawFindBar();
        size.y -= ImGui::GetCursorPosY() - before;
    }
    ImGuiIO& io = ImGui::GetIO();
    uiFont_ = ImGui::GetFont();
    ImFont* f = font ? font : ImGui::GetFont();
    float statusH = ImGui::GetFrameHeight() + 2;
    ImGui::PushFont(f);
    fontSize_ = f->FontSize * zoom;
    charWidth_ = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, "M").x;
    lineHeight_ = std::round(fontSize_ + 3.0f * zoom);
    float spaceWidth = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, " ").x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.background);
    ImGui::BeginChild(id, {size.x, size.y - statusH}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav |
                          (io.KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float gutter = charWidth_ * (std::to_string(lines_.size()).size() + 2) + 8;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float maxWidth = 0;
    for (size_t l = 0; l < lines_.size(); ++l)
        maxWidth = std::max(maxWidth, static_cast<float>(lines_[l].size()));
    ImVec2 contentSize{gutter + maxWidth * charWidth_ + 200, lines_.size() * lineHeight_ + ImGui::GetWindowHeight() * 0.5f};
    // The suggestion list is drawn over the text: clicks there pick a suggestion.
    bool overPopup = completionOpen_ && intel && ImGui::IsMouseHoveringRect(popupMin_, popupMax_, false) &&
                     ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::InvisibleButton("##text", contentSize, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    if (ImGui::IsItemActivated())
        focused_ = true;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        focused_ = false;

    auto mouseToPos = [&]() {
        ImVec2 m = ImGui::GetMousePos();
        Pos p;
        p.line = std::clamp(static_cast<int>((m.y - origin.y) / lineHeight_), 0, static_cast<int>(lines_.size()) - 1);
        p.col = columnAt(p.line, m.x - origin.x - gutter);
        return p;
    };
    if (overPopup) {
        int row = static_cast<int>((ImGui::GetMousePos().y - popupMin_.y - 4) / lineHeight_);
        int index = suggestScroll_ + row;
        if (index >= 0 && index < static_cast<int>(suggestions_.size()))
            completionIndex_ = index;
        if (io.MouseWheel != 0)
            suggestScroll_ = std::clamp(suggestScroll_ - static_cast<int>(io.MouseWheel), 0,
                                        std::max(0, static_cast<int>(suggestions_.size()) - 10));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !readOnly) {
            acceptSuggestion();
            changed = true;
        }
    } else if (ImGui::IsItemActivated()) {
        Pos p = mouseToPos();
        if (io.KeyCtrl && intel) {
            goToDefinition(p); // Ctrl+click
        } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            anchor_ = {p.line, wordStart(p.line, p.col)};
            cursor_ = {p.line, wordEnd(p.line, p.col)};
        } else {
            moveCursor(p, io.KeyShift);
            dragging_ = true;
        }
        completionOpen_ = false;
    }
    if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        cursor_ = mouseToPos();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        dragging_ = false;
    // Ctrl+wheel zooms.
    if (hovered && io.KeyCtrl && io.MouseWheel != 0)
        zoom = std::clamp(zoom + io.MouseWheel * 0.1f, 0.6f, 2.5f);

    if (focused_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        handleKeys(changed);
        handleTyping(changed);
    }
    if (intel && focused_ && cursor_ != sigCursor_)
        updateSignature();
    if (gotoLine_ > 0) {
        cursor_ = anchor_ = clamp({gotoLine_ - 1, 0});
        focused_ = true;
        scrollToCursor_ = true;
        pendingScrollLine_ = gotoLine_ - 1;
        gotoLine_ = -1;
    }
    if (pendingScrollLine_ >= 0) {
        ImGui::SetScrollY(std::max(0.0f, pendingScrollLine_ * lineHeight_ - ImGui::GetWindowHeight() * 0.4f));
        pendingScrollLine_ = -1;
    }

    // Problems are checked a moment after typing stops.
    if (changed && intel)
        diagTimer_ = 0.5f;
    if (diagTimer_ > 0) {
        diagTimer_ -= io.DeltaTime;
        if (diagTimer_ <= 0 && intel) {
            diags_ = intel->diagnose(text());
            diagTimer_ = -1;
        }
    }

    // Visible range
    float scrollY = ImGui::GetScrollY();
    int first = std::max(0, static_cast<int>(scrollY / lineHeight_) - 1);
    int last = std::min(static_cast<int>(lines_.size()), first + static_cast<int>(ImGui::GetWindowHeight() / lineHeight_) + 3);
    ImVec2 winPos = ImGui::GetWindowPos();
    float winW = ImGui::GetWindowWidth();
    float textX = origin.x + gutter;

    Pos a = selStart(), b = selEnd();
    bool inTriple = false;
    for (int l = 0; l < first; ++l) {
        // Keep track of multi-line strings above the visible area.
        const std::string& s = lines_[static_cast<size_t>(l)];
        size_t pos = 0, count = 0;
        while ((pos = s.find("\"\"\"", pos)) != std::string::npos) {
            ++count;
            pos += 3;
        }
        if (count % 2)
            inTriple = !inTriple;
    }
    // The word under the cursor is highlighted wherever else it appears.
    std::string cursorWord;
    if (focused_ && !hasSelection()) {
        int ws = wordStart(cursor_.line, cursor_.col), we = wordEnd(cursor_.line, cursor_.col);
        if (we - ws >= 2)
            cursorWord = lines_[static_cast<size_t>(cursor_.line)].substr(static_cast<size_t>(ws), static_cast<size_t>(we - ws));
        if (!cursorWord.empty() && std::isdigit(static_cast<unsigned char>(cursorWord[0])))
            cursorWord.clear();
    }
    Pos bracketA, bracketB;
    bool bracket = focused_ && matchingBracket(bracketA, bracketB);
    ImU32 guideColor = (palette.gutterLine & 0x00FFFFFF) | (static_cast<ImU32>(150) << 24);

    for (int l = first; l < last; ++l) {
        float y = origin.y + l * lineHeight_;
        const std::string& line = lines_[static_cast<size_t>(l)];
        if (l == cursor_.line && focused_ && !hasSelection())
            dl->AddRectFilled({winPos.x, y}, {winPos.x + winW, y + lineHeight_}, palette.currentLine);
        if (l + 1 == errorLine_) {
            dl->AddRectFilled({winPos.x, y}, {winPos.x + winW, y + lineHeight_}, IM_COL32(224, 70, 70, 55));
            dl->AddCircleFilled({origin.x + 6, y + lineHeight_ * 0.5f}, 4, IM_COL32(240, 80, 80, 255));
        }
        // Indent guides: a faint line for each level of indentation.
        int indent = indentWidth(line);
        if (indent == static_cast<int>(line.size())) { // blank: follow the lines around it
            int above = 0, below = 0;
            for (int k = l - 1; k >= 0; --k)
                if (lines_[static_cast<size_t>(k)].find_first_not_of(' ') != std::string::npos) {
                    above = indentWidth(lines_[static_cast<size_t>(k)]);
                    break;
                }
            for (int k = l + 1; k < static_cast<int>(lines_.size()); ++k)
                if (lines_[static_cast<size_t>(k)].find_first_not_of(' ') != std::string::npos) {
                    below = indentWidth(lines_[static_cast<size_t>(k)]);
                    break;
                }
            indent = std::min(above, below);
        }
        for (int k = 4; k <= indent; k += 4) {
            float gx = std::round(textX + (k - 4) * spaceWidth) + 0.5f;
            dl->AddLine({gx, y}, {gx, y + lineHeight_}, guideColor);
        }
        if (findOpen_ && !findText_.empty()) {
            for (size_t c = 0; c + findText_.size() <= line.size(); ++c)
                if (matchesAt(line, c))
                    dl->AddRect({textX + columnX(l, static_cast<int>(c)), y},
                                {textX + columnX(l, static_cast<int>(c + findText_.size())), y + lineHeight_},
                                IM_COL32(250, 200, 60, 200), 2);
        } else if (!cursorWord.empty()) {
            for (size_t at = line.find(cursorWord); at != std::string::npos; at = line.find(cursorWord, at + 1)) {
                bool left = at == 0 || !isWordChar(line[at - 1]);
                bool right = at + cursorWord.size() >= line.size() || !isWordChar(line[at + cursorWord.size()]);
                if (left && right)
                    dl->AddRectFilled({textX + columnX(l, static_cast<int>(at)), y},
                                      {textX + columnX(l, static_cast<int>(at + cursorWord.size())), y + lineHeight_},
                                      (palette.selection & 0x00FFFFFF) | (static_cast<ImU32>(55) << 24), 2);
            }
        }
        if (hasSelection() && l >= a.line && l <= b.line) {
            float x0 = l == a.line ? columnX(l, a.col) : 0;
            float x1 = l == b.line ? columnX(l, b.col) : columnX(l, static_cast<int>(line.size())) + charWidth_;
            dl->AddRectFilled({textX + x0, y}, {textX + x1, y + lineHeight_}, palette.selection);
        }
        if (bracket)
            for (Pos p : {bracketA, bracketB})
                if (p.line == l)
                    dl->AddRect({textX + columnX(l, p.col), y}, {textX + columnX(l, p.col + 1), y + lineHeight_},
                                (palette.text & 0x00FFFFFF) | (static_cast<ImU32>(130) << 24), 2);
        char num[16];
        std::snprintf(num, sizeof num, "%d", l + 1);
        float numW = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, num).x;
        dl->AddText(f, fontSize_, {origin.x + gutter - numW - 10, y + 1},
                    l == cursor_.line ? palette.currentLineNumber : palette.lineNumber, num);
        drawLine(dl, l, {textX, y + 1}, inTriple);
    }
    // Problems: a wavy underline and a mark in the margin.
    for (auto& d : diags_) {
        if (d.line < first || d.line >= last || d.line >= static_cast<int>(lines_.size()))
            continue;
        ImU32 col = d.error ? IM_COL32(248, 81, 73, 255) : IM_COL32(234, 179, 8, 255);
        float y = origin.y + d.line * lineHeight_ + lineHeight_ - 2;
        int c0 = std::clamp(d.col, 0, static_cast<int>(lines_[static_cast<size_t>(d.line)].size()));
        int c1 = std::max(c0 + 1, d.endCol);
        float x0 = textX + columnX(d.line, c0);
        float x1 = std::max(x0 + charWidth_, textX + columnX(d.line, c1));
        float amp = 1.5f * zoom, step = 2.5f * zoom;
        ImVec2 pts[400];
        int n = 0;
        bool up = true;
        for (float x = x0; x < x1 && n < 399; x += step, up = !up)
            pts[n++] = {x, y + (up ? -amp : amp)};
        pts[n++] = {x1, y + (up ? -amp : amp)};
        dl->AddPolyline(pts, n, col, 0, 1.3f);
        float my = origin.y + d.line * lineHeight_ + lineHeight_ * 0.5f;
        if (d.error)
            dl->AddCircleFilled({origin.x + 6, my}, 3.5f * zoom, col);
        else
            dl->AddTriangleFilled({origin.x + 6, my - 4 * zoom}, {origin.x + 2, my + 3 * zoom}, {origin.x + 10, my + 3 * zoom}, col);
    }
    dl->AddLine({origin.x + gutter - 5, winPos.y}, {origin.x + gutter - 5, winPos.y + ImGui::GetWindowHeight()},
                palette.gutterLine);

    // Cursor
    blink_ += io.DeltaTime;
    ImVec2 cursorScreen{textX + columnX(cursor_.line, cursor_.col), origin.y + cursor_.line * lineHeight_};
    if (focused_ && !readOnly && std::fmod(blink_, 1.0f) < 0.6f)
        dl->AddRectFilled(cursorScreen, {cursorScreen.x + 2, cursorScreen.y + lineHeight_}, palette.cursor);
    if (scrollToCursor_) {
        float top = cursorScreen.y - winPos.y, bottom = top + lineHeight_;
        if (top < lineHeight_)
            ImGui::SetScrollY(ImGui::GetScrollY() + top - lineHeight_);
        else if (bottom > ImGui::GetWindowHeight() - lineHeight_ * 2)
            ImGui::SetScrollY(ImGui::GetScrollY() + bottom - ImGui::GetWindowHeight() + lineHeight_ * 2);
        float left = cursorScreen.x - winPos.x;
        if (left < gutter + 10)
            ImGui::SetScrollX(std::max(0.0f, ImGui::GetScrollX() + left - gutter - 10));
        else if (left > winW - 40)
            ImGui::SetScrollX(ImGui::GetScrollX() + left - winW + 40);
        scrollToCursor_ = false;
    }

    // Help while hovering: the problem under the mouse, or what the word means.
    ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x == lastMouse_.x && mouse.y == lastMouse_.y)
        mouseStill_ += io.DeltaTime;
    else
        mouseStill_ = 0;
    lastMouse_ = mouse;
    if (hovered && !overPopup && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        int hoverLine = static_cast<int>((mouse.y - origin.y) / lineHeight_);
        bool onText = hoverLine >= 0 && hoverLine < static_cast<int>(lines_.size()) && mouse.x >= textX &&
                      mouse.x <= textX + columnX(hoverLine, static_cast<int>(lines_[static_cast<size_t>(hoverLine)].size())) + charWidth_;
        std::string problem;
        bool problemIsError = false;
        if (onText) {
            int col = columnAt(hoverLine, mouse.x - textX);
            for (auto& d : diags_)
                if (d.line == hoverLine && col >= d.col && col <= std::max(d.col + 1, d.endCol)) {
                    problem = d.message;
                    problemIsError = d.error;
                }
        }
        if (hoverLine + 1 == errorLine_ && problem.empty()) {
            problem = errorMessage_;
            problemIsError = true;
        }
        if (!problem.empty() && mouseStill_ > 0.25f) {
            ImGui::BeginTooltip();
            ImGui::PushFont(uiFont_);
            ImGui::PushStyleColor(ImGuiCol_Text, problemIsError ? IM_COL32(255, 150, 150, 255) : IM_COL32(250, 210, 110, 255));
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
            ImGui::TextUnformatted(problem.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::EndTooltip();
        } else if (onText && intel && mouseStill_ > 0.5f) {
            int from = 0, to = 0;
            std::string help = intel->hover(lines_, hoverLine, columnAt(hoverLine, mouse.x - textX), from, to);
            if (!help.empty()) {
                size_t nl = help.find('\n');
                ImGui::BeginTooltip();
                ImGui::PushFont(f);
                ImGui::TextColored(ImColor(palette.function), "%s", help.substr(0, nl).c_str());
                ImGui::PopFont();
                if (nl != std::string::npos) {
                    ImGui::PushFont(uiFont_);
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
                    ImGui::TextUnformatted(help.c_str() + nl + 1);
                    ImGui::PopTextWrapPos();
                    ImGui::PopFont();
                }
                ImGui::EndTooltip();
            }
        }
    }

    // Autocomplete and parameter hints
    if (completionOpen_ && focused_ && intel)
        drawSuggestions(cursorScreen);
    else
        popupMin_ = popupMax_ = {0, 0};
    if (sigOpen_ && focused_)
        drawSignature(cursorScreen);
    if (completionOpen_ && focused_ && !intel && !matches_.empty()) {
        ImVec2 at{cursorScreen.x, cursorScreen.y + lineHeight_ + 2};
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float w = 0;
        for (auto* m : matches_)
            w = std::max(w, f->CalcTextSizeA(fontSize_, FLT_MAX, 0, (m->word + "   " + m->detail).c_str()).x);
        w = std::min(w + 16, 520.0f);
        float h = matches_.size() * lineHeight_ + 8;
        fg->AddRectFilled(at, {at.x + w, at.y + h}, IM_COL32(40, 44, 52, 245), 4);
        fg->AddRect(at, {at.x + w, at.y + h}, IM_COL32(90, 100, 120, 255), 4);
        for (size_t i = 0; i < matches_.size(); ++i) {
            float y = at.y + 4 + i * lineHeight_;
            if (static_cast<int>(i) == completionIndex_)
                fg->AddRectFilled({at.x + 2, y}, {at.x + w - 2, y + lineHeight_}, IM_COL32(60, 100, 170, 255), 3);
            fg->AddText(f, fontSize_, {at.x + 8, y + 1}, IM_COL32(230, 230, 235, 255), matches_[i]->word.c_str());
            float ww = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, matches_[i]->word.c_str()).x;
            fg->PushClipRect(at, {at.x + w - 4, at.y + h});
            fg->AddText(f, fontSize_, {at.x + 8 + ww + 12, y + 1}, IM_COL32(140, 150, 165, 255), matches_[i]->detail.c_str());
            fg->PopClipRect();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    drawStatusBar();
    if (changed && errorLine_ > 0)
        errorLine_ = 0; // the error may be fixed; it's checked again as you type
    return changed;
}

void CodeEditor::drawSuggestions(ImVec2 cursorScreen) {
    ImFont* f = font ? font : ImGui::GetFont();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const int visible = std::min(10, static_cast<int>(suggestions_.size()));
    if (completionIndex_ < suggestScroll_)
        suggestScroll_ = completionIndex_;
    if (completionIndex_ >= suggestScroll_ + visible)
        suggestScroll_ = completionIndex_ - visible + 1;
    float badge = lineHeight_ - 4;
    float w = 0;
    for (auto& s : suggestions_)
        w = std::max(w, f->CalcTextSizeA(fontSize_, FLT_MAX, 0, s.label.c_str()).x +
                            std::min(220.0f, f->CalcTextSizeA(fontSize_ * 0.9f, FLT_MAX, 0, s.detail.c_str()).x));
    w = std::clamp(w + badge + 40, 220.0f, 620.0f);
    float h = visible * lineHeight_ + 8;
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 at{cursorScreen.x - badge - 12, cursorScreen.y + lineHeight_ + 2};
    if (at.y + h > display.y - 4)
        at.y = cursorScreen.y - h - 2; // not enough room below: open upward
    at.x = std::clamp(at.x, 4.0f, std::max(4.0f, display.x - w - 4));
    popupMin_ = at;
    popupMax_ = {at.x + w, at.y + h};
    fg->AddRectFilled(at, popupMax_, IM_COL32(34, 38, 46, 250), 6);
    fg->AddRect(at, popupMax_, IM_COL32(80, 90, 110, 255), 6);
    for (int i = 0; i < visible; ++i) {
        int index = suggestScroll_ + i;
        const script::Suggestion& s = suggestions_[static_cast<size_t>(index)];
        float y = at.y + 4 + i * lineHeight_;
        if (index == completionIndex_)
            fg->AddRectFilled({at.x + 3, y}, {at.x + w - 3, y + lineHeight_}, IM_COL32(52, 92, 160, 255), 4);
        auto [letter, color] = kindBadge(s.kind);
        ImVec2 b0{at.x + 8, y + 2}, b1{at.x + 8 + badge, y + 2 + badge};
        fg->AddRectFilled(b0, b1, (color & 0x00FFFFFF) | (static_cast<ImU32>(60) << 24), 3);
        ImVec2 ls = f->CalcTextSizeA(fontSize_ * 0.8f, FLT_MAX, 0, letter);
        fg->AddText(f, fontSize_ * 0.8f, {b0.x + (badge - ls.x) * 0.5f, b0.y + (badge - ls.y) * 0.5f}, color, letter);
        float lx = b1.x + 8;
        fg->AddText(f, fontSize_, {lx, y + 1}, IM_COL32(232, 234, 240, 255), s.label.c_str());
        float lw = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, s.label.c_str()).x;
        float dw = f->CalcTextSizeA(fontSize_ * 0.9f, FLT_MAX, 0, s.detail.c_str()).x;
        fg->PushClipRect({lx + lw + 12, y}, {at.x + w - 8, y + lineHeight_}, true);
        fg->AddText(f, fontSize_ * 0.9f, {std::max(lx + lw + 16, at.x + w - 10 - dw), y + 2}, IM_COL32(140, 150, 168, 255),
                    s.detail.c_str());
        fg->PopClipRect();
    }
    if (static_cast<int>(suggestions_.size()) > visible) {
        // A scroll bar showing where in the list we are.
        float trackH = h - 8, thumbH = std::max(12.0f, trackH * visible / suggestions_.size());
        float thumbY = at.y + 4 + (trackH - thumbH) * suggestScroll_ / std::max<size_t>(1, suggestions_.size() - static_cast<size_t>(visible));
        fg->AddRectFilled({at.x + w - 5, thumbY}, {at.x + w - 2, thumbY + thumbH}, IM_COL32(120, 130, 150, 200), 2);
    }
    // What the chosen suggestion does.
    const script::Suggestion& sel = suggestions_[static_cast<size_t>(completionIndex_)];
    std::string doc = sel.doc;
    if (sel.kind == script::SuggestionKind::Function || sel.kind == script::SuggestionKind::Method ||
        sel.kind == script::SuggestionKind::Event)
        doc = sel.detail + (doc.empty() ? "" : "\n" + doc);
    if (!doc.empty()) {
        ImFont* ui = uiFont_ ? uiFont_ : ImGui::GetFont();
        float wrap = 300;
        ImVec2 ts = ui->CalcTextSizeA(ui->FontSize, FLT_MAX, wrap, doc.c_str());
        ImVec2 d0{popupMax_.x + 4, at.y};
        if (d0.x + wrap + 20 > display.x)
            d0.x = at.x - wrap - 24;
        ImVec2 d1{d0.x + ts.x + 20, d0.y + ts.y + 16};
        fg->AddRectFilled(d0, d1, IM_COL32(34, 38, 46, 250), 6);
        fg->AddRect(d0, d1, IM_COL32(80, 90, 110, 255), 6);
        fg->AddText(ui, ui->FontSize, {d0.x + 10, d0.y + 8}, IM_COL32(215, 220, 230, 255), doc.c_str(), nullptr, wrap);
    }
}

void CodeEditor::drawSignature(ImVec2 cursorScreen) {
    ImFont* f = font ? font : ImGui::GetFont();
    ImFont* ui = uiFont_ ? uiFont_ : ImGui::GetFont();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const std::string& label = sig_.label;
    float wrap = 460;
    ImVec2 labelSize = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, label.c_str());
    ImVec2 docSize = sig_.doc.empty() ? ImVec2{0, 0} : ui->CalcTextSizeA(ui->FontSize, FLT_MAX, wrap, sig_.doc.c_str());
    float w = std::max(labelSize.x, docSize.x) + 20;
    float h = labelSize.y + (sig_.doc.empty() ? 0 : docSize.y + 6) + 12;
    ImVec2 at{cursorScreen.x - 10, cursorScreen.y - h - 4};
    if (at.y < 4)
        at.y = cursorScreen.y + lineHeight_ + 4 + (completionOpen_ ? popupMax_.y - popupMin_.y + 4 : 0);
    at.x = std::clamp(at.x, 4.0f, std::max(4.0f, ImGui::GetIO().DisplaySize.x - w - 4));
    fg->AddRectFilled(at, {at.x + w, at.y + h}, IM_COL32(34, 38, 46, 250), 6);
    fg->AddRect(at, {at.x + w, at.y + h}, IM_COL32(80, 90, 110, 255), 6);
    // The value being typed stands out.
    int a = -1, b = -1;
    if (sig_.active >= 0 && sig_.active < static_cast<int>(sig_.params.size())) {
        a = sig_.params[static_cast<size_t>(sig_.active)].first;
        b = sig_.params[static_cast<size_t>(sig_.active)].second;
    }
    float x = at.x + 10, y = at.y + 6;
    auto part = [&](int from, int to, ImU32 col) {
        if (to <= from)
            return;
        fg->AddText(f, fontSize_, {x, y}, col, label.c_str() + from, label.c_str() + to);
        float pw = f->CalcTextSizeA(fontSize_, FLT_MAX, 0, label.c_str() + from, label.c_str() + to).x;
        if (col != IM_COL32(200, 205, 215, 255))
            fg->AddLine({x, y + labelSize.y}, {x + pw, y + labelSize.y}, col, 1.5f);
        x += pw;
    };
    int n = static_cast<int>(label.size());
    if (a < 0) {
        part(0, n, IM_COL32(200, 205, 215, 255));
    } else {
        part(0, a, IM_COL32(200, 205, 215, 255));
        part(a, b, IM_COL32(250, 204, 21, 255));
        part(b, n, IM_COL32(200, 205, 215, 255));
    }
    if (!sig_.doc.empty())
        fg->AddText(ui, ui->FontSize, {at.x + 10, y + labelSize.y + 6}, IM_COL32(170, 178, 192, 255), sig_.doc.c_str(), nullptr, wrap);
}

void CodeEditor::drawStatusBar() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Ln %d, Col %d", cursor_.line + 1, cursor_.col + 1);
    if (intel && language == CodeLanguage::Cpp) {
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("The compiler checks C and C++ when you build");
    } else if (intel) {
        int errors = 0, warnings = 0;
        for (auto& d : diags_)
            (d.error ? errors : warnings)++;
        ImGui::SameLine(0, 18);
        if (errors + warnings == 0) {
            ImGui::TextColored({0.45f, 0.8f, 0.55f, 1}, "No problems");
        } else {
            std::string label = (errors ? std::to_string(errors) + (errors == 1 ? " error" : " errors") : "") +
                                (errors && warnings ? ", " : "") +
                                (warnings ? std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings") : "");
            ImGui::PushStyleColor(ImGuiCol_Text, errors ? IM_COL32(248, 113, 113, 255) : IM_COL32(234, 179, 8, 255));
            if (ImGui::SmallButton((label + "##problems").c_str()))
                ImGui::OpenPopup("##problemlist");
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show the problems and jump to them");
        }
        if (ImGui::BeginPopup("##problemlist")) {
            for (size_t i = 0; i < diags_.size(); ++i) {
                const auto& d = diags_[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_Text, d.error ? IM_COL32(248, 113, 113, 255) : IM_COL32(234, 179, 8, 255));
                std::string row = "Line " + std::to_string(d.line + 1) + ":  " + d.message;
                if (ImGui::Selectable(row.c_str()))
                    gotoPosition(d.line, d.col);
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
    }
    const char* lang = language == CodeLanguage::Cpp ? "C / C++" : language == CodeLanguage::CSharp ? "C#"
                       : language == CodeLanguage::GDScript ? "GDScript" : language == CodeLanguage::Luau ? "Luau" : "EasyScript";
    char right[64];
    std::snprintf(right, sizeof right, zoom != 1.0f ? "%s   %d%%" : "%s", lang, static_cast<int>(std::round(zoom * 100)));
    float rw = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, ImGui::GetWindowContentRegionMax().x - rw - 4));
    ImGui::TextDisabled("%s", right);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+Space: suggestions   F12 or Ctrl+click: go to where it's made\n"
                          "Alt+Up/Down: move lines   Shift+Alt+Up/Down: copy lines   Ctrl+Shift+K: delete lines\n"
                          "Ctrl+/: comment   Ctrl+F: find   Ctrl+H: replace   Ctrl+G: go to line\n"
                          "Ctrl+wheel or Ctrl+=/-: text size (Ctrl+0 resets)");
}

} // namespace aven::editor
