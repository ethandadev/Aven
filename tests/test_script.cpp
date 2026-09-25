#include "test_framework.h"

#include "aven/script/vm.h"

using namespace aven::script;

namespace {

struct Harness {
    VM vm;
    std::vector<std::string> printed;
    std::vector<ScriptError> errors;

    Harness() {
        vm.onPrint = [this](const std::string& s, const std::string&, int) { printed.push_back(s); };
        vm.onError = [this](const ScriptError& e) { errors.push_back(e); };
    }

    std::shared_ptr<Instance> load(const std::string& src) {
        auto m = vm.compile(src, "test.es");
        if (!m)
            return nullptr;
        return vm.createInstance(m, Value(), "Tester");
    }

    Value var(const std::shared_ptr<Instance>& inst, const char* name) {
        Value* v = inst->find(intern(name));
        return v ? *v : Value();
    }

    std::string output() const {
        std::string out;
        for (auto& p : printed)
            out += p + "\n";
        return out;
    }

    std::string firstError() const { return errors.empty() ? "" : errors[0].what(); }
};

} // namespace

AVEN_TEST(script_arithmetic_and_printing) {
    Harness h;
    auto inst = h.load("a = 7\nb = 2\nprint(a + b, a - b, a * b, a / b, a // b, a % b, a ** b)\nprint(-7 % 3, 0.1 + 0.2)\n");
    CHECK(inst != nullptr);
    CHECK_EQ(h.output(), std::string("9 5 14 3.5 3 1 49\n2 0.30000000000000004\n"));
}

AVEN_TEST(script_control_flow) {
    Harness h;
    auto inst = h.load(R"(
total = 0
for i in range(10):
    if i == 3:
        continue
    if i == 8:
        break
    total += i
n = 0
while n < 5:
    n += 1
else_check = "big" if total > 20 else "small"
if total > 100:
    grade = "a"
elif total > 20:
    grade = "b"
else:
    grade = "c"
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.var(inst, "total").number(), 25.0); // 0+1+2+4+5+6+7
    CHECK_EQ(h.var(inst, "n").number(), 5.0);
    CHECK_EQ(h.var(inst, "else_check").string(), std::string("big"));
    CHECK_EQ(h.var(inst, "grade").string(), std::string("b"));
}

AVEN_TEST(script_functions_defaults_keywords) {
    Harness h;
    auto inst = h.load(R"(
def greet(name, greeting="Hello", punct="!"):
    return greeting + ", " + name + punct

a = greet("Ava")
b = greet("Bo", punct="?")
c = greet(greeting="Hi", name="Cy")

def fact(n):
    if n <= 1:
        return 1
    return n * fact(n - 1)
f = fact(10)
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.var(inst, "a").string(), std::string("Hello, Ava!"));
    CHECK_EQ(h.var(inst, "b").string(), std::string("Hello, Bo?"));
    CHECK_EQ(h.var(inst, "c").string(), std::string("Hi, Cy!"));
    CHECK_EQ(h.var(inst, "f").number(), 3628800.0);
}

AVEN_TEST(script_functions_update_script_variables_without_global) {
    Harness h;
    auto inst = h.load(R"(
score = 0
def add_points(p):
    score += p
    temp = p * 2
    return temp
)");
    CHECK(inst != nullptr);
    auto r = h.vm.callFunction(inst, intern("add_points"), {Value(5)});
    CHECK(r.ok);
    CHECK_EQ(r.value.number(), 10.0);
    CHECK_EQ(h.var(inst, "score").number(), 5.0);
    CHECK(inst->find(intern("temp")) == nullptr); // locals stay local
}

AVEN_TEST(script_collections) {
    Harness h;
    auto inst = h.load(R"(
items = [3, 1, 2]
items.append(5)
items.sort()
first = items[0]
last = items[-1]
middle = items[1:3]
d = {"hp": 10, "name": "Slime"}
d["hp"] -= 3
keys = []
for k, v in d.items():
    keys.append(k)
has = "hp" in d
missing = d.get("mp", 0)
words = "a,b,c".split(",")
joined = "-".join(words)
count = len(items)
nested = [[1, 2], [3, 4]]
nested[1][0] = 9
s = sorted([5, 2, 8], reverse=True)
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.var(inst, "first").number(), 1.0);
    CHECK_EQ(h.var(inst, "last").number(), 5.0);
    CHECK_EQ(h.var(inst, "middle").repr(), std::string("[2, 3]"));
    CHECK_EQ(h.var(inst, "d").repr(), std::string("{\"hp\": 7, \"name\": \"Slime\"}"));
    CHECK_EQ(h.var(inst, "keys").repr(), std::string("[\"hp\", \"name\"]"));
    CHECK(h.var(inst, "has").boolean());
    CHECK_EQ(h.var(inst, "missing").number(), 0.0);
    CHECK_EQ(h.var(inst, "joined").string(), std::string("a-b-c"));
    CHECK_EQ(h.var(inst, "count").number(), 4.0);
    CHECK_EQ(h.var(inst, "nested").repr(), std::string("[[1, 2], [9, 4]]"));
    CHECK_EQ(h.var(inst, "s").repr(), std::string("[8, 5, 2]"));
}

