// The script debugger. Click a line number in a script (or press F9) for a breakpoint; when the
// game reaches that line, everything stops, the script opens at the line, and the Debugger window
// shows the calls that led there and every value they can see. Continue (F5) runs on; Step Over
// (F10), Step Into (F11) and Step Out (Shift+F11) go a line at a time.

#include "editor.h"

#include "aven/core/log.h"
#include "aven/runtime/script_system.h"

#include <imgui.h>

namespace aven::editor {

using script::VM;

bool Editor::debugPaused() const {
    return playing_ && game_ && game_->scripts().vm().debugPaused();
}

void Editor::setBreakpoint(const std::string& path, int line, bool on) {
    auto& lines = breakpoints_[path];
    if (on)
        lines.insert(line);
    else
        lines.erase(line);
    if (lines.empty())
        breakpoints_.erase(path);
    for (auto& t : tabs_)
        if (t->path == path && t->code) {
            auto it = breakpoints_.find(path);
            t->code->breakpoints = it == breakpoints_.end() ? std::set<int>{} : it->second;
        }
    if (playing_ && game_ && !replaying_)
        game_->scripts().vm().setBreakpoints(breakpoints_);
}

// Shows where the paused code is: its script, opened at the line (in Aven's own code editor).
void Editor::showDebugFrame(size_t index) {
    debugFrame_ = index;
    for (auto& t : tabs_)
        if (t->code)
            t->code->pausedLine = 0;
    if (index >= debugFrames_.size() || debugFrames_[index].file.empty())
        return;
    const script::DebugFrame& f = debugFrames_[index];
    openScript(f.file, f.line, true);
    for (auto& t : tabs_)
        if (t->path == f.file && t->code)
            t->code->pausedLine = f.line;
}

void Editor::updateDebugger() {
    // Breakpoints clicked in the code editor.
    bool changed = false;
    for (auto& t : tabs_) {
        if (!t->code || !t->code->breakpointsChanged)
            continue;
        t->code->breakpointsChanged = false;
        if (t->code->breakpoints.empty())
            breakpoints_.erase(t->path);
        else
            breakpoints_[t->path] = t->code->breakpoints;
        changed = true;
    }
    if (changed && playing_ && game_ && !replaying_)
        game_->scripts().vm().setBreakpoints(breakpoints_);

    bool paused = debugPaused();
    if (paused && !debugWasPaused_) {
        debugFrames_ = game_->scripts().vm().debugFrames();
        if (!debugFrames_.empty()) {
            const auto& f = debugFrames_[0];
            Log::info("debugger: stopped at ", f.file, " line ", f.line, f.object.empty() ? "" : " (on '" + f.object + "')");
        }
        showDebugFrame(0);
        ImGui::SetWindowFocus("Debugger");
    } else if (!paused && debugWasPaused_) {
        debugFrames_.clear();
        for (auto& t : tabs_)
            if (t->code)
                t->code->pausedLine = 0;
    }
    debugWasPaused_ = paused;
    if (paused && !ImGui::GetIO().WantTextInput) {
        bool shift = ImGui::GetIO().KeyShift;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
            debugStep(VM::Step::Continue);
        else if (ImGui::IsKeyPressed(ImGuiKey_F10, false))
            debugStep(VM::Step::Over);
        else if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
            debugStep(shift ? VM::Step::Out : VM::Step::Into);
    }
}

void Editor::debugStep(VM::Step step) {
    if (!debugPaused())
        return;
    VM& vm = game_->scripts().vm();
    vm.debugResume(step);
    // Stepping usually stops again straight away, a line on.
    debugWasPaused_ = vm.debugPaused();
    debugFrames_ = vm.debugFrames();
    if (debugWasPaused_) {
        showDebugFrame(0);
    } else {
        for (auto& t : tabs_)
            if (t->code)
                t->code->pausedLine = 0;
    }
}

void Editor::drawDebugger() {
    if (!debugPaused() || debugFrames_.empty())
        return;
    ImGui::SetNextWindowSize(ui::fitted({440, 420}), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x - ui::px(470),
                             ImGui::GetMainViewport()->WorkPos.y + ui::px(90)},
                            ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debugger")) {
        ImGui::End();
        return;
    }
    // Buttons first: they can change what's below.
    auto button = [](const char* label, const char* tip) {
        bool pressed = ImGui::Button(label);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
        return pressed;
    };
    std::optional<VM::Step> step;
    if (button("Continue", "Run on until the next breakpoint (F5)"))
        step = VM::Step::Continue;
    if (button("Step Over", "Run this line, and any function it calls, then stop at the next line (F10)"))
        step = VM::Step::Over;
    if (button("Step Into", "Go into the function this line calls (F11)"))
        step = VM::Step::Into;
    if (button("Step Out", "Finish this function and stop back where it was called from (Shift+F11)"))
        step = VM::Step::Out;
    bool stopGame = ImGui::Button("Stop");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop playing");
    if (step) {
        debugStep(*step);
        ImGui::End();
        return;
    }
    if (stopGame) {
        ImGui::End();
        stop();
        return;
    }

    const script::DebugFrame& top = debugFrames_[0];
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored({0.98f, 0.8f, 0.08f, 1}, "Stopped at %s, line %d", top.file.c_str(), top.line);
    if (!top.object.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("on '%s'", top.object.c_str());
    }
    ImGui::PopTextWrapPos();

    ui::sectionHeader("Calls");
    ImGui::TextDisabled("The newest call first. Click one to see its values.");
    for (size_t i = 0; i < debugFrames_.size(); ++i) {
        const auto& f = debugFrames_[i];
        std::string label = (f.function.empty() ? std::string("top of the script") : f.function + "()") + "  -  " + f.file +
                            " line " + std::to_string(f.line) + "##frame" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), debugFrame_ == i))
            showDebugFrame(i);
    }

    const script::DebugFrame& f = debugFrames_[std::min(debugFrame_, debugFrames_.size() - 1)];
    ui::sectionHeader("Values");
    auto table = [&](const char* id, const std::vector<std::pair<std::string, std::string>>& values, const char* kind) {
        if (values.empty())
            return;
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
            return;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ui::px(130));
        ImGui::TableSetupColumn("Value");
        for (auto& [name, value] : values) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", kind);
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    };
    if (f.locals.empty() && f.vars.empty())
        ImGui::TextDisabled("Nothing to show here.");
    table("##locals", f.locals, "In this function (and its values)");
    if (!f.locals.empty() && !f.vars.empty())
        ImGui::Spacing();
    table("##vars", f.vars, "The script's own variables, on this object");

    ui::sectionHeader("Breakpoints");
    std::pair<std::string, int> remove{"", 0};
    for (auto& [file, lines] : breakpoints_)
        for (int line : lines) {
            ImGui::PushID((file + ":" + std::to_string(line)).c_str());
            if (ImGui::SmallButton("x"))
                remove = {file, line};
            ImGui::SameLine();
            if (ImGui::Selectable((file + " line " + std::to_string(line)).c_str()))
                openScript(file, line, true);
            ImGui::PopID();
        }
    if (!remove.first.empty())
        setBreakpoint(remove.first, remove.second, false);
    if (!breakpoints_.empty() && ImGui::SmallButton("Remove all")) {
        auto all = breakpoints_;
        for (auto& [file, lines] : all)
            for (int line : lines)
                setBreakpoint(file, line, false);
    }
    ImGui::End();
}

} // namespace aven::editor
