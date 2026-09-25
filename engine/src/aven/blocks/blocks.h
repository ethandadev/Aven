#pragma once

// Block coding: Scratch-style blocks that compile to readable EasyScript.
// Every block knows the code it becomes, so beginners can always look at the
// "real" code behind their blocks and switch to typing it when ready.

#include "aven/core/json.h"
#include "aven/math/math.h"

#include <string>
#include <vector>

namespace aven::blocks {

enum class Shape {
    Hat,       // starts a script: "when game starts"
    Statement, // does something: "move 10 steps"
    CBlock,    // wraps other blocks: "repeat 10"
    IfElse,    // two wrapped areas: "if ... else ..."
    Reporter,  // a value: "mouse x"
    Boolean,   // a true/false value: "key space pressed?"
};

enum class InputType {
    Number,
    Text,
    Condition, // a Boolean block
    Value,     // any reporter
    Key,
    Choice,    // one of `options`
    Color,
    Variable,  // a script variable name
    Name,      // a plain identifier (e.g. game variable name)
    Image,
    Sound,
    Prefab,
    Scene,
};

struct Input {
    std::string name;
    InputType type = InputType::Number;
    std::string defaultValue;
    std::vector<std::string> options; // for Choice
};

struct BlockDef {
    std::string id;
    std::string category;
    Shape shape = Shape::Statement;
    std::string label;   // "move {steps} steps"; {name} marks an input
    std::string code;    // EasyScript with {name} placeholders; may span lines
    std::string event;   // hats: the callback signature, e.g. "on_key_pressed(key)"
    std::string filter;  // hats: condition deciding whether this hat runs, e.g. "key == {key}"
    std::vector<Input> inputs;
    std::string help;
    bool loopWaits = false; // C-blocks that repeat: add wait() each time around
};

struct Category {
    std::string name;
    Color color;
};

const std::vector<BlockDef>& catalog();
const std::vector<Category>& categories();
const BlockDef* find(const std::string& id);
Color categoryColor(const std::string& category);

// Turns a blocks document into EasyScript source. Sets *error on invalid input.
std::string compile(const Json& document, std::string* error = nullptr);
// Same, from the text of a .blocks file.
std::string compileFile(const std::string& text, std::string* error = nullptr);

// A new, empty blocks document.
Json emptyDocument();

} // namespace aven::blocks
