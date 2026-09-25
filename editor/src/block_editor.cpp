#include "block_editor.h"

#include "aven/platform/input.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aven::editor {

using blocks::BlockDef;
using blocks::InputType;
using blocks::Shape;

namespace {

constexpr float kPad = 8, kGap = 5, kRow = 34, kInputH = 22, kArm = 16, kBottom = 18, kHat = 14, kReporterH = 26;
constexpr float kMinBody = 24, kElseRow = 24, kSnap = 28;

struct Segment {
    bool input;
    std::string text;
};

std::vector<Segment> parseLabel(const std::string& label) {
    std::vector<Segment> out;
    size_t i = 0;
    while (i < label.size()) {
        size_t open = label.find('{', i);
        if (open == std::string::npos) {
            out.push_back({false, label.substr(i)});
            break;
        }
        if (open > i)
            out.push_back({false, label.substr(i, open - i)});
        size_t close = label.find('}', open);
        out.push_back({true, label.substr(open + 1, close - open - 1)});
        i = close + 1;
    }
    for (auto& s : out)
        if (!s.input) {
            size_t a = s.text.find_first_not_of(' '), b = s.text.find_last_not_of(' ');
            s.text = a == std::string::npos ? "" : s.text.substr(a, b - a + 1);
        }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Segment& s) { return !s.input && s.text.empty(); }), out.end());
    return out;
}

const blocks::Input* findInput(const BlockDef* def, const std::string& name) {
    for (auto& in : def->inputs)
        if (in.name == name)
            return &in;
    return nullptr;
}

ImU32 toU32(Color c, float shade = 1.0f, float alpha = 1.0f) {
    return IM_COL32(static_cast<int>(saturate(c.r * shade) * 255), static_cast<int>(saturate(c.g * shade) * 255),
                    static_cast<int>(saturate(c.b * shade) * 255), static_cast<int>(alpha * 255));
}

bool acceptsBlocks(InputType t) {
    return t == InputType::Number || t == InputType::Text || t == InputType::Value || t == InputType::Condition;
}

Json defaultLiteral(const blocks::Input& in) {
    switch (in.type) {
    case InputType::Condition: return Json();
    case InputType::Number: {
        char* end = nullptr;
        double v = std::strtod(in.defaultValue.c_str(), &end);
        if (end && *end == '\0' && !in.defaultValue.empty())
            return Json(v);
        return Json(in.defaultValue);
    }
    case InputType::Value: {
        std::string d = in.defaultValue;
        if (d.size() >= 2 && d.front() == '"' && d.back() == '"')
            return Json(d.substr(1, d.size() - 2));
        char* end = nullptr;
        double v = std::strtod(d.c_str(), &end);
        if (end && *end == '\0' && !d.empty())
            return Json(v);
        return Json(d);
    }
    default: return Json(in.defaultValue);
    }
}

const char* kKeys[] = {"space", "left", "right", "up", "down", "enter", "escape", "shift", "a", "b", "c", "d", "e",
                       "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
                       "w", "x", "y", "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
const char* kColorNames[] = {"red", "orange", "yellow", "lime", "green", "teal", "cyan", "sky", "blue", "navy",
                             "purple", "magenta", "pink", "brown", "gold", "white", "gray", "black"};

} // namespace

bool parseBlockColor(const std::string& name, Color& out);

BlockEditor::BlockEditor() {
    preview_.readOnly = true;
}

BlockEditor::~BlockEditor() = default;

// ---------------------------------------------------------------- document

BlockEditor::BlockPtr BlockEditor::makeBlock(const BlockDef* def) {
    auto b = std::make_unique<Block>();
    b->def = def;
    for (auto& in : def->inputs)
        b->inputs[in.name].literal = defaultLiteral(in);
    return b;
}

BlockEditor::BlockPtr BlockEditor::fromJson(const Json& j) {
    const BlockDef* def = blocks::find(j["type"].asString());
    if (!def)
        return nullptr;
    auto b = makeBlock(def);
    for (auto& m : j["inputs"].members()) {
        if (!b->inputs.count(m.key))
            continue;
        if (m.value.isObject() && m.value.contains("type"))
            b->inputs[m.key].block = fromJson(m.value);
        else
            b->inputs[m.key].literal = m.value;
    }
    for (auto& c : j["body"].elements())
        if (auto child = fromJson(c))
            b->body.push_back(std::move(child));
    for (auto& c : j["else"].elements())
        if (auto child = fromJson(c))
            b->elseBody.push_back(std::move(child));
    return b;
}

Json BlockEditor::toJson(const Block& b) {
    Json j = Json::object();
    j["type"] = b.def->id;
    Json inputs = Json::object();
    for (auto& in : b.def->inputs) {
        auto it = b.inputs.find(in.name);
        if (it == b.inputs.end())
            continue;
        inputs[in.name] = it->second.block ? toJson(*it->second.block) : it->second.literal;
    }
    if (inputs.size())
        j["inputs"] = inputs;
    if (b.def->shape == Shape::CBlock || b.def->shape == Shape::IfElse) {
        Json body = Json::array();
        for (auto& c : b.body)
            body.push(toJson(*c));
        j["body"] = body;
    }
    if (b.def->shape == Shape::IfElse) {
        Json body = Json::array();
        for (auto& c : b.elseBody)
            body.push(toJson(*c));
        j["else"] = body;
    }
    return j;
}

BlockEditor::BlockPtr BlockEditor::clone(const Block& b) {
    return fromJson(toJson(b));
}

bool BlockEditor::load(const Json& doc) {
    stacks_.clear();
    variables_.clear();
    for (auto& v : doc["variables"].elements())
        variables_.push_back({v["name"].asString(), v["value"]});
    for (auto& s : doc["scripts"].elements()) {
        Stack st;
        st.pos = {s["x"].asFloat(20), s["y"].asFloat(20)};
        st.autoPlace = !s.contains("x") && !s.contains("y");
        for (auto& b : s["blocks"].elements())
            if (auto block = fromJson(b))
                st.blocks.push_back(std::move(block));
        if (!st.blocks.empty())
            stacks_.push_back(std::move(st));
    }
    codeDirty_ = true;
    undo_.clear();
    redo_.clear();
    return true;
}

