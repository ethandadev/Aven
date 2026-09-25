// Sound Maker: make retro sound effects (coins, lasers, jumps, explosions) by clicking a preset
// and tweaking sliders, then save them into the project and use them on an object.

#include "editor.h"

#include "aven/audio/sfx.h"
#include "aven/core/fs.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <random>

namespace aven::editor {

namespace {

struct Preset {
    const char* kind;
    const char* label;
    const char* tip;
};

const Preset kPresets[] = {
    {"coin", "Coin", "Picking something up"},       {"laser", "Laser", "Shooting"},
    {"explosion", "Explosion", "Booms and crashes"}, {"powerup", "Power-up", "Getting stronger"},
    {"hurt", "Hurt", "Getting hit"},                 {"jump", "Jump", "Jumping"},
    {"blip", "Blip", "Menus and buttons"},           {"random", "Surprise me", "Anything at all"},
};

bool slider(const char* label, float& v, float lo, float hi, const char* tip) {
    ImGui::SetNextItemWidth(-140);
    bool changed = ImGui::SliderFloat(label, &v, lo, hi, "%.2f");
    if (tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    return changed;
}

} // namespace

void Editor::openSoundMaker() { showSoundMaker_ = focusSoundMaker_ = true; }

void Editor::drawSoundMaker() {
    ui::placeWindow({760, 640}, {0.5f, 0.5f});
    if (focusSoundMaker_) {
        ImGui::SetNextWindowFocus();
        focusSoundMaker_ = false;
    }
    if (!ImGui::Begin("Sound Maker###SoundMaker", &showSoundMaker_)) {
        ImGui::End();
        return;
    }
    if (!soundPreview_)
        soundPreview_ = std::make_unique<SoundPreview>();
    bool changed = false;
    auto newSeed = [] { return static_cast<uint32_t>(std::random_device{}()); };

    // Presets.
    ImGui::TextDisabled("Pick a kind of sound. Click again for another version.");
    for (size_t i = 0; i < std::size(kPresets); ++i) {
        if (i)
            ImGui::SameLine();
        if (ImGui::Button(kPresets[i].label)) {
            sfx_ = sfxPreset(kPresets[i].kind, newSeed());
            if (sfxName_.empty() || sfxName_.rfind(sfxKind_, 0) == 0)
                sfxName_ = kPresets[i].kind;
            sfxKind_ = kPresets[i].kind;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", kPresets[i].tip);
    }
    if (ImGui::Button("Change it a little")) {
        sfx_ = sfxMutate(sfx_, newSeed());
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Play  (Space)") || (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space)))
        soundPreview_->play(sfxSamples_);
    ImGui::SameLine();
    ImGui::Checkbox("Play when changed", &sfxAutoPlay_);

    // Waveform. The first one is made quietly, so opening the window doesn't beep.
    bool quiet = sfxSamples_.empty() && !changed;
    changed |= quiet;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = 90;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::GetColorU32(ImGuiCol_FrameBg), 6);
    if (!sfxSamples_.empty()) {
        ImU32 col = ImGui::GetColorU32(ImGuiCol_SliderGrab);
        size_t n = sfxSamples_.size();
        int columns = static_cast<int>(w);
        for (int x = 0; x < columns; ++x) {
            size_t a = n * static_cast<size_t>(x) / static_cast<size_t>(columns), b = std::max(a + 1, n * static_cast<size_t>(x + 1) / static_cast<size_t>(columns));
            float lo = 0, hi = 0;
            for (size_t i = a; i < b && i < n; ++i) {
                lo = std::min(lo, sfxSamples_[i]);
                hi = std::max(hi, sfxSamples_[i]);
            }
            float cy = p.y + h * 0.5f;
            dl->AddLine({p.x + x, cy - hi * h * 0.48f}, {p.x + x, cy - lo * h * 0.48f + 1}, col);
        }
        if (soundPreview_->playing()) {
            float px = p.x + soundPreview_->progress() * w;
            dl->AddLine({px, p.y}, {px, p.y + h}, IM_COL32(255, 255, 255, 200), 2);
        }
        char info[64];
        std::snprintf(info, sizeof info, "%.2f s", static_cast<double>(n) / kSfxSampleRate);
        dl->AddText({p.x + 8, p.y + 6}, ImGui::GetColorU32(ImGuiCol_TextDisabled), info);
    }
    ImGui::Dummy({w, h});

    // Settings.
    ImGui::BeginChild("##sfxsettings", {0, -ImGui::GetFrameHeightWithSpacing() * 3.4f});
    const char* waves[] = {"Square", "Sawtooth", "Sine", "Noise", "Triangle"};
    for (int i = 0; i < 5; ++i) {
        if (i)
            ImGui::SameLine();
        bool sel = sfx_.wave == i;
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        if (ImGui::Button(waves[i])) {
            sfx_.wave = i;
            changed = true;
        }
        if (sel)
            ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("sound shape");
    if (ImGui::CollapsingHeader("Loudness over time", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= slider("Attack", sfx_.attack, 0, 1, "How slowly it fades in");
        changed |= slider("Sustain", sfx_.sustain, 0, 1, "How long it stays loud");
        changed |= slider("Punch", sfx_.punch, 0, 1, "An extra kick at the start");
        changed |= slider("Decay", sfx_.decay, 0, 1, "How slowly it fades out");
        changed |= slider("Volume", sfx_.volume, 0, 1, nullptr);
    }
    if (ImGui::CollapsingHeader("Pitch", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= slider("Start pitch", sfx_.baseFreq, 0, 1, "Low (0) to high (1)");
        changed |= slider("Slide", sfx_.freqRamp, -1, 1, "Pitch goes down (negative) or up (positive)");
        changed |= slider("Slide change", sfx_.freqDeltaRamp, -1, 1, "Makes the slide speed up or slow down");
        changed |= slider("Lowest pitch", sfx_.freqLimit, 0, 1, "The sound stops when the pitch falls this low");
    }
    if (ImGui::CollapsingHeader("Wobble and steps")) {
        changed |= slider("Vibrato depth", sfx_.vibratoStrength, 0, 1, "Wobbly pitch");
        changed |= slider("Vibrato speed", sfx_.vibratoSpeed, 0, 1, nullptr);
        changed |= slider("Jump amount", sfx_.arpMod, -1, 1, "A sudden pitch change partway through (like coins)");
        changed |= slider("Jump time", sfx_.arpSpeed, 0, 1, "When the pitch change happens");
        changed |= slider("Repeat", sfx_.repeatSpeed, 0, 1, "Plays the sound again and again, faster when higher");
    }
    if (ImGui::CollapsingHeader("Tone")) {
        changed |= slider("Square width", sfx_.duty, 0, 1, "Only for Square: thin or full");
        changed |= slider("Width change", sfx_.dutyRamp, -1, 1, nullptr);
        changed |= slider("Phaser", sfx_.phaserOffset, -1, 1, "A swooshy effect");
        changed |= slider("Phaser sweep", sfx_.phaserRamp, -1, 1, nullptr);
        changed |= slider("Muffle", sfx_.lpfFreq, 0, 1, "Lower sounds more muffled (1 = off)");
        changed |= slider("Muffle change", sfx_.lpfRamp, -1, 1, nullptr);
        changed |= slider("Muffle ring", sfx_.lpfResonance, 0, 1, nullptr);
        changed |= slider("Thin out", sfx_.hpfFreq, 0, 1, "Removes the low part of the sound");
        changed |= slider("Thin change", sfx_.hpfRamp, -1, 1, nullptr);
    }
    ImGui::EndChild();
    if (changed) {
        sfxSamples_ = sfxSynthesize(sfx_);
        if (sfxAutoPlay_ && !quiet)
            soundPreview_->play(sfxSamples_);
    }

    // Saving and using.
    ImGui::Separator();
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##sfxname", &sfxName_);
    ImGui::SameLine();
    if (ImGui::Button("Save to sounds/")) {
        std::string base;
        for (char c : sfxName_)
            base += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_';
        if (base.empty())
            base = "sound";
        std::string path = uniqueName("sounds", base, ".wav");
        writeWav(projectDir_ / path, sfxSamples_);
        // The settings too, so the sound can be opened and changed later.
        fs::writeText(projectDir_ / (path.substr(0, path.size() - 4) + ".sfx"), sfx_.toJson().dump(2));
        sfxLastSaved_ = path;
        scanAssets();
        milestone("sounds_made");
        notify("Saved " + path);
    }
    ImGui::SameLine();
    if (ImGui::BeginCombo("##opensfx", "Open one you made...", ImGuiComboFlags_WidthFitPreview)) {
        auto files = projectFiles({".sfx"});
        if (files.empty())
            ImGui::TextDisabled("Nothing saved yet.");
        for (auto& f : files)
            if (ImGui::Selectable(f.c_str())) {
                sfx_ = SfxParams::fromJson(Json::parse(fs::readText(projectDir_ / f).value_or("{}")));
                sfxName_ = stdfs::path(f).stem().string();
                sfxSamples_ = sfxSynthesize(sfx_);
                soundPreview_->play(sfxSamples_);
            }
        ImGui::EndCombo();
    }
    // Put the saved sound on the selected object.
    Entity e = selected();
    if (!sfxLastSaved_.empty() && e && !playing_) {
        ImGui::TextDisabled("Use %s on %s:", sfxLastSaved_.c_str(), scene().info(e).name.c_str());
        auto& reg = scene().registry();
        int buttons = 0;
        for (auto& ci : ComponentRegistry::all()) {
            if (!ci.get(reg, e))
                continue;
            for (auto& f : ci.fields)
                if (f.options.asset == AssetKind::Audio) {
                    ImGui::SameLine();
                    std::string label = ci.name + " " + f.label;
                    if (ImGui::SmallButton(label.c_str())) {
                        recordUndo("Use sound");
                        setFieldValue(e, ci.name, f.name, sfxLastSaved_);
                        notify(ci.name + " now plays " + sfxLastSaved_);
                    }
                    ++buttons;
                }
        }
        if (!buttons) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add an AudioSource")) {
                recordUndo("Add sound");
                addComponentByName(e, "AudioSource");
                setFieldValue(e, "AudioSource", "clip", sfxLastSaved_);
            }
        }
    }
    ImGui::End();
}

} // namespace aven::editor
