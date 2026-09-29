#include "test_framework.h"

#include "aven/script/vm.h"

#include <cstdio>

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

// Files from other editors: a byte order mark, Windows line endings, numbers written in
// different ways, and characters written by number.
AVEN_TEST(script_source_text_edge_cases) {
    Harness h;
    auto inst = h.load("\xEF\xBB\xBF" "a = 0xFF + 0x_10\r\nb = 1_000.000_5\r\n"
                       "c = 1 + \\\r\n    2\r\nd = \"caf\\u00e9 \\\r\nbar\"\r\ne = '''x\r\ny'''\r\n");
    CHECK(inst != nullptr);
    CHECK_EQ(h.firstError(), std::string());
    if (!inst)
        return;
    CHECK_EQ(h.var(inst, "a").number(), 271.0);
    CHECK_EQ(h.var(inst, "b").number(), 1000.0005);
    CHECK_EQ(h.var(inst, "c").number(), 3.0);
    CHECK_EQ(h.var(inst, "d").string(), std::string("caf\xC3\xA9 bar"));
    CHECK_EQ(h.var(inst, "e").string(), std::string("x\ny"));
}

// Absurdly nested code gives an error instead of running out of stack space (the web player's
// stack is small), and so does a very long chain of operators.
AVEN_TEST(script_deep_nesting_is_an_error) {
    for (std::string src : {"x = " + std::string(5000, '(') + "1" + std::string(5000, ')') + "\n",
                            [] {
                                std::string s = "x = ";
                                for (int i = 0; i < 5000; ++i)
                                    s += i < 2500 ? "not " : "- ";
                                return s + "1\n";
                            }(),
                            "x = " + std::string(3000, '[') + std::string(3000, ']') + "\n",
                            [] {
                                std::string s = "x = 1";
                                for (int i = 0; i < 20000; ++i)
                                    s += " + 1";
                                return s + "\n";
                            }()}) {
        Harness h;
        h.load(src);
        CHECK(h.firstError().find("nested too deeply") != std::string::npos);
    }
    Harness h; // ordinary code is nowhere near the limit
    auto inst = h.load("x = ((((((((((1 + 2) * 3))))))))) + [[[[[[4]]]]]][0][0][0][0][0][0]\n"
                       "d = {\"}\": 1}\nt = f\"{d['}']} and {d[\\\"}\\\"]}\"\n");
    CHECK_EQ(h.firstError(), std::string());
    if (inst) {
        CHECK_EQ(h.var(inst, "x").number(), 13.0);
        CHECK_EQ(h.var(inst, "t").string(), std::string("1 and 1"));
    }
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

AVEN_TEST(script_inspector_hints) {
    VM vm;
    auto m = vm.compile("# @header Movement\nspeed = 5  # @range(0, 20) how fast\nlives = 3  # @range(1, 9)\n"
                        "hit = \"sounds/hit.wav\"  # @sound when hit\nsecret = 1  # @hide\nlink = \"#tag\"  # a # inside text\n",
                        "h.es");
    CHECK(m != nullptr);
    CHECK_EQ(m->exports.size(), size_t(5));
    auto& speed = m->exports[0];
    CHECK_EQ(speed.header, std::string("Movement"));
    CHECK_EQ(speed.rangeMin, 0.0);
    CHECK_EQ(speed.rangeMax, 20.0);
    CHECK_EQ(speed.comment, std::string("how fast")); // the hint isn't part of the tooltip
    CHECK(m->exports[1].header.empty());
    CHECK_EQ(m->exports[1].rangeMax, 9.0);
    CHECK_EQ(m->exports[2].fileKind, std::string("sound"));
    CHECK_EQ(m->exports[2].comment, std::string("when hit"));
    CHECK(m->exports[3].hidden);
    CHECK_EQ(m->exports[4].comment, std::string("a # inside text"));
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
        {"x = 0x\n", "hexadecimal", 1},
        {"x = 1e999\n", "too big", 1},
        {"x = \"\\u12\"\n", "four hexadecimal digits", 1},
        {"f(a=1, a=2)\n", "given twice", 1},
        {"import \"my-tools.es\"\n", "Give it one with 'as'", 1},
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

// A timer can stop another timer that's due in the same frame; the stopped one doesn't run.
AVEN_TEST(script_timer_stopped_by_another_in_the_same_frame) {
    Harness h;
    auto inst = h.load(R"(
fired = []
second_id = 0
def first():
    fired.append("first")
    stop_timer(second_id)
def second():
    fired.append("second")
def on_start():
    after(1, first)
    second_id = after(1, second)
)");
    h.vm.callFunction(inst, intern("on_start"), {});
    for (int i = 0; i < 15; ++i)
        h.vm.update(0.1);
    CHECK_EQ(h.firstError(), std::string());
    CHECK_EQ(h.var(inst, "fired").repr(), std::string("[\"first\"]"));
}

// Odd values that used to be able to crash: lists inside themselves, huge or missing numbers,
// and functions that start themselves through the engine.
AVEN_TEST(script_odd_values_give_errors_not_crashes) {
    Harness h;
    auto inst = h.load(R"(
a = [1]
a.append(a)
b = [a]
a.append(b)
shown = str(a)
same = a == a
c = [1]
c.append(c)
alike = a == c
t1 = "ab" * float("nan")
t2 = "" * 1e300
t3 = [] * 1e300
padded = f"{1e300:.2f}"
wide = len(f"{1:99999999999}")
zf = len("7".zfill(1e300))
r = round(1e300, 2)
m = max(5)
p = [1]
q = [p, p]
p.append(q)
p.append(q)
r1 = [1]
r2 = [r1, r1]
r1.append(r2)
r1.append(r2)
twins = p == r1
twin_text = str(p) == str(r1)
)");
    CHECK_EQ(h.firstError(), std::string());
    if (!inst)
        return;
    CHECK(h.var(inst, "shown").string().find("[...]") != std::string::npos);
    CHECK(h.var(inst, "same").boolean());
    CHECK(!h.var(inst, "alike").boolean());
    CHECK_EQ(h.var(inst, "t1").string(), std::string());
    CHECK_EQ(h.var(inst, "t2").string(), std::string());
    CHECK(h.var(inst, "padded").string().size() > 300);
    CHECK_EQ(h.var(inst, "wide").number(), 1000.0);
    CHECK_EQ(h.var(inst, "zf").number(), 100000.0);
    CHECK_EQ(h.var(inst, "r").number(), 1e300);
    CHECK_EQ(h.var(inst, "m").number(), 5.0);
    // Two lists holding each other twice over: answered quickly, not by going round every path.
    CHECK(h.var(inst, "twins").boolean());
    CHECK(h.var(inst, "twin_text").boolean());
    CHECK(VM::toJson(h.var(inst, "p")).isArray());
    aven::Json j = VM::toJson(h.var(inst, "a")); // a list inside itself: cut off, not endless
    CHECK(j.isArray());

    struct Case {
        const char* src;
        const char* expect;
    };
    Case cases[] = {
        {"x = [1, 2][1e300]\n", "doesn't exist"},
        {"x = [1, 2][float(\"inf\")]\n", "doesn't exist"},
        {"x = [1, 2][float(\"nan\")]\n", "whole numbers"},
        {"a = [1]\na.append(a)\nb = [1]\nb.append(b)\nx = a < b\n", "nested too deeply"},
        {"def f(x):\n    return sorted([1, 2], key=f)\nx = f(1)\n", "too many times"},
        {"x = int(\"1e999\")\n", "isn't a number"},
    };
    for (auto& c : cases) {
        Harness e;
        e.load(c.src);
        if (e.firstError().find(c.expect) == std::string::npos)
            std::printf("  %s -> %s\n", c.src, e.firstError().c_str());
        CHECK(e.firstError().find(c.expect) != std::string::npos);
    }

    Harness tasks; // start_task starting itself, forever, all in one go
    auto t = tasks.load("n = 0\ndef go():\n    n += 1\n    start_task(go)\n");
    CHECK(t != nullptr);
    tasks.vm.callFunction(t, intern("go"), {});
    CHECK(tasks.firstError().find("too many times") != std::string::npos);
    CHECK(tasks.var(t, "n").number() < 100);
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

// Lists and dicts that only refer to each other are freed; anything still used is left alone.
AVEN_TEST(script_cycles_are_collected) {
    collectCycles(); // start clean
    size_t before = trackedContainers();
    {
        Harness h;
        auto inst = h.load(R"(
kept = {"items": [1, 2]}
kept["self"] = kept
def make_garbage():
    for i in range(50):
        a = []
        b = {"other": a}
        a.append(b)
        a.append(a)
make_garbage()
)");
        CHECK(inst != nullptr);
        CHECK(trackedContainers() >= before + 100);
        size_t freed = collectCycles();
        CHECK(freed >= 100);
        // `kept` is still reachable from the script: untouched.
        Value kept = h.var(inst, "kept");
        CHECK(kept.isDict());
        CHECK(kept.dictObj().find(Value("items")) && kept.dictObj().find(Value("items"))->listObj().items.size() == 2);
        CHECK(kept.dictObj().find(Value("self")) != nullptr);
        CHECK(h.errors.empty());
    }
    collectCycles(); // the script is gone, so its self-referencing dict goes too
    CHECK_EQ(trackedContainers(), before);
}

AVEN_TEST(script_text_added_in_place_stays_unshared) {
    // text += more adds in place when nothing else holds the text; everything else must see no change.
    Harness h;
    auto inst = h.load(R"(
msg = ""
def build():
    t = ""
    for i in range(5):
        t += str(i)
    a = "x"
    b = a
    a += "y"
    items = ["q"]
    s = items[0]
    s += "z"
    u = "ab"
    u += u
    print(t, a, b, items[0], s, u)

def grow():
    global msg
    keep = msg
    msg += "a"
    msg += "b"
    print(msg, "[" + keep + "]")

build()
grow()
first = msg
grow()
print(first)
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.firstError(), std::string());
    CHECK_EQ(h.output(), std::string("01234 xy x q qz abab\nab []\nabab [ab]\nab\n"));
}

AVEN_TEST(script_dict_number_keys_and_deleting) {
    Harness h;
    auto inst = h.load(R"(
d = {}
d[1] = "one"
d[1.0] = "one again"
d[-3] = "minus three"
d[0.5] = "half"
d[10 ** 20] = "big"
d[-0.0] = "zero"
print(len(d), d[1], d[-3], d[0.5], d[10 ** 20], d[0])
e = {}
for i in range(10):
    e[i] = i * i
for i in range(0, 10, 2):
    e.pop(i)
print(list(e.keys()), e[7], len(e))
e[4] = 16
print(list(e.keys()), e[4], e[9])
print(str(12), str(-7), str(10 ** 14), str(2.5), str(-0.0))
)");
    CHECK(inst != nullptr);
    CHECK_EQ(h.firstError(), std::string());
    CHECK_EQ(h.output(), std::string("5 one again minus three half big zero\n[1, 3, 5, 7, 9] 49 5\n[1, 3, 5, 7, 9, 4] 16 81\n"
                                     "12 -7 100000000000000 2.5 0\n"));
}