Json BlockEditor::save() const {
    Json doc = blocks::emptyDocument();
    for (auto& [name, value] : variables_) {
        Json v = Json::object();
        v["name"] = name;
        v["value"] = value;
        doc["variables"].push(v);
    }
    for (auto& s : stacks_) {
        Json st = Json::object();
        st["x"] = std::round(s.pos.x);
        st["y"] = std::round(s.pos.y);
        Json list = Json::array();
        for (auto& b : s.blocks)
            list.push(toJson(*b));
        st["blocks"] = list;
        doc["scripts"].push(st);
    }
    return doc;
}

const std::string& BlockEditor::code() {
    if (codeDirty_) {
        std::string error;
        code_ = blocks::compile(save(), &error);
        preview_.setText(code_);
        codeDirty_ = false;
    }
    return code_;
}

void BlockEditor::setCodeError(int line, const std::string& message) { preview_.setError(line, message); }

void BlockEditor::snapshot() {
    undo_.push_back(save());
    if (undo_.size() > 100)
        undo_.erase(undo_.begin());
    redo_.clear();
}

void BlockEditor::undo() {
    if (undo_.empty())
        return;
    redo_.push_back(save());
    Json prev = undo_.back();
    undo_.pop_back();
    auto u = std::move(undo_);
    auto r = std::move(redo_);
    load(prev);
    undo_ = std::move(u);
    redo_ = std::move(r);
    modified();
}

void BlockEditor::redo() {
    if (redo_.empty())
        return;
    undo_.push_back(save());
    Json next = redo_.back();
    redo_.pop_back();
    auto u = std::move(undo_);
    auto r = std::move(redo_);
    load(next);
    undo_ = std::move(u);
    redo_ = std::move(r);
    modified();
}

void BlockEditor::modified() {
    changed_ = true;
    codeDirty_ = true;
}

std::vector<std::string> BlockEditor::variableNames() const {
    std::vector<std::string> out;
    for (auto& [n, v] : variables_)
        out.push_back(n);
    return out;
}

// ---------------------------------------------------------------- layout

float BlockEditor::textWidth(const std::string& s) const {
    ImFont* f = font ? font : ImGui::GetFont();
    return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, s.c_str()).x;
}

std::string BlockEditor::literalText(const blocks::Input& in, const Json& v) const {
    if (v.isNumber()) {
        char buf[32];
        double n = v.asNumber();
        if (n == std::floor(n) && std::abs(n) < 1e9)
            std::snprintf(buf, sizeof buf, "%.0f", n);
        else
            std::snprintf(buf, sizeof buf, "%g", n);
        return buf;
    }
    if (v.isBool())
        return v.asBool() ? "true" : "false";
    std::string s = v.asString();
    if (in.type == InputType::Image || in.type == InputType::Sound || in.type == InputType::Prefab || in.type == InputType::Scene) {
        size_t slash = s.find_last_of('/');
        if (slash != std::string::npos)
            s = s.substr(slash + 1);
        if (s.empty())
            s = "choose...";
    }
    return s;
}

ImVec2 BlockEditor::measureSlot(Block& b, const blocks::Input& in) {
    Slot& slot = b.inputs[in.name];
    if (slot.block)
        return measure(*slot.block);
    if (in.type == InputType::Condition)
        return {42, kInputH - 2};
    if (in.type == InputType::Color)
        return {30, kInputH};
    std::string text = literalText(in, slot.literal);
    float w = textWidth(text) + 16;
    bool dropdown = in.type == InputType::Key || in.type == InputType::Choice || in.type == InputType::Variable ||
                    in.type == InputType::Image || in.type == InputType::Sound || in.type == InputType::Prefab ||
                    in.type == InputType::Scene || (in.type == InputType::Name && !in.options.empty());
    if (dropdown)
        w += 12;
    return {std::max(w, 30.0f), kInputH};
}

ImVec2 BlockEditor::measure(Block& b) {
    const BlockDef* def = b.def;
    bool value = def->shape == Shape::Reporter || def->shape == Shape::Boolean;
    float x = value ? kPad + 6 : kPad;
    float row = value ? kReporterH : kRow;
    for (auto& seg : parseLabel(def->label)) {
        if (!seg.input) {
            x += textWidth(seg.text) + kGap;
        } else if (const blocks::Input* in = findInput(def, seg.text)) {
            ImVec2 s = measureSlot(b, *in);
            x += s.x + kGap;
            row = std::max(row, s.y + 8);
        }
    }
    float width = std::max(x - kGap + (value ? kPad + 6 : kPad), value ? 36.0f : 70.0f);
    b.header = row + (def->shape == Shape::Hat ? kHat : 0);
    float height = b.header;
    if (def->shape == Shape::CBlock || def->shape == Shape::IfElse) {
        b.bodyHeight = std::max(kMinBody, stackHeight(b.body));
        height += b.bodyHeight + kBottom;
        width = std::max(width, 140.0f);
        if (def->shape == Shape::IfElse) {
            b.elseHeight = std::max(kMinBody, stackHeight(b.elseBody));
            height += kElseRow + b.elseHeight;
        }
    }
    b.size = {width, height};
    return b.size;
}

float BlockEditor::stackHeight(std::vector<BlockPtr>& list) {
    float h = 0;
    for (auto& b : list)
        h += measure(*b).y;
    return h;
}

