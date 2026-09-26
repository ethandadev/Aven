#include "aven/script/ast.h"
#include "aven/script/errors.h"

#include <unordered_set>

namespace aven::script {

namespace {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

    Program program() {
        Program p;
        while (!check(Tok::EndOfFile)) {
            if (match(Tok::Newline) || match(Tok::Semicolon))
                continue;
            if (check(Tok::Indent))
                error("This line is indented, but nothing above it (like 'if', 'for' or 'def') starts a block. "
                      "Remove the spaces at the start of the line.");
            statement(p.statements);
        }
        return p;
    }

    ExprPtr singleExpression() {
        while (match(Tok::Newline)) {
        }
        ExprPtr e = expression();
        while (match(Tok::Newline)) {
        }
        if (!check(Tok::EndOfFile))
            error("Unexpected " + describe(peek()) + " inside '{ }'.");
        return e;
    }

private:
    std::vector<Token> t_;
    size_t p_ = 0;
    int functionDepth_ = 0;
    int loopDepth_ = 0;

    const Token& peek(size_t ahead = 0) const { return t_[std::min(p_ + ahead, t_.size() - 1)]; }
    const Token& previous() const { return t_[p_ - 1]; }
    bool check(Tok type) const { return peek().type == type; }
    const Token& advance() {
        if (p_ < t_.size() - 1)
            ++p_;
        return previous();
    }
    bool match(Tok type) {
        if (!check(type))
            return false;
        advance();
        return true;
    }

    [[noreturn]] void error(const std::string& msg, int line = 0) {
        throw ScriptError(msg, line ? line : peek().line);
    }

    static std::string describe(const Token& tok) {
        switch (tok.type) {
        case Tok::Name: return "'" + tok.text + "'";
        case Tok::Number: return "the number " + tok.text;
        case Tok::String: return "the text \"" + tok.text + "\"";
        default: return tok.text.empty() ? tokenDescription(tok.type) : "'" + tok.text + "'";
        }
    }

    const Token& expect(Tok type, const std::string& message) {
        if (!check(type))
            error(message);
        return advance();
    }

    void expectColon(const char* what) {
        if (check(Tok::LBrace))
            error(std::string("Expected ':' at the end of the '") + what +
                  "' line. EasyScript uses a colon and indentation instead of { }.");
        if (check(Tok::Assign) && std::string(what) != "def")
            error("Use '==' to compare two values. A single '=' stores a value in a variable.");
        expect(Tok::Colon, std::string("Expected ':' at the end of the '") + what + "' line.");
    }

    void endOfStatement() {
        if (match(Tok::Semicolon)) {
            // Semicolons are optional (a C habit); allow one at the end or between statements.
            if (!check(Tok::Newline) && !check(Tok::EndOfFile) && !check(Tok::Dedent))
                return;
        }
        if (check(Tok::PlusPlus) || check(Tok::MinusMinus))
            error(std::string("EasyScript doesn't have '") + peek().text + "'. Use 'x " +
                  (check(Tok::PlusPlus) ? "+= 1" : "-= 1") + "' instead.");
        if (check(Tok::Dedent) || check(Tok::EndOfFile))
            return;
        if (!match(Tok::Newline)) {
            if (check(Tok::Assign))
                error("You can only store values in names, attributes (like self.x) or list items.");
            error("Unexpected " + describe(peek()) + ". Each instruction should go on its own line.");
        }
    }

    // --- statements

    void statement(std::vector<StmtPtr>& out) {
        const Token& tok = peek();
        switch (tok.type) {
        case Tok::If: out.push_back(ifStatement()); return;
        case Tok::While: out.push_back(whileStatement()); return;
        case Tok::For: out.push_back(forStatement()); return;
        case Tok::Def: out.push_back(defStatement()); return;
        case Tok::Else:
        case Tok::Elif:
            error(std::string("This '") + tok.text + "' doesn't have a matching 'if' above it at the same indentation.");
        default: break;
        }
        // Friendly hints for other languages' function syntax.
        if (tok.type == Tok::Name && (tok.text == "function" || tok.text == "func" || tok.text == "fn" ||
                                      tok.text == "void" || tok.text == "fun") &&
            peek(1).type == Tok::Name)
            error("Use 'def' to create a function in EasyScript, like: def " + peek(1).text + "():");
        simpleStatements(out);
    }

