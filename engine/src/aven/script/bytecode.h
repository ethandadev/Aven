#pragma once

#include "aven/script/value.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace aven::script {

enum class Op : uint8_t {
    Const,
    PushNone,
    PushTrue,
    PushFalse,
    Pop,
    Dup,
    Dup2,
    Rot, // a: depth; moves the value `a` below the top to the top
    LoadLocal,
    StoreLocal,
    LoadName,
    StoreName,
    LoadSelf,
    LoadAttr,
    StoreAttr,  // stack: obj, value
    LoadIndex,
    StoreIndex, // stack: obj, index, value
    LoadSlice,  // a: bit 1 = has start, bit 2 = has stop
    Add,
    Sub,
    Mul,
    Div,
    FloorDiv,
    Mod,
    Pow,
    Neg,
    Pos,
    Not,
    Eq,
    NotEq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    In,
    NotIn,
    Jump,
    JumpIfFalse,
    JumpIfFalseOrPop,
    JumpIfTrueOrPop,
    BuildList,
    BuildDict,
    BuildString,
    Format, // a: constant index of the format spec
    Call,   // a: positional count, b: keyword count, c: keyword name list index
    Return,
    MakeFunction, // a: function index, b: default count
    GetIter,
    ForIter, // a: jump target when finished
    Unpack,  // a: count
};

struct Instr {
    Op op;
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
};

struct Module;

struct FunctionProto {
    std::string name;
    int line = 0;
    std::vector<Symbol> params;
    int requiredParams = 0;
    int numLocals = 0;
    std::vector<Symbol> localNames;
    std::vector<Instr> code;
    std::vector<int> lines;
    std::vector<Value> constants;
    std::vector<std::vector<Symbol>> keywordLists;
    Module* module = nullptr;
    bool isTopLevel = false;
};

// A script variable shown in the inspector so it can be tweaked per object.
struct ExportedVar {
    std::string name;
    Value defaultValue;
    std::string comment; // trailing "# ..." comment, used as a tooltip
    int line = 0;
};

struct Module {
    std::string path;
    std::string source;
    std::vector<std::unique_ptr<FunctionProto>> functions; // [0] is the top-level code
    std::vector<ExportedVar> exports;
    std::vector<std::string> functionNames;
    std::unordered_set<Symbol> scriptVariables;

    bool hasFunction(std::string_view name) const;
};

// Compiles source into a module. Throws ScriptError (with line) on syntax errors.
std::shared_ptr<Module> compileModule(std::string_view source, const std::string& path);

} // namespace aven::script