void BlockEditor::layout(Block& b, ImVec2 pos) {
    b.pos = pos;
    b.slots.clear();
    const BlockDef* def = b.def;
    bool value = def->shape == Shape::Reporter || def->shape == Shape::Boolean;
    float top = pos.y + (def->shape == Shape::Hat ? kHat : 0);
    float rowH = b.header - (def->shape == Shape::Hat ? kHat : 0);
    float centerY = top + rowH * 0.5f;
    float x = pos.x + (value ? kPad + 6 : kPad);
    for (auto& seg : parseLabel(def->label)) {
        if (!seg.input) {
            x += textWidth(seg.text) + kGap;
            continue;
        }
        const blocks::Input* in = findInput(def, seg.text);
        if (!in)
            continue;
        Slot& slot = b.inputs[in->name];
        ImVec2 s = slot.block ? slot.block->size : measureSlot(b, *in);
        ImVec2 mn{x, centerY - s.y * 0.5f};
        b.slots.push_back({in->name, mn, {mn.x + s.x, mn.y + s.y}});
        if (slot.block)
            layout(*slot.block, mn);
        x += s.x + kGap;
    }
    b.bodyTop = pos.y + b.header;
    if (def->shape == Shape::CBlock || def->shape == Shape::IfElse) {
        layoutList(b.body, {pos.x + kArm, b.bodyTop});
        if (def->shape == Shape::IfElse) {
            b.elseTop = b.bodyTop + b.bodyHeight + kElseRow;
            layoutList(b.elseBody, {pos.x + kArm, b.elseTop});
        }
    }
}

void BlockEditor::layoutList(std::vector<BlockPtr>& list, ImVec2 pos) {
    for (auto& b : list) {
        layout(*b, pos);
        pos.y += b->size.y;
    }
}

void BlockEditor::collectSlots(Block& b) {
    for (auto& s : b.slots) {
        Slot& slot = b.inputs[s.name];
        if (slot.block) {
            Location where;
            where.parent = &b;
            where.input = s.name;
            hits_.push_back({slot.block.get(), where, slot.block->pos,
                             {slot.block->pos.x + slot.block->size.x, slot.block->pos.y + slot.block->size.y}});
            collectSlots(*slot.block);
            collect(slot.block->body, -1, slot.block.get());
        } else {
            slotHits_.push_back({&b, s.name, s.min, s.max});
        }
    }
}

void BlockEditor::collect(std::vector<BlockPtr>& list, int stack, Block* parent) {
    for (size_t i = 0; i < list.size(); ++i) {
        Block& b = *list[i];
        Location where;
        where.list = &list;
        where.index = i;
        where.stack = stack;
        float headerBottom = b.pos.y + b.header;
        ImVec2 max{b.pos.x + b.size.x, b.def->shape == Shape::CBlock || b.def->shape == Shape::IfElse ? headerBottom : b.pos.y + b.size.y};
        hits_.push_back({&b, where, b.pos, max});
        collectSlots(b);
        bool isValue = b.def->shape == Shape::Reporter || b.def->shape == Shape::Boolean;
        if (!isValue)
            attach_.push_back({&list, i + 1, {b.pos.x, b.pos.y + b.size.y}, -1});
        if (b.def->shape == Shape::CBlock || b.def->shape == Shape::IfElse) {
            attach_.push_back({&b.body, 0, {b.pos.x + kArm, b.bodyTop}, -1});
            collect(b.body, -1, &b);
            if (b.def->shape == Shape::IfElse) {
                attach_.push_back({&b.elseBody, 0, {b.pos.x + kArm, b.elseTop}, -1});
                collect(b.elseBody, -1, &b);
            }
            // Clicking the arms or bottom bar of a C-block picks it up too.
            hits_.push_back({&b, where, {b.pos.x, headerBottom}, {b.pos.x + kArm, b.pos.y + b.size.y}});
            hits_.push_back({&b, where, {b.pos.x, b.pos.y + b.size.y - kBottom}, {b.pos.x + b.size.x, b.pos.y + b.size.y}});
        }
    }
    (void)parent;
}

// ---------------------------------------------------------------- drawing

ImVec2 BlockEditor::toScreen(ImVec2 p) const {
    return {canvasOrigin_.x + (p.x + pan_.x) * zoom_, canvasOrigin_.y + (p.y + pan_.y) * zoom_};
}

ImVec2 BlockEditor::toCanvas(ImVec2 p) const {
    return {(p.x - canvasOrigin_.x) / zoom_ - pan_.x, (p.y - canvasOrigin_.y) / zoom_ - pan_.y};
}

void BlockEditor::drawSlot(ImDrawList* dl, Block& b, const blocks::Input& in, const SlotRect& r) {
    ImFont* f = font ? font : ImGui::GetFont();
    float fs = f->FontSize * zoom_;
    ImVec2 mn = toScreen(r.min), mx = toScreen(r.max);
    Slot& slot = b.inputs[in.name];
    if (slot.block) {
        drawBlock(dl, *slot.block);
        return;
    }
    Color cat = blocks::categoryColor(b.def->category);
    float h = mx.y - mn.y;
    if (in.type == InputType::Condition) {
        float c = h * 0.5f;
        ImVec2 pts[6] = {{mn.x + c, mn.y}, {mx.x - c, mn.y}, {mx.x, mn.y + c}, {mx.x - c, mx.y}, {mn.x + c, mx.y}, {mn.x, mn.y + c}};
        dl->AddConvexPolyFilled(pts, 6, toU32(cat, 0.7f));
        return;
    }
    if (in.type == InputType::Color) {
        std::string name = slot.literal.asString("red");
        Color col{1, 0, 0, 1};
        parseBlockColor(name, col);
        dl->AddRectFilled(mn, mx, toU32(col), 4 * zoom_);
        dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 200), 4 * zoom_, 0, 1.5f);
        return;
    }
    bool dropdown = in.type == InputType::Key || in.type == InputType::Choice || in.type == InputType::Variable ||
                    in.type == InputType::Image || in.type == InputType::Sound || in.type == InputType::Prefab ||
                    in.type == InputType::Scene || (in.type == InputType::Name && !in.options.empty());
    std::string text = literalText(in, slot.literal);
    if (dropdown) {
        dl->AddRectFilled(mn, mx, toU32(cat, 0.78f), 4 * zoom_);
        dl->AddText(f, fs, {mn.x + 7 * zoom_, mn.y + (h - fs) * 0.5f}, IM_COL32(255, 255, 255, 255), text.c_str());
        float ax = mx.x - 10 * zoom_, ay = mn.y + h * 0.5f;
        dl->AddTriangleFilled({ax - 4 * zoom_, ay - 2 * zoom_}, {ax + 4 * zoom_, ay - 2 * zoom_}, {ax, ay + 3 * zoom_},
                              IM_COL32(255, 255, 255, 220));
    } else {
        float rounding = in.type == InputType::Number ? h * 0.5f : 4 * zoom_;
        dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255), rounding);
        dl->AddText(f, fs, {mn.x + 8 * zoom_, mn.y + (h - fs) * 0.5f}, IM_COL32(50, 50, 60, 255), text.c_str());
    }
}