    void simpleStatements(std::vector<StmtPtr>& out) {
        out.push_back(simpleStatement());
        while (check(Tok::Semicolon) && peek(1).type != Tok::Newline && peek(1).type != Tok::EndOfFile) {
            advance();
            out.push_back(simpleStatement());
        }
        endOfStatement();
    }

    StmtPtr simpleStatement() {
        int line = peek().line;
        if (match(Tok::Pass))
            return std::make_unique<Stmt>(StmtKind::Pass, line);
        if (match(Tok::Break)) {
            if (loopDepth_ == 0)
                error("'break' can only be used inside a 'for' or 'while' loop.", line);
            return std::make_unique<Stmt>(StmtKind::Break, line);
        }
        if (match(Tok::Continue)) {
            if (loopDepth_ == 0)
                error("'continue' can only be used inside a 'for' or 'while' loop.", line);
            return std::make_unique<Stmt>(StmtKind::Continue, line);
        }
        if (match(Tok::Return)) {
            if (functionDepth_ == 0)
                error("'return' can only be used inside a function (a 'def').", line);
            auto s = std::make_unique<Stmt>(StmtKind::Return, line);
            if (!check(Tok::Newline) && !check(Tok::Semicolon) && !check(Tok::EndOfFile) && !check(Tok::Dedent))
                s->expr = expressionList();
            return s;
        }
        if (match(Tok::Global)) {
            auto s = std::make_unique<Stmt>(StmtKind::Global, line);
            do {
                s->globals.push_back(intern(expect(Tok::Name, "Expected a variable name after 'global'.").text));
            } while (match(Tok::Comma));
            return s;
        }

        ExprPtr first = expressionList();
        if (check(Tok::Assign)) {
            auto s = std::make_unique<Stmt>(StmtKind::Assign, line);
            s->targets.push_back(std::move(first));
            while (match(Tok::Assign)) {
                if (check(Tok::Assign))
                    error("Use '==' to compare. A single '=' stores a value.");
                ExprPtr next = expressionList();
                s->targets.push_back(std::move(next));
            }
            s->expr = std::move(s->targets.back());
            s->targets.pop_back();
            for (auto& target : s->targets)
                checkTarget(*target);
            return s;
        }
        if (check(Tok::PlusAssign) || check(Tok::MinusAssign) || check(Tok::StarAssign) ||
            check(Tok::SlashAssign) || check(Tok::PercentAssign)) {
            Tok op = advance().type;
            auto s = std::make_unique<Stmt>(StmtKind::AugAssign, line);
            checkTarget(*first);
            if (first->kind == ExprKind::List)
                error("You can't use '" + previous().text + "' on several variables at once.");
            s->op = op;
            s->targets.push_back(std::move(first));
            s->expr = expression();
            return s;
        }
        if (check(Tok::PlusPlus) || check(Tok::MinusMinus))
            endOfStatement();
        auto s = std::make_unique<Stmt>(StmtKind::Expression, line);
        s->expr = std::move(first);
        return s;
    }

    void checkTarget(const Expr& e) {
        switch (e.kind) {
        case ExprKind::Name:
        case ExprKind::Attr:
        case ExprKind::Index: return;
        case ExprKind::List:
            for (auto& item : e.items)
                checkTarget(*item);
            return;
        case ExprKind::Self: error("You can't replace 'self'. To change a property, write something like self.x = 5", e.line);
        case ExprKind::Call: error("You can't store a value in a function call. Did you mean '==' to compare?", e.line);
        default: error("You can only store values in names, attributes (like self.x) or list items.", e.line);
        }
    }

