#include "test_framework.h"

#include "aven/blocks/blocks.h"
#include "aven/script/vm.h"

using namespace aven;

namespace {

Json block(const std::string& type) {
    Json b = Json::object();
    b["type"] = type;
    b["inputs"] = Json::object();
    return b;
}

bool compilesAsEasyScript(const std::string& source, std::string& error) {
    script::VM vm;
    vm.onError = [&](const script::ScriptError& e) { error = std::string(e.what()) + " (line " + std::to_string(e.line) + ")"; };
    return vm.compile(source, "blocks.es") != nullptr;
}

} // namespace

AVEN_TEST(blocks_every_block_generates_valid_code) {
    // One script per hat, with every statement block (and reporters inside a print) in a forever loop.
    Json doc = blocks::emptyDocument();
    Json var = Json::object();
    var["name"] = "my_variable";
    var["value"] = 0;
    doc["variables"].push(var);
    for (auto& def : blocks::catalog()) {
        if (def.shape != blocks::Shape::Hat)
            continue;
        Json script = Json::object();
        Json stack = Json::array();
        stack.push(block(def.id));
        Json loop = block("forever");
        loop["body"] = Json::array();
        for (auto& other : blocks::catalog()) {
            if (other.shape == blocks::Shape::Statement) {
                loop["body"].push(block(other.id));
            } else if (other.shape == blocks::Shape::CBlock || other.shape == blocks::Shape::IfElse) {
                Json c = block(other.id);
                c["body"] = Json::array();
                c["body"].push(block("next_frame"));
                loop["body"].push(c);
            } else if (other.shape == blocks::Shape::Reporter || other.shape == blocks::Shape::Boolean) {
                Json p = block("print");
                p["inputs"]["value"] = block(other.id);
                loop["body"].push(p);
            }
        }
        stack.push(loop);
        script["blocks"] = stack;
        doc["scripts"].push(script);
    }
    std::string error;
    std::string source = blocks::compile(doc, &error);
    CHECK(error.empty());
    std::string compileError;
    bool ok = compilesAsEasyScript(source, compileError);
    if (!ok)
        std::cerr << "  generated code failed: " << compileError << "\n" << source << "\n";
    CHECK(ok);
}

AVEN_TEST(blocks_simple_script_reads_naturally) {
    std::string text = R"({
      "aven": "blocks", "version": 1,
      "variables": [{"name": "speed", "value": 5}],
      "scripts": [
        {"blocks": [{"type": "when_start"}, {"type": "say", "inputs": {"text": "Hi!", "seconds": 2}}]},
        {"blocks": [{"type": "when_update"},
                    {"type": "if", "inputs": {"condition": {"type": "key_down", "inputs": {"key": "right"}}},
                     "body": [{"type": "change_x", "inputs": {"dx": {"type": "var", "inputs": {"var": "speed"}}}}]}]},
        {"blocks": [{"type": "move_by"}]}
      ]
    })";
    std::string error;
    std::string code = blocks::compileFile(text, &error);
    CHECK(error.empty());
    CHECK(code.find("speed = 5\n") != std::string::npos);
    CHECK(code.find("def on_start():\n    self.say(\"Hi!\", 2)\n") != std::string::npos);
    CHECK(code.find("def on_update(dt):\n    if key_down(\"right\"):\n        self.x += speed\n") != std::string::npos);
    CHECK(code.find("move") == code.find("move") && code.find("self.move(") == std::string::npos); // loose blocks don't run
}

AVEN_TEST(blocks_multiple_hats_run_side_by_side) {
    std::string text = R"({"scripts": [
        {"blocks": [{"type": "when_start"}, {"type": "forever", "body": [{"type": "turn"}]}]},
        {"blocks": [{"type": "when_start"}, {"type": "say"}]},
        {"blocks": [{"type": "when_key", "inputs": {"key": "a"}}, {"type": "hide"}]},
        {"blocks": [{"type": "when_key", "inputs": {"key": "b"}}, {"type": "show"}]},
        {"blocks": [{"type": "when_timer", "inputs": {"seconds": 2}}, {"type": "clone"}]}
    ]})";
    std::string error;
    std::string code = blocks::compileFile(text, &error);
    CHECK(code.find("start_task(_on_start_1)") != std::string::npos);
    CHECK(code.find("if key == \"b\":\n        start_task(_on_key_pressed_2, key)") != std::string::npos);
    CHECK(code.find("every(2, _every_1)") != std::string::npos);
    CHECK(code.find("if not is_clone:") != std::string::npos); // clones don't rerun start scripts
    std::string compileError;
    CHECK(compilesAsEasyScript(code, compileError));
}