void BlockEditor::drawBlock(ImDrawList* dl, Block& b, bool ghost) {
    ImFont* f = font ? font : ImGui::GetFont();
    float fs = f->FontSize * zoom_;
    Color cat = blocks::categoryColor(b.def->category);
    float alpha = ghost ? 0.75f : 1.0f;
    ImU32 fill = toU32(cat, 1.0f, alpha), border = toU32(cat, 0.72f, alpha);
    ImVec2 mn = toScreen(b.pos), mx = toScreen({b.pos.x + b.size.x, b.pos.y + b.size.y});
    float r = 5 * zoom_;
    Shape shape = b.def->shape;
    switch (shape) {
    case Shape::Reporter:
        dl->AddRectFilled(mn, mx, fill, (mx.y - mn.y) * 0.5f);
        dl->AddRect(mn, mx, border, (mx.y - mn.y) * 0.5f, 0, 1.2f);
        break;
    case Shape::Boolean: {
        float c = (mx.y - mn.y) * 0.5f;
        ImVec2 pts[6] = {{mn.x + c, mn.y}, {mx.x - c, mn.y}, {mx.x, mn.y + c}, {mx.x - c, mx.y}, {mn.x + c, mx.y}, {mn.x, mn.y + c}};
        dl->AddConvexPolyFilled(pts, 6, fill);
        dl->AddPolyline(pts, 6, border, ImDrawFlags_Closed, 1.2f);
        break;
    }
    case Shape::Hat: {
        ImVec2 capMax{mn.x + std::min(110 * zoom_, mx.x - mn.x), mn.y + kHat * 2 * zoom_};
        dl->AddRectFilled(mn, capMax, fill, 18 * zoom_, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled({mn.x, mn.y + kHat * zoom_}, mx, fill, r, ImDrawFlags_RoundCornersBottom | ImDrawFlags_RoundCornersTopRight);
        dl->AddRectFilled({mn.x + 12 * zoom_, mx.y}, {mn.x + 30 * zoom_, mx.y + 4 * zoom_}, fill, 2 * zoom_);
        break;
    }
    case Shape::Statement:
        dl->AddRectFilled(mn, mx, fill, r);
        dl->AddRect(mn, mx, border, r, 0, 1.0f);
        dl->AddRectFilled({mn.x + 12 * zoom_, mx.y - 1}, {mn.x + 30 * zoom_, mx.y + 4 * zoom_}, fill, 2 * zoom_);
        break;
    case Shape::CBlock:
    case Shape::IfElse: {
        float headerBottom = mn.y + b.header * zoom_;
        float arm = kArm * zoom_;
        dl->AddRectFilled(mn, {mx.x, headerBottom}, fill, r, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled({mn.x, headerBottom - 1}, {mn.x + arm, mx.y}, fill);
        dl->AddRectFilled({mn.x, mx.y - kBottom * zoom_}, mx, fill, r, ImDrawFlags_RoundCornersBottom);
        if (shape == Shape::IfElse) {
            float elseY = toScreen({0, b.elseTop - kElseRow}).y;
            dl->AddRectFilled({mn.x, elseY}, {mx.x, elseY + kElseRow * zoom_}, fill, r);
            dl->AddText(f, fs, {mn.x + kPad * zoom_, elseY + (kElseRow * zoom_ - fs) * 0.5f}, IM_COL32(255, 255, 255, 255), "else");
        }
        dl->AddRectFilled({mn.x + 12 * zoom_, mx.y - 1}, {mn.x + 30 * zoom_, mx.y + 4 * zoom_}, fill, 2 * zoom_);
        break;
    }
    }

    // Label text and inputs
    bool value = shape == Shape::Reporter || shape == Shape::Boolean;
    float top = b.pos.y + (shape == Shape::Hat ? kHat : 0);
    float rowH = b.header - (shape == Shape::Hat ? kHat : 0);
    float x = b.pos.x + (value ? kPad + 6 : kPad);
    size_t slotIndex = 0;
    for (auto& seg : parseLabel(b.def->label)) {
        if (!seg.input) {
            ImVec2 p = toScreen({x, top + rowH * 0.5f});
            dl->AddText(f, fs, {p.x, p.y - fs * 0.5f}, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), seg.text.c_str());
            x += textWidth(seg.text) + kGap;
            continue;
        }
        const blocks::Input* in = findInput(b.def, seg.text);
        if (!in || slotIndex >= b.slots.size())
            continue;
        const SlotRect& sr = b.slots[slotIndex++];
        drawSlot(dl, b, *in, sr);
        x = sr.max.x + kGap;
    }
    if (shape == Shape::CBlock || shape == Shape::IfElse) {
        drawList(dl, b.body, ghost);
        if (shape == Shape::IfElse)
            drawList(dl, b.elseBody, ghost);
    }
}

void BlockEditor::drawList(ImDrawList* dl, std::vector<BlockPtr>& list, bool ghost) {
    for (auto& b : list)
        drawBlock(dl, *b, ghost);
}

// ---------------------------------------------------------------- interaction

BlockEditor::BlockPtr BlockEditor::detach(const Location& where) {
    if (where.parent) {
        Slot& slot = where.parent->inputs[where.input];
        BlockPtr b = std::move(slot.block);
        const blocks::Input* in = findInput(where.parent->def, where.input);
        if (in)
            slot.literal = defaultLiteral(*in);
        return b;
    }
    return nullptr;
}

void BlockEditor::beginDrag(const Hit& hit, ImVec2 mouse) {
    snapshot();
    ImVec2 m = toCanvas(mouse);
    Block* b = hit.block;
    grabOffset_ = {m.x - b->pos.x, m.y - b->pos.y};
    dragPos_ = b->pos;
    bool value = b->def->shape == Shape::Reporter || b->def->shape == Shape::Boolean;
    if (hit.where.parent) {
        dragReporter_ = detach(hit.where);
    } else if (hit.where.list) {
        auto& list = *hit.where.list;
        if (value && list.size() == 1) {
            dragReporter_ = std::move(list[0]);
            list.clear();
        } else {
            // Picking up a block takes everything below it too, like in Scratch.
            for (size_t i = hit.where.index; i < list.size(); ++i)
                dragStack_.push_back(std::move(list[i]));
            list.resize(hit.where.index);
        }
    }
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.blocks.empty(); }),
                  stacks_.end());
    dragging_ = true;
    modified();
}

