#include "code_editor.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aven::editor {

namespace {


bool isKeyword(const std::string& w) {
    static const char* kw[] = {"def",   "if",    "elif", "else", "while", "for",   "in",   "return", "break",
                               "continue", "pass", "and", "or",  "not",   "True",  "False", "None", "global",
                               "true",  "false", "null"};
    for (const char* k : kw)
        if (w == k)
            return true;
    return false;
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

    if (completionOpen_) {
        if (pressed(ImGuiKey_DownArrow)) {
            completionIndex_ = (completionIndex_ + 1) % static_cast<int>(matches_.size());
            return;
        }
        if (pressed(ImGuiKey_UpArrow)) {
            completionIndex_ = (completionIndex_ + static_cast<int>(matches_.size()) - 1) % static_cast<int>(matches_.size());
            return;
        }
        if (pressed(ImGuiKey_Escape)) {
            completionOpen_ = false;
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
    if (changed && !io.InputQueueCharacters.empty())
        updateCompletions();
}

float CodeEditor::columnX(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    ImFont* f = font ? font : ImGui::GetFont();
    return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, s.c_str(), s.c_str() + std::min<size_t>(static_cast<size_t>(col), s.size())).x;
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
    float size = f->FontSize;
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
    while (i < s.size()) {
        char c = s[i];
        if (c == '#') {
            emit(i, s.size(), CodeEditor::palette.comment);
            return;
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
            if (isKeyword(word))
                color = CodeEditor::palette.keyword;
            else if (word == "self" || word == "game")
                color = CodeEditor::palette.self;
            else if (prevWord == "def")
                color = CodeEditor::palette.function;
            else if (highlightWords.count(word))
                color = CodeEditor::palette.builtin;
            else if (i < s.size() && s[i] == '(')
                color = CodeEditor::palette.function;
            emit(start, i, color);
            prevWord = word;
            continue;
        }
        size_t start = i++;
        while (i < s.size() && !isWordChar(s[i]) && s[i] != '"' && s[i] != '\'' && s[i] != '#' &&
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

bool CodeEditor::draw(const char* id, ImVec2 size) {
    bool changed = false;
    if (findOpen_ || gotoOpen_) {
        float before = ImGui::GetCursorPosY();
        changed |= drawFindBar();
        size.y -= ImGui::GetCursorPosY() - before;
    }
    ImFont* f = font ? font : ImGui::GetFont();
    ImGui::PushFont(f);
    charWidth_ = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, "M").x;
    lineHeight_ = f->FontSize + 3.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.background);
    ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float gutter = charWidth_ * (std::to_string(lines_.size()).size() + 2) + 8;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float maxWidth = 0;
    for (size_t l = 0; l < lines_.size(); ++l)
        maxWidth = std::max(maxWidth, static_cast<float>(lines_[l].size()));
    ImVec2 contentSize{gutter + maxWidth * charWidth_ + 200, lines_.size() * lineHeight_ + ImGui::GetWindowHeight() * 0.5f};
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
    if (ImGui::IsItemActivated()) {
        Pos p = mouseToPos();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            anchor_ = {p.line, wordStart(p.line, p.col)};
            cursor_ = {p.line, wordEnd(p.line, p.col)};
        } else {
            moveCursor(p, ImGui::GetIO().KeyShift);
            dragging_ = true;
        }
        completionOpen_ = false;
    }
    if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        cursor_ = mouseToPos();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        dragging_ = false;

    if (focused_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        handleKeys(changed);
        handleTyping(changed);
    }
    if (gotoLine_ > 0) {
        cursor_ = anchor_ = clamp({gotoLine_ - 1, 0});
        focused_ = true;
        scrollToCursor_ = true;
        gotoLine_ = -1;
        ImGui::SetScrollY(std::max(0.0f, cursor_.line * lineHeight_ - ImGui::GetWindowHeight() * 0.4f));
    }

    // Visible range
    float scrollY = ImGui::GetScrollY();
    int first = std::max(0, static_cast<int>(scrollY / lineHeight_) - 1);
    int last = std::min(static_cast<int>(lines_.size()), first + static_cast<int>(ImGui::GetWindowHeight() / lineHeight_) + 3);
    ImVec2 winPos = ImGui::GetWindowPos();
    float winW = ImGui::GetWindowWidth();

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
    for (int l = first; l < last; ++l) {
        float y = origin.y + l * lineHeight_;
        if (l == cursor_.line && focused_ && !hasSelection())
            dl->AddRectFilled({winPos.x, y}, {winPos.x + winW, y + lineHeight_}, palette.currentLine);
        if (l + 1 == errorLine_) {
            dl->AddRectFilled({winPos.x, y}, {winPos.x + winW, y + lineHeight_}, IM_COL32(224, 70, 70, 55));
            dl->AddCircleFilled({origin.x + 6, y + lineHeight_ * 0.5f}, 4, IM_COL32(240, 80, 80, 255));
        }
        if (findOpen_ && !findText_.empty()) {
            const std::string& line = lines_[static_cast<size_t>(l)];
            for (size_t c = 0; c + findText_.size() <= line.size(); ++c)
                if (matchesAt(line, c))
                    dl->AddRect({origin.x + gutter + columnX(l, static_cast<int>(c)), y},
                                {origin.x + gutter + columnX(l, static_cast<int>(c + findText_.size())), y + lineHeight_},
                                IM_COL32(250, 200, 60, 200), 2);
        }
        if (hasSelection() && l >= a.line && l <= b.line) {
            float x0 = l == a.line ? columnX(l, a.col) : 0;
            float x1 = l == b.line ? columnX(l, b.col) : columnX(l, static_cast<int>(lines_[static_cast<size_t>(l)].size())) + charWidth_;
            dl->AddRectFilled({origin.x + gutter + x0, y}, {origin.x + gutter + x1, y + lineHeight_}, palette.selection);
        }
        char num[16];
        std::snprintf(num, sizeof num, "%d", l + 1);
        float numW = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, num).x;
        dl->AddText(f, f->FontSize, {origin.x + gutter - numW - 10, y + 1},
                    l == cursor_.line ? palette.currentLineNumber : palette.lineNumber, num);
        drawLine(dl, l, {origin.x + gutter, y + 1}, inTriple);
    }
    dl->AddLine({origin.x + gutter - 5, winPos.y}, {origin.x + gutter - 5, winPos.y + ImGui::GetWindowHeight()},
                palette.gutterLine);

    // Cursor
    blink_ += ImGui::GetIO().DeltaTime;
    ImVec2 cursorScreen{origin.x + gutter + columnX(cursor_.line, cursor_.col), origin.y + cursor_.line * lineHeight_};
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

    // Error tooltip when hovering the error line.
    if (errorLine_ > 0 && hovered) {
        ImVec2 m = ImGui::GetMousePos();
        int hoverLine = static_cast<int>((m.y - origin.y) / lineHeight_);
        if (hoverLine + 1 == errorLine_) {
            ImGui::BeginTooltip();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 150, 150, 255));
            ImGui::PushTextWrapPos(420);
            ImGui::TextUnformatted(errorMessage_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }
    }