AVEN_TEST(script_fstrings_and_text) {
    Harness h;
    auto inst = h.load(R"(
score = 42
t = 3.14159
msg = f"Score: {score}, time {t:.2f}s, {{braces}}, {score * 2}"
concat = "Lives: " + 3
padded = f"{7:03}"
colon = f"{'time: ' + str(3)}"
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.var(inst, "msg").string(), std::string("Score: 42, time 3.14s, {braces}, 84"));
    CHECK_EQ(h.var(inst, "concat").string(), std::string("Lives: 3"));
    CHECK_EQ(h.var(inst, "padded").string(), std::string("007"));
    CHECK_EQ(h.var(inst, "colon").string(), std::string("time: 3"));
}

AVEN_TEST(script_c_style_aliases) {
    Harness h;
    auto inst = h.load("a = true && !false\nb = false || null == None;\nif a: c = 1\nelse if b: c = 2\n");
    CHECK(inst != nullptr);
    CHECK(h.var(inst, "a").boolean());
    CHECK(h.var(inst, "b").boolean());
    CHECK_EQ(h.var(inst, "c").number(), 1.0);
}

AVEN_TEST(script_bools_compare_like_numbers) {
    Harness h;
    auto inst = h.load("eq = 1 == True\nne = 1 != True\nne0 = 0 != False\ncount = True + True\n");
    CHECK(inst != nullptr);
    CHECK(h.var(inst, "eq").boolean());
    CHECK(!h.var(inst, "ne").boolean());
    CHECK(!h.var(inst, "ne0").boolean());
    CHECK_EQ(h.var(inst, "count").number(), 2.0);
}

AVEN_TEST(script_vectors_and_colors) {
    Harness h;
    auto inst = h.load(R"(
v = vec(3, 4) * 2
l = v.length()
c = color("red")
r = c.r
w = rgb(255, 255, 255)
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.var(inst, "v").repr(), std::string("vec(6, 8)"));
    CHECK_NEAR(h.var(inst, "l").number(), 10, 1e-9);
    CHECK_NEAR(h.var(inst, "r").number(), 0xEF / 255.0, 1e-9);
    CHECK_EQ(h.var(inst, "w").repr(), std::string("rgb(255, 255, 255, 1)"));
}

AVEN_TEST(script_exports_for_inspector) {
    VM vm;
    auto m = vm.compile("speed = 5  # how fast we run\nname = \"Hero\"\n_private = 3\ntint = rgb(255, 0, 0)\ncomputed = speed * 2\n",
                        "p.es");
    CHECK(m != nullptr);
    CHECK_EQ(m->exports.size(), size_t(3));
    CHECK_EQ(m->exports[0].name, std::string("speed"));
    CHECK_EQ(m->exports[0].comment, std::string("how fast we run"));
    CHECK_EQ(m->exports[2].name, std::string("tint"));
}

AVEN_TEST(script_errors_are_friendly) {
    struct Case {
        const char* src;
        const char* expect;
        int line;
    };
    Case cases[] = {
        {"speed = 5\nx = spede + 1\n", "Did you mean 'speed'?", 2},
        {"if x = 5:\n    pass\n", "Use '=='", 1},
        {"x = 1\nx++\n", "Use 'x += 1'", 2},
        {"def f()\n    pass\n", "Expected ':'", 1},
        {"if True:\nprint(1)\n", "indented block", 2},
        {"x = [1, 2\ny = 3\n", "never closed", 1},
        {"x = 5 / 0\n", "divide by zero", 1},
        {"x = [1, 2, 3]\ny = x[5]\n", "has 3 items, so position 5", 2},
        {"x = \"a\" - 1\n", "Text can only be joined with '+'", 1},
        {"function jump():\n    pass\n", "Use 'def'", 1},
        {"if 0 < x < 5:\n    pass\n", "joined with 'and'", 1},
        {"for i in 10:\n    pass\n", "range(10)", 1},
        {"x = len()\n", "len() needs 1 value, but got 0", 1},
        {"if x > 3 {\n", "instead of { }", 1},
        {"while True {\n  x = 1\n}\n", "instead of { }", 1},
        {"  x = 1\n", "indented", 1},
        {"print(\xE2\x80\x9Chello\xE2\x80\x9D)\n", "Use straight quotes", 1},
        {"x = 5 \xE2\x80\x93 2\n", "Type - instead", 1},
    };
    for (auto& c : cases) {
        Harness h;
        h.load(c.src);
        CHECK(!h.errors.empty());
        if (h.errors.empty())
            continue;
        std::string msg = h.errors[0].what();
        if (msg.find(c.expect) == std::string::npos)
            std::cerr << "  unexpected message for " << c.src << ": " << msg << "\n";
        CHECK(msg.find(c.expect) != std::string::npos);
        CHECK_EQ(h.errors[0].line, c.line);
        CHECK_EQ(h.errors[0].file, std::string("test.es"));
    }
}

