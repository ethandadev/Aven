#include "aven/script/lexer.h"

#include "aven/script/errors.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace aven::script {

const char* tokenDescription(Tok t) {
    switch (t) {
    case Tok::Name: return "a name";
    case Tok::Number: return "a number";
    case Tok::String:
    case Tok::FString: return "text";
    case Tok::Newline: return "the end of the line";
    case Tok::Indent: return "an indented block";
    case Tok::Dedent: return "the end of the block";
    case Tok::EndOfFile: return "the end of the script";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBracket: return "'['";
    case Tok::RBracket: return "']'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::Comma: return "','";
    case Tok::Colon: return "':'";
    case Tok::Dot: return "'.'";
    case Tok::Assign: return "'='";
    default: return "a symbol";
    }
}

namespace {

const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> k{
        {"def", Tok::Def},         {"if", Tok::If},           {"elif", Tok::Elif},     {"else", Tok::Else},
        {"while", Tok::While},     {"for", Tok::For},         {"in", Tok::In},         {"return", Tok::Return},
        {"break", Tok::Break},     {"continue", Tok::Continue}, {"pass", Tok::Pass},   {"and", Tok::And},
        {"or", Tok::Or},           {"not", Tok::Not},         {"True", Tok::True},     {"False", Tok::False},
        {"None", Tok::None},       {"global", Tok::Global},
        // Friendly aliases so C, C# and JavaScript habits still work.
        {"true", Tok::True},       {"false", Tok::False},     {"none", Tok::None},     {"null", Tok::None},
    };
    return k;
}

class Lexer {
public:
    Lexer(std::string_view src, int firstLine) : s_(src), line_(firstLine) {}

    std::vector<Token> run() {
        indents_.push_back(0);
        atLineStart_ = true;
        while (true) {
            if (atLineStart_ && depth_ == 0) {
                if (!handleIndentation())
                    break;
            }
            skipSpacesAndComments();
            if (pos_ >= s_.size())
                break;
            char c = s_[pos_];
            if (c == '\n') {
                ++pos_;
                if (depth_ == 0) {
                    emitNewline();
                    atLineStart_ = true;
                }
                ++line_;
                lineStart_ = pos_;
                continue;
            }
            if (c == '\\' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '\n') {
                pos_ += 2; // explicit line continuation
                ++line_;
                lineStart_ = pos_;
                continue;
            }
            lexToken();
        }
        if (depth_ > 0) {
            if (openBracket_ == '{' && startsBlockKeyword(openBracketLineStart_))
                throw ScriptError("EasyScript uses a ':' at the end of the line and indentation instead of { } "
                                  "for blocks. For example:\n    if x > 3:\n        jump()",
                                  openBracketLine_);
            throw ScriptError("A '" + std::string(1, openBracket_) + "' opened on line " +
                                  std::to_string(openBracketLine_) + " is never closed.",
                              openBracketLine_);
        }
        emitNewline();
        while (indents_.size() > 1) {
            indents_.pop_back();
            push(Tok::Dedent);
        }
        push(Tok::EndOfFile);
        return std::move(tokens_);
    }

private:
    std::string_view s_;
    size_t pos_ = 0;
    int line_;
    size_t lineStart_ = 0;
    int depth_ = 0;
    char openBracket_ = 0;
    int openBracketLine_ = 0;
    size_t openBracketLineStart_ = 0;
    bool atLineStart_ = true;
    std::vector<int> indents_;
    std::vector<Token> tokens_;

    int column() const { return static_cast<int>(pos_ - lineStart_) + 1; }

    Token& push(Tok t, std::string text = {}) {
        Token tok;
        tok.type = t;
        tok.text = std::move(text);
        tok.line = line_;
        tok.column = column();
        tokens_.push_back(std::move(tok));
        return tokens_.back();
    }

    void emitNewline() {
        if (!tokens_.empty() && tokens_.back().type != Tok::Newline && tokens_.back().type != Tok::Indent &&
            tokens_.back().type != Tok::Dedent)
            push(Tok::Newline);
    }

    [[noreturn]] void error(const std::string& msg) { throw ScriptError(msg, line_); }

