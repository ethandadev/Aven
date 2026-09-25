#include "code_editor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aven::editor {

namespace {

const ImU32 kColText = IM_COL32(220, 223, 228, 255);
const ImU32 kColKeyword = IM_COL32(198, 120, 221, 255);
const ImU32 kColString = IM_COL32(152, 195, 121, 255);
const ImU32 kColNumber = IM_COL32(209, 154, 102, 255);
const ImU32 kColComment = IM_COL32(110, 118, 129, 255);
const ImU32 kColFunction = IM_COL32(97, 175, 239, 255);
const ImU32 kColBuiltin = IM_COL32(229, 192, 123, 255);
const ImU32 kColSelf = IM_COL32(224, 108, 117, 255);

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
            emit(0, s.size(), kColString);
            return;
        }
        emit(0, end + 3, kColString);
        i = end + 3;
        inTriple = false;
    }
    std::string prevWord;
    while (i < s.size()) {
        char c = s[i];
        if (c == '#') {
            emit(i, s.size(), kColComment);
            return;
        }
        if (s.compare(i, 3, "\"\"\"") == 0) {
            size_t end = s.find("\"\"\"", i + 3);
            if (end == std::string::npos) {
                emit(i, s.size(), kColString);
                inTriple = true;
                return;
            }
            emit(i, end + 3, kColString);
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
            emit(start, i, kColString);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == '_'))
                ++i;
            emit(start, i, kColNumber);
            continue;
        }
        if (isWordChar(c)) {
            size_t start = i;
            while (i < s.size() && isWordChar(s[i]))
                ++i;
            std::string word = s.substr(start, i - start);
            ImU32 color = kColText;
            if (isKeyword(word))
                color = kColKeyword;
            else if (word == "self" || word == "game")
                color = kColSelf;
            else if (prevWord == "def")
                color = kColFunction;
            else if (highlightWords.count(word))
                color = kColBuiltin;
            else if (i < s.size() && s[i] == '(')
                color = kColFunction;
            emit(start, i, color);
            prevWord = word;
            continue;
        }
        size_t start = i++;
        while (i < s.size() && !isWordChar(s[i]) && s[i] != '"' && s[i] != '\'' && s[i] != '#' &&
               !std::isdigit(static_cast<unsigned char>(s[i])))
            ++i;
        emit(start, i, kColText);
    }
}

bool CodeEditor::draw(const char* id, ImVec2 size) {
    bool changed = false;
    ImFont* f = font ? font : ImGui::GetFont();
    ImGui::PushFont(f);
    charWidth_ = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, "M").x;
    lineHeight_ = f->FontSize + 3.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 33, 39, 255));
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
            dl->AddRectFilled({winPos.x, y}, {winPos.x + winW, y + lineHeight_}, IM_COL32(255, 255, 255, 10));
        if (l + 1 == errorLine_) {
            dl->AddRectFilled({winPos.x, y}, {winPos.x + winW, y + lineHeight_}, IM_COL32(224, 70, 70, 55));
            dl->AddCircleFilled({origin.x + 6, y + lineHeight_ * 0.5f}, 4, IM_COL32(240, 80, 80, 255));
        }
        if (hasSelection() && l >= a.line && l <= b.line) {
            float x0 = l == a.line ? columnX(l, a.col) : 0;
            float x1 = l == b.line ? columnX(l, b.col) : columnX(l, static_cast<int>(lines_[static_cast<size_t>(l)].size())) + charWidth_;
            dl->AddRectFilled({origin.x + gutter + x0, y}, {origin.x + gutter + x1, y + lineHeight_}, IM_COL32(80, 120, 200, 110));
        }
        char num[16];
        std::snprintf(num, sizeof num, "%d", l + 1);
        float numW = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, num).x;
        dl->AddText(f, f->FontSize, {origin.x + gutter - numW - 10, y + 1},
                    l == cursor_.line ? IM_COL32(200, 205, 215, 255) : IM_COL32(95, 102, 115, 255), num);
        drawLine(dl, l, {origin.x + gutter, y + 1}, inTriple);
    }
    dl->AddLine({origin.x + gutter - 5, winPos.y}, {origin.x + gutter - 5, winPos.y + ImGui::GetWindowHeight()},
                IM_COL32(60, 65, 75, 255));

    // Cursor
    blink_ += ImGui::GetIO().DeltaTime;
    ImVec2 cursorScreen{origin.x + gutter + columnX(cursor_.line, cursor_.col), origin.y + cursor_.line * lineHeight_};
    if (focused_ && !readOnly && std::fmod(blink_, 1.0f) < 0.6f)
        dl->AddRectFilled(cursorScreen, {cursorScreen.x + 2, cursorScreen.y + lineHeight_}, IM_COL32(230, 230, 240, 255));
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