    std::vector<StmtPtr> block(const char* what) {
        std::vector<StmtPtr> body;
        if (!check(Tok::Newline)) {
            // One-line form: if x: y = 1
            simpleStatements(body);
            return body;
        }
        advance();
        if (!match(Tok::Indent))
            error(std::string("Expected an indented block after the '") + what +
                  "' line. Indent the next line with 4 spaces.");
        while (!check(Tok::Dedent) && !check(Tok::EndOfFile)) {
            if (match(Tok::Newline) || match(Tok::Semicolon))
                continue;
            statement(body);
        }
        match(Tok::Dedent);
        return body;
    }

    StmtPtr ifStatement() {
        auto s = std::make_unique<Stmt>(StmtKind::If, advance().line);
        s->expr = expression();
        expectColon("if");
        s->body = block("if");
        if (check(Tok::Elif) || (check(Tok::Else) && peek(1).type == Tok::If)) {
            // "else if" is accepted as a friendly alias for "elif".
            if (check(Tok::Else))
                advance();
            s->orelse.push_back(ifStatement());
        } else if (match(Tok::Else)) {
            expectColon("else");
            s->orelse = block("else");
        }
        return s;
    }

    StmtPtr whileStatement() {
        auto s = std::make_unique<Stmt>(StmtKind::While, advance().line);
        s->expr = expression();
        expectColon("while");
        ++loopDepth_;
        s->body = block("while");
        --loopDepth_;
        return s;
    }

    StmtPtr forStatement() {
        auto s = std::make_unique<Stmt>(StmtKind::For, advance().line);
        if (check(Tok::LParen))
            error("Write the loop like: for i in range(10):");
        auto target = std::make_unique<Expr>(ExprKind::List, peek().line);
        target->isTuple = true;
        do {
            const Token& name = expect(Tok::Name, "Expected a variable name after 'for', like: for item in items:");
            auto n = std::make_unique<Expr>(ExprKind::Name, name.line);
            n->col = name.column;
            n->text = name.text;
            n->sym = intern(name.text);
            target->items.push_back(std::move(n));
        } while (match(Tok::Comma));
        if (target->items.size() == 1)
            s->targets.push_back(std::move(target->items[0]));
        else
            s->targets.push_back(std::move(target));
        if (!match(Tok::In))
            error("Expected 'in' after the loop variable, like: for i in range(10):");
        s->expr = expression();
        expectColon("for");
        ++loopDepth_;
        s->body = block("for");
        --loopDepth_;
        return s;
    }

    StmtPtr defStatement() {
        int line = advance().line;
        if (functionDepth_ > 0)
            error("Functions must be created at the top level of the script, not inside another function.", line);
        auto s = std::make_unique<Stmt>(StmtKind::Def, line);
        const Token& name = expect(Tok::Name, "Expected a function name after 'def', like: def jump():");
        s->name = intern(name.text);
        expect(Tok::LParen, "Expected '(' after the function name, like: def " + name.text + "():");
        std::unordered_set<Symbol> seen;
        if (!check(Tok::RParen)) {
            do {
                if (check(Tok::RParen))
                    break;
                const Token& p = expect(Tok::Name, "Expected a parameter name.");
                if (p.text == "self")
                    error("You don't need 'self' as a parameter; it's always available inside functions.");
                Symbol sym = intern(p.text);
                if (!seen.insert(sym).second)
                    error("The parameter '" + p.text + "' is listed twice.");
                s->params.push_back(sym);
                if (match(Tok::Assign))
                    s->defaults.push_back(expression());
                else if (!s->defaults.empty())
                    error("Parameters with default values must come after the ones without.");
            } while (match(Tok::Comma));
        }
        expect(Tok::RParen, "Expected ')' to close the parameter list.");
        expectColon("def");
        ++functionDepth_;
        int savedLoops = loopDepth_;
        loopDepth_ = 0;
        s->body = block("def");
        loopDepth_ = savedLoops;
        --functionDepth_;
        s->endLine = previous().line;
        return s;
    }

    // --- expressions