    void skipSpacesAndComments() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
            } else if (c == '#') {
                while (pos_ < s_.size() && s_[pos_] != '\n')
                    ++pos_;
            } else if (c == '\n' && depth_ > 0) {
                ++pos_;
                ++line_;
                lineStart_ = pos_;
            } else {
                break;
            }
        }
    }

    // Returns false at end of input.
    bool handleIndentation() {
        while (true) {
            int width = 0;
            size_t p = pos_;
            while (p < s_.size() && (s_[p] == ' ' || s_[p] == '\t' || s_[p] == '\r')) {
                if (s_[p] == ' ')
                    ++width;
                else if (s_[p] == '\t')
                    width += 4 - (width % 4);
                ++p;
            }
            if (p >= s_.size()) {
                pos_ = p;
                return false;
            }
            if (s_[p] == '\n' || s_[p] == '#') {
                // Blank or comment-only line: indentation doesn't matter.
                while (p < s_.size() && s_[p] != '\n')
                    ++p;
                if (p < s_.size())
                    ++p;
                pos_ = p;
                ++line_;
                lineStart_ = pos_;
                continue;
            }
            pos_ = p;
            atLineStart_ = false;
            if (width > indents_.back()) {
                indents_.push_back(width);
                push(Tok::Indent);
            } else {
                while (width < indents_.back()) {
                    indents_.pop_back();
                    push(Tok::Dedent);
                }
                if (width != indents_.back())
                    error("This line's indentation doesn't line up with any line above it. "
                          "Make it start at the same position as the block it belongs to.");
            }
            return true;
        }
    }

    void lexToken() {
        char c = s_[pos_];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            if ((c == 'f' || c == 'F') && pos_ + 1 < s_.size() && (s_[pos_ + 1] == '"' || s_[pos_ + 1] == '\'')) {
                ++pos_;
                lexString(true);
                return;
            }
            size_t start = pos_;
            while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
                ++pos_;
            std::string word(s_.substr(start, pos_ - start));
            auto it = keywords().find(word);
            Token& t = push(it != keywords().end() ? it->second : Tok::Name, word);
            t.column = static_cast<int>(start - lineStart_) + 1;
            return;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && pos_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_ + 1])))) {
            lexNumber();
            return;
        }
        if (c == '"' || c == '\'') {
            lexString(false);
            return;
        }
        lexSymbol();
    }

    void lexNumber() {
        size_t start = pos_;
        double value;
        if (s_[pos_] == '0' && pos_ + 1 < s_.size() && (s_[pos_ + 1] == 'x' || s_[pos_ + 1] == 'X')) {
            pos_ += 2;
            while (pos_ < s_.size() && std::isxdigit(static_cast<unsigned char>(s_[pos_])))
                ++pos_;
            value = static_cast<double>(std::strtoull(std::string(s_.substr(start + 2, pos_ - start - 2)).c_str(),
                                                      nullptr, 16));
        } else {
            while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
                ++pos_;
            if (pos_ < s_.size() && s_[pos_] == '.' &&
                !(pos_ + 1 < s_.size() && std::isalpha(static_cast<unsigned char>(s_[pos_ + 1])))) {
                ++pos_;
                while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_])))
                    ++pos_;
            }
            if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
                size_t save = pos_;
                ++pos_;
                if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-'))
                    ++pos_;
                if (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_])))
                        ++pos_;
                } else {
                    pos_ = save;
                }
            }
            std::string text;
            for (char ch : s_.substr(start, pos_ - start))
                if (ch != '_')
                    text += ch;
            value = std::strtod(text.c_str(), nullptr);
        }
        if (pos_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
            error("Names can't start with a number. Try putting the number at the end, like 'player2'.");
        Token& t = push(Tok::Number, std::string(s_.substr(start, pos_ - start)));
        t.number = value;
        t.column = static_cast<int>(start - lineStart_) + 1;
    }

    void lexString(bool isFormat) {
        char quote = s_[pos_];
        int startLine = line_;
        bool triple = s_.substr(pos_, 3) == std::string(3, quote);
        pos_ += triple ? 3 : 1;
        std::string out;
        while (true) {
            if (pos_ >= s_.size())
                throw ScriptError("This text is missing its closing " + std::string(triple ? 3 : 1, quote) + ".",
                                  startLine);
            char c = s_[pos_];
            if (triple && s_.substr(pos_, 3) == std::string(3, quote)) {
                pos_ += 3;
                break;
            }
            if (!triple && c == quote) {
                ++pos_;
                break;
            }
            if (c == '\n') {
                if (!triple)
                    throw ScriptError("This text is missing its closing " + std::string(1, quote) +
                                          " before the end of the line.",
                                      startLine);
                ++line_;
                lineStart_ = pos_ + 1;
            }
            if (c == '\\' && pos_ + 1 < s_.size()) {
                char e = s_[pos_ + 1];
                pos_ += 2;
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '0': out += '\0'; break;
                case '\\': out += '\\'; break;
                case '\'': out += '\''; break;
                case '"': out += '"'; break;
                case '\n':
                    ++line_;
                    lineStart_ = pos_;
                    break;
                // Keep braces escaped so f-string parsing can tell them apart.
                case '{': out += isFormat ? "{{" : "{"; break;
                case '}': out += isFormat ? "}}" : "}"; break;
                default:
                    out += '\\';
                    out += e;
                }
                continue;
            }
            out += c;
            ++pos_;
        }
        Token& t = push(isFormat ? Tok::FString : Tok::String, std::move(out));
        t.line = startLine;
    }

    void open(char c) {
        if (depth_ == 0) {
            openBracket_ = c;
            openBracketLine_ = line_;
            openBracketLineStart_ = lineStart_;
        }
        ++depth_;
    }

    bool startsBlockKeyword(size_t lineStart) const {
        size_t p = lineStart;
        while (p < s_.size() && (s_[p] == ' ' || s_[p] == '\t'))
            ++p;
        for (const char* kw : {"if ", "elif ", "else", "while ", "for ", "def "})
            if (s_.substr(p, std::strlen(kw)) == kw)
                return true;
        return false;
    }

    void close(char c) {
        if (depth_ == 0)
            error(std::string("There is a '") + c + "' here without a matching opening bracket.");
        --depth_;
    }

    void lexSymbol() {
        char c = s_[pos_];
        char n = pos_ + 1 < s_.size() ? s_[pos_ + 1] : '\0';
        auto two = [&](Tok t, const char* text) {
            push(t, text);
            pos_ += 2;
        };
        auto one = [&](Tok t) {
            push(t, std::string(1, c));
            ++pos_;
        };
        switch (c) {
        case '(': open(c); one(Tok::LParen); return;
        case ')': close(c); one(Tok::RParen); return;
        case '[': open(c); one(Tok::LBracket); return;
        case ']': close(c); one(Tok::RBracket); return;
        case '{': open(c); one(Tok::LBrace); return;
        case '}': close(c); one(Tok::RBrace); return;
        case ',': one(Tok::Comma); return;
        case ':': one(Tok::Colon); return;
        case '.': one(Tok::Dot); return;
        case ';': one(Tok::Semicolon); return;
        case '+':
            if (n == '=') return two(Tok::PlusAssign, "+=");
            if (n == '+') return two(Tok::PlusPlus, "++");
            return one(Tok::Plus);
        case '-':
            if (n == '=') return two(Tok::MinusAssign, "-=");
            if (n == '-') return two(Tok::MinusMinus, "--");
            return one(Tok::Minus);
        case '*':
            if (n == '*') return two(Tok::StarStar, "**");
            if (n == '=') return two(Tok::StarAssign, "*=");
            return one(Tok::Star);
        case '/':
            if (n == '/') return two(Tok::SlashSlash, "//");
            if (n == '=') return two(Tok::SlashAssign, "/=");
            return one(Tok::Slash);
        case '%':
            if (n == '=') return two(Tok::PercentAssign, "%=");
            return one(Tok::Percent);
        case '=':
            if (n == '=') return two(Tok::Eq, "==");
            return one(Tok::Assign);
        case '!':
            if (n == '=') return two(Tok::NotEq, "!=");
            push(Tok::Not, "!");
            ++pos_;
            return;
        case '<':
            if (n == '=') return two(Tok::LtEq, "<=");
            return one(Tok::Lt);
        case '>':
            if (n == '=') return two(Tok::GtEq, ">=");
            return one(Tok::Gt);
        case '&':
            if (n == '&') return two(Tok::And, "&&");
            break;
        case '|':
            if (n == '|') return two(Tok::Or, "||");
            break;
        default: break;
        }
        auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x80) {
            // A character from outside plain ASCII, often pasted from a web page or a chat.
            size_t len = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
            std::string ch(s_.substr(pos_, len));
            if (ch == "\xE2\x80\x9C" || ch == "\xE2\x80\x9D" || ch == "\xE2\x80\x98" || ch == "\xE2\x80\x99")
                error("The curly quote " + ch + " isn't understood. Use straight quotes instead: \" or '. "
                      "(Curly quotes sneak in when code is copied from a web page or a document.)");
            if (ch == "\xE2\x80\x93" || ch == "\xE2\x80\x94")
                error("The long dash " + ch + " isn't a minus sign. Type - instead.");
            if (ch == "\xC3\x97")
                error("For multiplying, use * instead of " + ch + ".");
            error("I don't understand the character '" + ch + "' here. Letters with accents and symbols can go inside "
                  "\"text\", but names and code use plain letters, numbers and _.");
        }
        error(std::string("I don't understand the character '") + c + "' here.");
    }
};

} // namespace

std::vector<Token> tokenize(std::string_view source, int firstLine) {
    return Lexer(source, firstLine).run();
}

} // namespace aven::script