void BlockEditor::endDrag(ImVec2 mouse, bool overPalette) {
    dragging_ = false;
    (void)mouse;
    if (overPalette) {
        dragStack_.clear();
        dragReporter_.reset();
        modified();
        return;
    }
    if (dragReporter_) {
        // Drop into the nearest compatible input.
        float best = kSnap;
        SlotHit* target = nullptr;
        ImVec2 anchor{dragPos_.x, dragPos_.y + dragReporter_->size.y * 0.5f};
        for (auto& s : slotHits_) {
            const blocks::Input* in = findInput(s.block->def, s.input);
            if (!in || !acceptsBlocks(in->type))
                continue;
            if (in->type == InputType::Condition && dragReporter_->def->shape != Shape::Boolean)
                continue;
            ImVec2 c{s.min.x, (s.min.y + s.max.y) * 0.5f};
            float d = std::hypot(anchor.x - c.x, anchor.y - c.y);
            if (d < best) {
                best = d;
                target = &s;
            }
        }
        if (target) {
            target->block->inputs[target->input].block = std::move(dragReporter_);
        } else {
            Stack s;
            s.pos = dragPos_;
            s.blocks.push_back(std::move(dragReporter_));
            stacks_.push_back(std::move(s));
        }
        modified();
        return;
    }
    if (dragStack_.empty())
        return;
    float dragHeight = 0;
    for (auto& b : dragStack_)
        dragHeight += b->size.y;
    bool hat = dragStack_.front()->def->shape == Shape::Hat;
    AttachPoint* target = nullptr;
    float best = kSnap;
    if (!hat) {
        for (auto& a : attach_) {
            float d = std::hypot(dragPos_.x - a.point.x, dragPos_.y - a.point.y);
            if (d < best) {
                best = d;
                target = &a;
            }
        }
    }
    // Attaching above the top of a stack.
    for (size_t si = 0; si < stacks_.size(); ++si) {
        Stack& s = stacks_[si];
        if (s.blocks.empty() || s.blocks.front()->def->shape == Shape::Hat)
            continue;
        if (s.blocks.front()->def->shape == Shape::Reporter || s.blocks.front()->def->shape == Shape::Boolean)
            continue;
        ImVec2 p{s.pos.x, s.pos.y - dragHeight};
        float d = std::hypot(dragPos_.x - p.x, dragPos_.y - p.y);
        if (d < best) {
            best = d;
            attach_.push_back({&s.blocks, 0, p, static_cast<int>(si)});
            target = &attach_.back();
        }
    }
    if (target) {
        auto& list = *target->list;
        if (target->stack >= 0)
            stacks_[static_cast<size_t>(target->stack)].pos = {dragPos_.x, dragPos_.y};
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(target->index), std::make_move_iterator(dragStack_.begin()),
                    std::make_move_iterator(dragStack_.end()));
    } else {
        Stack s;
        s.pos = dragPos_;
        s.blocks = std::move(dragStack_);
        stacks_.push_back(std::move(s));
    }
    dragStack_.clear();
    modified();
}

void BlockEditor::handleCanvasInput(ImVec2 canvasMin, ImVec2 canvasMax, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    ImVec2 cm = toCanvas(mouse);

    if (hovered && io.MouseWheel != 0) {
        if (io.KeyCtrl) {
            float old = zoom_;
            zoom_ = std::clamp(zoom_ * (io.MouseWheel > 0 ? 1.1f : 0.9f), 0.5f, 1.6f);
            // Zoom around the mouse.
            pan_.x = (mouse.x - canvasOrigin_.x) / zoom_ - cm.x;
            pan_.y = (mouse.y - canvasOrigin_.y) / zoom_ - cm.y;
            (void)old;
        } else {
            pan_.y += io.MouseWheel * 40 / zoom_;
            pan_.x += io.MouseWheelH * 40 / zoom_;
        }
    }
    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiKey_Space)))) {
        pan_.x += io.MouseDelta.x / zoom_;
        pan_.y += io.MouseDelta.y / zoom_;
        return;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dragging_) {
        pressed_ = true;
        pressPos_ = mouse;
        pressedOnSlot_ = false;
        pressedHit_ = {};
        for (auto it = slotHits_.rbegin(); it != slotHits_.rend(); ++it)
            if (cm.x >= it->min.x && cm.x <= it->max.x && cm.y >= it->min.y && cm.y <= it->max.y) {
                pressedSlot_ = *it;
                pressedOnSlot_ = true;
                break;
            }
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (cm.x >= it->min.x && cm.x <= it->max.x && cm.y >= it->min.y && cm.y <= it->max.y) {
                pressedHit_ = *it;
                break;
            }
        if (!pressedHit_.block && !pressedOnSlot_)
            pressed_ = false; // clicked empty canvas
    }
    if (pressed_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !dragging_) {
        float moved = std::hypot(mouse.x - pressPos_.x, mouse.y - pressPos_.y);
        if (moved > 5 && pressedHit_.block) {
            beginDrag(pressedHit_, pressPos_);
            pressed_ = false;
        }
    }
    if (pressed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        pressed_ = false;
        if (pressedOnSlot_) {
            editBlock_ = pressedSlot_.block;
            editInput_ = pressedSlot_.input;
            const blocks::Input* in = findInput(editBlock_->def, editInput_);
            std::string text = in ? literalText(*in, editBlock_->inputs[editInput_].literal) : "";
            if (in && (in->type == InputType::Image || in->type == InputType::Sound || in->type == InputType::Prefab ||
                       in->type == InputType::Scene))
                text = editBlock_->inputs[editInput_].literal.asString();
            std::snprintf(editBuffer_, sizeof editBuffer_, "%s", text.c_str());
            if (in && in->type != InputType::Condition)
                openEditPopup_ = true;
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !dragging_) {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (cm.x >= it->min.x && cm.x <= it->max.x && cm.y >= it->min.y && cm.y <= it->max.y) {
                contextBlock_ = it->block;
                contextWhere_ = it->where;
                openContext_ = true;
                break;
            }
    }
    (void)canvasMin;
    (void)canvasMax;
}