    // a, b, c  -> tuple (a list) when there's a comma
    ExprPtr expressionList() {
        ExprPtr first = expression();
        if (!check(Tok::Comma))
            return first;
        auto list = std::make_unique<Expr>(ExprKind::List, first->line);
        list->isTuple = true;
        list->items.push_back(std::move(first));
        while (match(Tok::Comma)) {
            if (check(Tok::Assign) || check(Tok::Newline))
                break;
            list->items.push_back(expression());
        }
        return list;
    }

    ExprPtr expression() {
        ExprPtr e = orExpr();
        if (check(Tok::If) && peek().line == e->line) {
            int line = advance().line;
            auto t = std::make_unique<Expr>(ExprKind::Ternary, line);
            t->b = std::move(e);
            t->a = orExpr();
            expect(Tok::Else, "Expected 'else' in the one-line if, like: a if condition else b");
            t->c = expression();
            return t;
        }
        return e;
    }

    ExprPtr orExpr() {
        ExprPtr e = andExpr();
        while (check(Tok::Or)) {
            int line = advance().line;
            auto n = std::make_unique<Expr>(ExprKind::Or, line);
            n->a = std::move(e);
            n->b = andExpr();
            e = std::move(n);
        }
        return e;
    }

    ExprPtr andExpr() {
        ExprPtr e = notExpr();
        while (check(Tok::And)) {
            int line = advance().line;
            auto n = std::make_unique<Expr>(ExprKind::And, line);
            n->a = std::move(e);
            n->b = notExpr();
            e = std::move(n);
        }
        return e;
    }

    ExprPtr notExpr() {
        if (check(Tok::Not)) {
            int line = advance().line;
            auto n = std::make_unique<Expr>(ExprKind::Unary, line);
            n->op = Tok::Not;
            n->a = notExpr();
            return n;
        }
        return comparison();
    }

    static bool isComparison(Tok t) {
        return t == Tok::Eq || t == Tok::NotEq || t == Tok::Lt || t == Tok::Gt || t == Tok::LtEq ||
               t == Tok::GtEq || t == Tok::In;
    }

    ExprPtr comparison() {
        ExprPtr e = sum();
        bool compared = false;
        while (isComparison(peek().type) || (check(Tok::Not) && peek(1).type == Tok::In)) {
            if (compared)
                error("Comparisons can't be chained here. Write it as two checks joined with 'and', like: "
                      "0 < x and x < 10");
            compared = true;
            auto n = std::make_unique<Expr>(ExprKind::Compare, peek().line);
            if (match(Tok::Not)) {
                advance(); // in
                n->op = Tok::In;
                n->notIn = true;
            } else {
                n->op = advance().type;
            }
            n->a = std::move(e);
            n->b = sum();
            e = std::move(n);
        }
        if (check(Tok::Assign) && compared)
            error("Use '==' to compare. A single '=' stores a value.");
        return e;
    }

    ExprPtr sum() {
        ExprPtr e = term();
        while (check(Tok::Plus) || check(Tok::Minus)) {
            const Token& op = advance();
            auto n = std::make_unique<Expr>(ExprKind::Binary, op.line);
            n->op = op.type;
            n->a = std::move(e);
            n->b = term();
            e = std::move(n);
        }
        return e;
    }

    ExprPtr term() {
        ExprPtr e = unary();
        while (check(Tok::Star) || check(Tok::Slash) || check(Tok::SlashSlash) || check(Tok::Percent)) {
            const Token& op = advance();
            auto n = std::make_unique<Expr>(ExprKind::Binary, op.line);
            n->op = op.type;
            n->a = std::move(e);
            n->b = unary();
            e = std::move(n);
        }
        return e;
    }

    ExprPtr unary() {
        if (check(Tok::Minus) || check(Tok::Plus)) {
            const Token& op = advance();
            auto n = std::make_unique<Expr>(ExprKind::Unary, op.line);
            n->op = op.type;
            n->a = unary();
            // Fold negative number literals so defaults like `speed = -5` show in the inspector.
            if (n->a->kind == ExprKind::Number) {
                n->a->number = op.type == Tok::Minus ? -n->a->number : n->a->number;
                return std::move(n->a);
            }
            return n;
        }
        return power();
    }

