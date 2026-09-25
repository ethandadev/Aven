#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aven::script {

enum class Tok {
    Name,
    Number,
    String,
    FString,
    Newline,
    Indent,
    Dedent,
    EndOfFile,
    // keywords
    Def,
    If,
    Elif,
    Else,
    While,
    For,
    In,
    Return,
    Break,
    Continue,
    Pass,
    And,
    Or,
    Not,
    True,
    False,
    None,
    Global,
    // punctuation
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Dot,
    Semicolon,
    Plus,
    Minus,
    Star,
    Slash,
    SlashSlash,
    Percent,
    StarStar,
    Assign,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
    Eq,
    NotEq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    PlusPlus,
    MinusMinus,
};

struct Token {
    Tok type = Tok::EndOfFile;
    std::string text; // identifier name, string contents, or source text
    double number = 0;
    int line = 1;
    int column = 1;
};

const char* tokenDescription(Tok t);

// Converts source text into tokens, turning indentation into Indent/Dedent tokens.
// Throws ScriptError with a line number on invalid input.
std::vector<Token> tokenize(std::string_view source, int firstLine = 1);

} // namespace aven::script