void BlockEditor::drawEditPopup() {
    if (openEditPopup_) {
        ImGui::OpenPopup("##block_input");
        openEditPopup_ = false;
    }
    if (!ImGui::BeginPopup("##block_input"))
        return;
    if (!editBlock_) {
        ImGui::EndPopup();
        return;
    }
    const blocks::Input* in = findInput(editBlock_->def, editInput_);
    if (!in) {
        ImGui::EndPopup();
        return;
    }
    Json& literal = editBlock_->inputs[editInput_].literal;
    auto setValue = [&](Json v) {
        snapshot();
        literal = std::move(v);
        modified();
    };
    switch (in->type) {
    case InputType::Key: {
        ImGui::TextDisabled("Choose a key");
        ImGui::BeginChild("keys", {260, 220});
        int col = 0;
        for (const char* k : kKeys) {
            if (col++ % 4)
                ImGui::SameLine();
            if (ImGui::Button(k, {58, 0})) {
                setValue(Json(k));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        break;
    }
    case InputType::Choice:
    case InputType::Name:
        if (!in->options.empty()) {
            for (auto& o : in->options)
                if (ImGui::Selectable(o.c_str(), literal.asString() == o)) {
                    setValue(Json(o));
                    ImGui::CloseCurrentPopup();
                }
            break;
        }
        [[fallthrough]];
    case InputType::Text:
    case InputType::Number:
    case InputType::Value: {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(180);
        bool enter = ImGui::InputText("##value", editBuffer_, sizeof editBuffer_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited() || enter) {
            std::string s = editBuffer_;
            char* end = nullptr;
            double n = std::strtod(s.c_str(), &end);
            bool numeric = !s.empty() && end && *end == '\0';
            if (in->type == InputType::Name)
                setValue(Json(s));
            else if (numeric && in->type != InputType::Text)
                setValue(Json(n));
            else
                setValue(Json(s));
        }
        if (enter)
            ImGui::CloseCurrentPopup();
        break;
    }
    case InputType::Variable: {
        for (auto& name : variableNames())
            if (ImGui::Selectable(name.c_str(), literal.asString() == name)) {
                setValue(Json(name));
                ImGui::CloseCurrentPopup();
            }
        ImGui::Separator();
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("##newvar", newVarName_, sizeof newVarName_);
        ImGui::SameLine();
        if (ImGui::Button("New variable") && newVarName_[0]) {
            snapshot();
            variables_.push_back({newVarName_, Json(0)});
            literal = Json(std::string(newVarName_));
            modified();
            ImGui::CloseCurrentPopup();
        }
        break;
    }
    case InputType::Color: {
        int col = 0;
        for (const char* c : kColorNames) {
            Color color;
            parseBlockColor(c, color);
            if (col++ % 6)
                ImGui::SameLine();
            ImGui::PushID(c);
            if (ImGui::ColorButton(c, {color.r, color.g, color.b, 1}, 0, {30, 30})) {
                setValue(Json(c));
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        break;
    }
    case InputType::Image:
    case InputType::Sound:
    case InputType::Prefab:
    case InputType::Scene: {
        std::vector<std::string> files = assetOptions ? assetOptions(in->type) : std::vector<std::string>{};
        if (files.empty())
            ImGui::TextDisabled("No files of this kind in the project yet.");
        for (auto& file : files)
            if (ImGui::Selectable(file.c_str(), literal.asString() == file)) {
                setValue(Json(file));
                ImGui::CloseCurrentPopup();
            }
        break;
    }
    case InputType::Condition: break;
    }
    ImGui::EndPopup();
}

void BlockEditor::drawContextMenu() {
    if (openContext_) {
        ImGui::OpenPopup("##block_context");
        openContext_ = false;
    }
    if (!ImGui::BeginPopup("##block_context"))
        return;
    if (contextBlock_) {
        if (!contextBlock_->def->help.empty()) {
            ImGui::PushTextWrapPos(280);
            ImGui::TextDisabled("%s", contextBlock_->def->help.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Duplicate")) {
            snapshot();
            Stack s;
            s.pos = {contextBlock_->pos.x + 24, contextBlock_->pos.y + 24};
            s.blocks.push_back(clone(*contextBlock_));
            stacks_.push_back(std::move(s));
            modified();
        }
        if (ImGui::MenuItem("Delete block")) {
            snapshot();
            if (contextWhere_.parent) {
                detach(contextWhere_);
            } else if (contextWhere_.list) {
                auto& list = *contextWhere_.list;
                if (contextWhere_.index < list.size())
                    list.erase(list.begin() + static_cast<std::ptrdiff_t>(contextWhere_.index));
            }
            stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.blocks.empty(); }),
                          stacks_.end());
            contextBlock_ = nullptr;
            modified();
        }
    }
    ImGui::EndPopup();
}

void BlockEditor::drawPalette(ImVec2 origin, ImVec2 size) {
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##palette", size, ImGuiChildFlags_None);
    // Categories as colored buttons in two columns.
    int i = 0;
    for (auto& cat : blocks::categories()) {
        if (i++ % 2)
            ImGui::SameLine();
        bool active = category_ == cat.name;
        Color c = cat.color;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.r, c.g, c.b, active ? 1.0f : 0.45f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.r, c.g, c.b, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.r, c.g, c.b, 1.0f));
        if (ImGui::Button(cat.name.c_str(), {(size.x - 24) * 0.5f, 26}))
            category_ = cat.name;
        ImGui::PopStyleColor(3);
    }
    ImGui::Separator();
    if (category_ == "Variables") {
        ImGui::SetNextItemWidth(size.x - 110);
        ImGui::InputText("##varname", newVarName_, sizeof newVarName_);
        ImGui::SameLine();
        if (ImGui::Button("+ Variable") && newVarName_[0]) {
            snapshot();
            std::string name;
            for (char ch : std::string(newVarName_))
                name += (ch == ' ') ? '_' : ch;
            bool exists = false;
            for (auto& v : variables_)
                exists = exists || v.first == name;
            if (!exists)
                variables_.push_back({name, Json(0)});
            modified();
        }
        for (size_t v = 0; v < variables_.size(); ++v) {
            ImGui::PushID(static_cast<int>(v));
            ImGui::Text("%s =", variables_[v].first.c_str());
            ImGui::SameLine();
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s", variables_[v].second.isString() ? variables_[v].second.asString().c_str()
                                                                                   : variables_[v].second.dump().c_str());
            ImGui::SetNextItemWidth(70);
            if (ImGui::InputText("##start", buf, sizeof buf)) {
                char* end = nullptr;
                double n = std::strtod(buf, &end);
                variables_[v].second = (end && *end == '\0' && buf[0]) ? Json(n) : Json(std::string(buf));
                modified();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                snapshot();
                variables_.erase(variables_.begin() + static_cast<std::ptrdiff_t>(v));
                modified();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    // Draw prototype blocks for the category; dragging one creates a copy.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 savedOrigin = canvasOrigin_, savedPan = pan_;
    float savedZoom = zoom_;
    canvasOrigin_ = ImGui::GetCursorScreenPos();
    pan_ = {0, 0};
    zoom_ = 0.9f;
    float y = 4;
    std::vector<std::pair<BlockPtr, ImVec2>> protos;
    for (auto& def : blocks::catalog()) {
        if (def.category != category_)
            continue;
        BlockPtr b = makeBlock(&def);
        if (!variables_.empty())
            for (auto& in : def.inputs)
                if (in.type == InputType::Variable)
                    b->inputs[in.name].literal = Json(variables_.front().first);
        measure(*b);
        layout(*b, {6, y});
        y += b->size.y + 10;
        protos.emplace_back(std::move(b), ImVec2{});
    }
    ImGui::Dummy({size.x - 20, y * zoom_ + 10});
    for (auto& [b, unused] : protos)
        drawBlock(dl, *b);
    // Start dragging a new block from the palette.
    ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dragging_) {
        ImVec2 cm = toCanvas(mouse);
        for (auto& [b, unused] : protos) {
            if (cm.x >= b->pos.x && cm.x <= b->pos.x + b->size.x && cm.y >= b->pos.y && cm.y <= b->pos.y + b->size.y) {
                snapshot();
                grabOffset_ = {(cm.x - b->pos.x), (cm.y - b->pos.y)};
                bool value = b->def->shape == Shape::Reporter || b->def->shape == Shape::Boolean;
                if (value)
                    dragReporter_ = std::move(b);
                else
                    dragStack_.push_back(std::move(b));
                dragging_ = true;
                break;
            }
        }
    }
    if (ImGui::IsWindowHovered()) {
        ImVec2 cm = toCanvas(mouse);
        for (auto& [b, unused] : protos)
            if (b && cm.x >= b->pos.x && cm.x <= b->pos.x + b->size.x && cm.y >= b->pos.y && cm.y <= b->pos.y + b->size.y &&
                !b->def->help.empty() && !dragging_)
                ImGui::SetTooltip("%s", b->def->help.c_str());
    }
    canvasOrigin_ = savedOrigin;
    pan_ = savedPan;
    zoom_ = savedZoom;
    ImGui::EndChild();
}

bool BlockEditor::draw(const char* id, ImVec2 size) {
    changed_ = false;
    ImGui::PushID(id);
    ImFont* f = font ? font : ImGui::GetFont();
    ImGui::PushFont(f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float codeWidth = showCode ? std::min(380.0f, size.x * 0.34f) : 0;
    float canvasWidth = size.x - paletteWidth_ - codeWidth;

    // Canvas
    ImVec2 canvasMin{origin.x + paletteWidth_, origin.y};
    ImVec2 canvasMax{canvasMin.x + canvasWidth, origin.y + size.y};
    canvasOrigin_ = canvasMin;
    ImGui::SetCursorScreenPos(canvasMin);
    ImGui::InvisibleButton("##canvas", {std::max(canvasWidth, 1.0f), std::max(size.y, 1.0f)},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasMin, canvasMax, true);
    dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(38, 42, 50, 255));
    // Dotted grid
    float step = 24 * zoom_;
    float ox = std::fmod(pan_.x * zoom_, step), oy = std::fmod(pan_.y * zoom_, step);
    for (float x = canvasMin.x + ox; x < canvasMax.x; x += step)
        for (float y = canvasMin.y + oy; y < canvasMax.y; y += step)
            dl->AddCircleFilled({x, y}, 1.2f, IM_COL32(70, 76, 88, 255));

    // Layout, hit collection and drawing
    hits_.clear();
    slotHits_.clear();
    attach_.clear();
    float autoY = 20;
    for (size_t s = 0; s < stacks_.size(); ++s) {
        float height = 0;
        for (auto& b : stacks_[s].blocks)
            height += measure(*b).y;
        if (stacks_[s].autoPlace) {
            stacks_[s].pos = {20, autoY};
            stacks_[s].autoPlace = false;
        }
        autoY = std::max(autoY, stacks_[s].pos.y + height + 30);
        layoutList(stacks_[s].blocks, stacks_[s].pos);
        collect(stacks_[s].blocks, static_cast<int>(s), nullptr);
    }
    for (auto& s : stacks_)
        drawList(dl, s.blocks);

    if (stacks_.empty()) {
        const char* hint = "Drag blocks here from the left.\nStart with a yellow Events block like 'when game starts'.";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText({(canvasMin.x + canvasMax.x - ts.x) * 0.5f, (canvasMin.y + canvasMax.y - ts.y) * 0.5f},
                    IM_COL32(150, 158, 172, 255), hint);
    }

    handleCanvasInput(canvasMin, canvasMax, canvasHovered);

    // Drag feedback
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool overPalette = mouse.x < canvasMin.x;
    if (dragging_) {
        ImVec2 cm = toCanvas(mouse);
        dragPos_ = {cm.x - grabOffset_.x, cm.y - grabOffset_.y};
        if (dragReporter_) {
            measure(*dragReporter_);
            layout(*dragReporter_, dragPos_);
        } else {
            for (auto& b : dragStack_)
                measure(*b);
            layoutList(dragStack_, dragPos_);
            // Show where the blocks would snap.
            if (!dragStack_.empty() && dragStack_.front()->def->shape != Shape::Hat) {
                float best = kSnap;
                const AttachPoint* target = nullptr;
                for (auto& a : attach_) {
                    float d = std::hypot(dragPos_.x - a.point.x, dragPos_.y - a.point.y);
                    if (d < best) {
                        best = d;
                        target = &a;
                    }
                }
                if (target) {
                    ImVec2 p = toScreen(target->point);
                    dl->AddRectFilled({p.x, p.y - 3}, {p.x + 120 * zoom_, p.y + 3}, IM_COL32(255, 255, 255, 200), 3);
                }
            }
        }
        if (dragReporter_) {
            ImVec2 anchor{dragPos_.x, dragPos_.y + dragReporter_->size.y * 0.5f};
            for (auto& s : slotHits_) {
                const blocks::Input* in = findInput(s.block->def, s.input);
                if (!in || !acceptsBlocks(in->type) ||
                    (in->type == InputType::Condition && dragReporter_->def->shape != Shape::Boolean))
                    continue;
                if (std::hypot(anchor.x - s.min.x, anchor.y - (s.min.y + s.max.y) * 0.5f) < kSnap)
                    dl->AddRect(toScreen(s.min), toScreen(s.max), IM_COL32(255, 255, 255, 255), 6, 0, 2.5f);
            }
        }
    }
    dl->PopClipRect();

    // Palette (drawn after the canvas so it's on top; drags can start here)
    dl->AddRectFilled(origin, {origin.x + paletteWidth_, origin.y + size.y}, IM_COL32(30, 33, 40, 255));
    drawPalette(origin, {paletteWidth_, size.y});
    if (dragging_) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        if (overPalette) {
            fg->AddRectFilled(origin, {origin.x + paletteWidth_, origin.y + size.y}, IM_COL32(200, 60, 60, 60));
            ImVec2 ts = ImGui::CalcTextSize("Drop here to delete");
            fg->AddText({origin.x + (paletteWidth_ - ts.x) * 0.5f, origin.y + size.y * 0.5f}, IM_COL32(255, 200, 200, 255),
                        "Drop here to delete");
        }
        ImVec2 savedOrigin = canvasOrigin_;
        canvasOrigin_ = canvasMin;
        if (dragReporter_)
            drawBlock(fg, *dragReporter_, true);
        else
            drawList(fg, dragStack_, true);
        canvasOrigin_ = savedOrigin;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            endDrag(mouse, overPalette);
    }

    drawEditPopup();
    drawContextMenu();

    // Keyboard: undo/redo and delete while the canvas is hovered.
    if (canvasHovered && !ImGui::GetIO().WantTextInput) {
        bool ctrl = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z))
            ImGui::GetIO().KeyShift ? redo() : undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y))
            redo();
    }

    // Generated code
    if (showCode) {
        ImVec2 codeMin{canvasMax.x, origin.y};
        ImGui::SetCursorScreenPos(codeMin);
        ImGui::BeginChild("##code", {codeWidth, size.y}, ImGuiChildFlags_None);
        ImGui::TextDisabled("Your blocks as EasyScript");
        ImGui::SameLine(codeWidth - 110);
        if (onConvertToCode && ImGui::SmallButton("Switch to code"))
            onConvertToCode();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turn these blocks into an EasyScript file you can type in.\nYour blocks are kept too.");
        code();
        preview_.font = codeFont;
        preview_.draw("##preview", {codeWidth - 8, size.y - 34});
        ImGui::EndChild();
    }

    ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
    ImGui::PopFont();
    ImGui::PopID();
    return changed_;
}

bool parseBlockColor(const std::string& name, Color& out) {
    static const std::pair<const char*, uint32_t> table[] = {
        {"red", 0xEF4444},    {"orange", 0xF97316}, {"yellow", 0xFACC15}, {"lime", 0x84CC16},   {"green", 0x22C55E},
        {"teal", 0x14B8A6},   {"cyan", 0x22D3EE},   {"sky", 0x38BDF8},    {"blue", 0x3B82F6},   {"navy", 0x1E3A8A},
        {"purple", 0xA855F7}, {"magenta", 0xD946EF}, {"pink", 0xEC4899},  {"brown", 0x92400E},  {"gold", 0xEAB308},
        {"white", 0xFFFFFF},  {"black", 0x000000},  {"gray", 0x6B7280},   {"grey", 0x6B7280},
    };
    for (auto& [n, hex] : table)
        if (name == n) {
            out = Color::fromHex(hex);
            return true;
        }
    if (!name.empty() && name[0] == '#' && name.size() == 7) {
        out = Color::fromHex(static_cast<uint32_t>(std::strtoul(name.c_str() + 1, nullptr, 16)));
        return true;
    }
    return false;
}

} // namespace aven::editor