    // Autocomplete popup
    if (completionOpen_ && focused_ && !matches_.empty()) {
        ImVec2 at{cursorScreen.x, cursorScreen.y + lineHeight_ + 2};
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float w = 0;
        for (auto* m : matches_)
            w = std::max(w, f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, (m->word + "   " + m->detail).c_str()).x);
        w = std::min(w + 16, 520.0f);
        float h = matches_.size() * lineHeight_ + 8;
        fg->AddRectFilled(at, {at.x + w, at.y + h}, IM_COL32(40, 44, 52, 245), 4);
        fg->AddRect(at, {at.x + w, at.y + h}, IM_COL32(90, 100, 120, 255), 4);
        for (size_t i = 0; i < matches_.size(); ++i) {
            float y = at.y + 4 + i * lineHeight_;
            if (static_cast<int>(i) == completionIndex_)
                fg->AddRectFilled({at.x + 2, y}, {at.x + w - 2, y + lineHeight_}, IM_COL32(60, 100, 170, 255), 3);
            fg->AddText(f, f->FontSize, {at.x + 8, y + 1}, IM_COL32(230, 230, 235, 255), matches_[i]->word.c_str());
            float ww = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, matches_[i]->word.c_str()).x;
            fg->PushClipRect(at, {at.x + w - 4, at.y + h});
            fg->AddText(f, f->FontSize, {at.x + 8 + ww + 12, y + 1}, IM_COL32(140, 150, 165, 255), matches_[i]->detail.c_str());
            fg->PopClipRect();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (changed && errorLine_ > 0)
        errorLine_ = 0; // the error may be fixed; it's re-checked when saved
    return changed;
}

} // namespace aven::editor