    ExprPtr power() {
        ExprPtr e = postfix();
        if (check(Tok::StarStar)) {
            const Token& op = advance();
            auto n = std::make_unique<Expr>(ExprKind::Binary, op.line);
            n->op = Tok::StarStar;
            n->a = std::move(e);
            n->b = unary();
            return n;
        }
        return e;
    }

    ExprPtr postfix() {
        ExprPtr e = atom();
        while (true) {
            if (check(Tok::LParen)) {
                int line = advance().line;
                auto call = std::make_unique<Expr>(ExprKind::Call, line);
                call->a = std::move(e);
                arguments(*call);
                e = std::move(call);
            } else if (check(Tok::Dot)) {
                int line = advance().line;
                const Token& name = expect(Tok::Name, "Expected a name after '.'");
                auto attr = std::make_unique<Expr>(ExprKind::Attr, line);
                attr->col = name.column;
                attr->a = std::move(e);
                attr->text = name.text;
                attr->sym = intern(name.text);
                e = std::move(attr);
            } else if (check(Tok::LBracket)) {
                int line = advance().line;
                ExprPtr start, stop;
                if (!check(Tok::Colon))
                    start = expression();
                if (match(Tok::Colon)) {
                    auto slice = std::make_unique<Expr>(ExprKind::Slice, line);
                    if (!check(Tok::RBracket))
                        stop = expression();
                    slice->a = std::move(e);
                    slice->b = std::move(start);
                    slice->c = std::move(stop);
                    e = std::move(slice);
                } else {
                    auto index = std::make_unique<Expr>(ExprKind::Index, line);
                    index->a = std::move(e);
                    index->b = std::move(start);
                    e = std::move(index);
                }
                expect(Tok::RBracket, "Expected ']' to close the '['.");
            } else {
                return e;
            }
        }
    }

    void arguments(Expr& call) {
        bool keywords = false;
        while (!check(Tok::RParen)) {
            if (check(Tok::Name) && peek(1).type == Tok::Assign) {
                call.kwNames.push_back(intern(advance().text));
                advance(); // =
                call.items.push_back(expression());
                keywords = true;
            } else {
                if (keywords)
                    error("Values given by name (like volume=0.5) must come after the other values.");
                call.items.push_back(expression());
            }
            if (!match(Tok::Comma))
                break;
        }
        expect(Tok::RParen, "Expected ')' to close the function call. Did you forget a comma between values?");
    }

