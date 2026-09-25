#pragma once

#include "aven/script/lexer.h"
#include "aven/script/value.h"

#include <memory>
#include <string>
#include <vector>

namespace aven::script {

enum class ExprKind {
    Number,
    String,
    FString,
    True,
    False,
    None,
    Name,
    Self,
    List,
    Dict,
    Unary,   // op: Minus/Plus/Not, a
    Binary,  // op, a, b
    And,     // a and b
    Or,      // a or b
    Compare, // op (Eq, NotEq, Lt, ..., In, and NotIn encoded as Not), a, b
    Call,    // a(items..., kw...)
    Attr,    // a.sym
    Index,   // a[b]
    Slice,   // a[b:c] (b/c may be null)
    Ternary, // b if a else c
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    ExprKind kind;
    int line = 0;
    double number = 0;
    std::string text;
    Symbol sym = 0;
    Tok op = Tok::EndOfFile;
    bool notIn = false;   // Compare: "not in"
    bool isTuple = false; // List: written without brackets (a, b = ...)
    ExprPtr a, b, c;
    std::vector<ExprPtr> items;             // list items, call args, dict keys/values, f-string parts
    std::vector<Symbol> kwNames;            // Call: names of the last kwNames.size() items
    std::vector<std::string> literalParts;  // FString: text between expressions (items.size() + 1 entries)
    std::vector<std::string> formatSpecs;   // FString: ":.2f" style spec per expression

    Expr(ExprKind k, int l) : kind(k), line(l) {}
};

enum class StmtKind { Expression, Assign, AugAssign, If, While, For, Def, Return, Break, Continue, Pass, Global };

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
    StmtKind kind;
    int line = 0;
    ExprPtr expr;                 // value / condition / iterable / return value
    std::vector<ExprPtr> targets; // Assign (chained a = b = 1), AugAssign, For target
    Tok op = Tok::EndOfFile;      // AugAssign operator
    std::vector<StmtPtr> body;
    std::vector<StmtPtr> orelse;
    Symbol name = 0;              // Def
    std::vector<Symbol> params;   // Def
    std::vector<ExprPtr> defaults; // Def: defaults for the last defaults.size() params
    std::vector<Symbol> globals;  // Global
    int endLine = 0;

    Stmt(StmtKind k, int l) : kind(k), line(l) {}
};

struct Program {
    std::vector<StmtPtr> statements;
};

// Parses a whole script. Throws ScriptError with a line number on syntax errors.
Program parse(std::string_view source);

} // namespace aven::script