AVEN_TEST(script_runtime_error_trace) {
    Harness h;
    auto inst = h.load("def inner():\n    return None.x\ndef outer():\n    inner()\n");
    CHECK(inst != nullptr);
    auto r = h.vm.callFunction(inst, intern("outer"), {});
    CHECK(!r.ok);
    CHECK_EQ(h.errors.size(), size_t(1));
    CHECK_EQ(h.errors[0].line, 2);
    CHECK(h.errors[0].trace.find("inner()") != std::string::npos);
    CHECK(h.errors[0].trace.find("outer()") != std::string::npos);
    CHECK(std::string(h.errors[0].what()).find("None") != std::string::npos);
}

AVEN_TEST(script_infinite_loop_is_stopped) {
    Harness h;
    h.vm.instructionBudget = 100000;
    auto inst = h.load("def forever():\n    while True:\n        pass\n");
    auto r = h.vm.callFunction(inst, intern("forever"), {});
    CHECK(!r.ok);
    CHECK(h.firstError().find("wait()") != std::string::npos);
}

AVEN_TEST(script_wait_resumes_later) {
    Harness h;
    auto inst = h.load(R"(
steps = []
def on_start():
    steps.append("a")
    wait(1)
    steps.append("b")
    for i in range(2):
        wait()
        steps.append(i)
)");
    auto r = h.vm.callFunction(inst, intern("on_start"), {});
    CHECK(r.ok && r.waiting);
    CHECK_EQ(h.var(inst, "steps").repr(), std::string("[\"a\"]"));
    h.vm.update(0.5);
    CHECK_EQ(h.var(inst, "steps").repr(), std::string("[\"a\"]"));
    CHECK(h.vm.isWaiting(inst.get(), intern("on_start")));
    h.vm.update(0.6);
    CHECK_EQ(h.var(inst, "steps").repr(), std::string("[\"a\", \"b\"]"));
    h.vm.update(0.016);
    h.vm.update(0.016);
    CHECK_EQ(h.var(inst, "steps").repr(), std::string("[\"a\", \"b\", 0, 1]"));
    CHECK_EQ(h.vm.waitingCount(), size_t(0));
}

AVEN_TEST(script_wait_not_allowed_at_top_level) {
    Harness h;
    h.load("wait(1)\n");
    CHECK(h.firstError().find("wait() can't be used here") != std::string::npos);
}

AVEN_TEST(script_timers) {
    Harness h;
    auto inst = h.load(R"(
ticks = 0
boom = False
def tick():
    ticks += 1
def explode():
    boom = True
def on_start():
    every(1, tick)
    after(2.5, explode)
)");
    h.vm.callFunction(inst, intern("on_start"), {});
    for (int i = 0; i < 30; ++i)
        h.vm.update(0.1);
    CHECK_EQ(h.var(inst, "ticks").number(), 3.0);
    CHECK(h.var(inst, "boom").boolean());
    inst->alive = false; // destroyed objects stop their timers
    for (int i = 0; i < 20; ++i)
        h.vm.update(0.1);
    CHECK_EQ(h.var(inst, "ticks").number(), 3.0);
}

AVEN_TEST(script_hot_reload_keeps_changed_state) {
    Harness h;
    auto m1 = h.vm.compile("speed = 5\nhealth = 100\ndef value():\n    return 1\n", "r.es");
    auto inst = h.vm.createInstance(m1, Value(), "R");
    inst->set(intern("health"), Value(40)); // changed while playing
    auto m2 = h.vm.compile("speed = 9\nhealth = 100\nmana = 3\ndef value():\n    return 2\n", "r.es");
    CHECK(h.vm.reload(inst, m2));
    CHECK_EQ(h.var(inst, "speed").number(), 9.0);  // untouched default picks up the edit
    CHECK_EQ(h.var(inst, "health").number(), 40.0); // runtime change is kept
    CHECK_EQ(h.var(inst, "mana").number(), 3.0);
    CHECK_EQ(h.vm.callFunction(inst, intern("value"), {}).value.number(), 2.0);
}

AVEN_TEST(script_callbacks_may_omit_parameters) {
    Harness h;
    auto inst = h.load("n = 0\ndef on_update():\n    n += 1\n");
    CHECK(h.vm.callFunction(inst, intern("on_update"), {Value(0.016)}).ok);
    CHECK_EQ(h.var(inst, "n").number(), 1.0);
}

AVEN_TEST(script_json_roundtrip) {
    aven::Json j = VM::toJson(Value::list({Value(1), Value("a"), Value::color(1, 0, 0, 1)}));
    Value back = VM::fromJson(j);
    CHECK_EQ(back.repr(), std::string("[1, \"a\", rgb(255, 0, 0, 1)]"));
}