    ExprPtr atom() {
        const Token& tok = advance();
        switch (tok.type) {
        case Tok::Number: {
            auto e = std::make_unique<Expr>(ExprKind::Number, tok.line);
            e->number = tok.number;
            return e;
        }
        case Tok::String: {
            auto e = std::make_unique<Expr>(ExprKind::String, tok.line);
            e->col = tok.column;
            e->text = tok.text;
            // Adjacent strings join: "a" "b" == "ab"
            while (check(Tok::String))
                e->text += advance().text;
            return e;
        }
        case Tok::FString: return fstring(tok);
        case Tok::True: return std::make_unique<Expr>(ExprKind::True, tok.line);
        case Tok::False: return std::make_unique<Expr>(ExprKind::False, tok.line);
        case Tok::None: return std::make_unique<Expr>(ExprKind::None, tok.line);
        case Tok::Name: {
            if (tok.text == "self")
                return std::make_unique<Expr>(ExprKind::Self, tok.line);
            auto e = std::make_unique<Expr>(ExprKind::Name, tok.line);
            e->col = tok.column;
            e->text = tok.text;
            e->sym = intern(tok.text);
            return e;
        }
        case Tok::LParen: {
            if (match(Tok::RParen)) {
                auto e = std::make_unique<Expr>(ExprKind::List, tok.line);
                e->isTuple = true;
                return e;
            }
            ExprPtr e = expressionList();
            expect(Tok::RParen, "Expected ')' to close the '('.");
            return e;
        }
        case Tok::LBracket: {
            auto e = std::make_unique<Expr>(ExprKind::List, tok.line);
            while (!check(Tok::RBracket)) {
                e->items.push_back(expression());
                if (!match(Tok::Comma))
                    break;
            }
            expect(Tok::RBracket, "Expected ']' to close the list. Did you forget a comma between items?");
            return e;
        }
        case Tok::LBrace: {
            auto e = std::make_unique<Expr>(ExprKind::Dict, tok.line);
            while (!check(Tok::RBrace)) {
                e->items.push_back(expression());
                expect(Tok::Colon, "Expected ':' between a key and its value, like {\"name\": \"Ava\"}.");
                e->items.push_back(expression());
                if (!match(Tok::Comma))
                    break;
            }
            expect(Tok::RBrace, "Expected '}' to close the dictionary.");
            return e;
        }
        case Tok::Newline:
        case Tok::EndOfFile: error("This line ends too early. Something is missing at the end.", tok.line);
        case Tok::PlusPlus:
        case Tok::MinusMinus:
            error(std::string("EasyScript doesn't have '") + tok.text + "'. Use 'x " +
                      (tok.type == Tok::PlusPlus ? "+= 1" : "-= 1") + "' instead.",
                  tok.line);
        case Tok::Indent: error("This line is indented more than it should be.", tok.line);
        default: error("Unexpected " + describe(tok) + " here.", tok.line);
        }
    }

    ExprPtr fstring(const Token& tok) {
        auto e = std::make_unique<Expr>(ExprKind::FString, tok.line);
        const std::string& s = tok.text;
        std::string literal;
        size_t i = 0;
        while (i < s.size()) {
            char c = s[i];
            if (c == '{' && i + 1 < s.size() && s[i + 1] == '{') {
                literal += '{';
                i += 2;
                continue;
            }
            if (c == '}' && i + 1 < s.size() && s[i + 1] == '}') {
                literal += '}';
                i += 2;
                continue;
            }
            if (c == '}')
                error("A '}' in this f-string has no matching '{'. Use '}}' to show a brace.", tok.line);
            if (c != '{') {
                literal += c;
                ++i;
                continue;
            }
            size_t depth = 1, j = i + 1;
            while (j < s.size() && depth > 0) {
                if (s[j] == '{')
                    ++depth;
                else if (s[j] == '}')
                    --depth;
                if (depth > 0)
                    ++j;
            }
            if (j >= s.size())
                error("A '{' in this f-string is never closed with '}'.", tok.line);
            std::string inner = s.substr(i + 1, j - i - 1);
            std::string spec;
            // Split off a format spec like {value:.2f} (ignore ':' inside brackets/strings).
            int nesting = 0;
            char quote = 0;
            for (size_t k = 0; k < inner.size(); ++k) {
                char ch = inner[k];
                if (quote) {
                    if (ch == '\\')
                        ++k;
                    else if (ch == quote)
                        quote = 0;
                } else if (ch == '"' || ch == '\'')
                    quote = ch;
                else if (ch == '(' || ch == '[' || ch == '{')
                    ++nesting;
                else if (ch == ')' || ch == ']' || ch == '}')
                    --nesting;
                else if (ch == ':' && nesting == 0) {
                    spec = inner.substr(k + 1);
                    inner = inner.substr(0, k);
                    break;
                }
            }
            if (inner.find_first_not_of(" \t") == std::string::npos)
                error("An f-string has empty '{ }'. Put a value inside, like {score}.", tok.line);
            Parser sub(tokenize(inner, tok.line));
            e->literalParts.push_back(literal);
            literal.clear();
            e->items.push_back(sub.singleExpression());
            e->formatSpecs.push_back(spec);
            i = j + 1;
        }
        e->literalParts.push_back(literal);
        return e;
    }
};

} // namespace

Program parse(std::string_view source) {
    return Parser(tokenize(source)).program();
}

} // namespace aven::script
