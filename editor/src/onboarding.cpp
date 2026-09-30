// The welcome tour: the first time Rynax runs (or Help > Welcome Tour), six playful steps with Pip,
// Rynax's little diamond, that set up the profile, how much help Learn mode gives, keys like another
// engine's, and the look. Every answer stays on this computer.
//
//   1 You: name, picture (a critter or their own), pronouns
//   2 How did you find Rynax?
//   3 How much have you coded? (sets the Learn mode level)
//   4 Used Unity, Godot or Unreal? (skipped for people new to code: sets keys and the Code Ladder)
//   5 Make it yours: theme, size, sounds, calm mode...
//   6 Done! (confetti)

#include "avatars.h"
#include "editor.h"
#include "learn_mode.h"

#include "rynax/audio/sfx.h"
#include "rynax/core/fs.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>

namespace rynax::editor {

namespace {

enum Step { StepYou, StepFound, StepCoding, StepEngines, StepLook, StepDone, StepCount };

constexpr float kPi = 3.14159265f;

struct Choice {
    const char* id;
    const char* title;
    const char* line;
    const char* quip; // what Pip says when it's picked
};

const Choice kFound[] = {
    {"YouTube", "YouTube", "A video", "A video found you? Lights, camera... game dev!"},
    {"Google", "Google", "A search", "Searching pays off. You found the good stuff!"},
    {"GitHub", "GitHub", "The code", "A fellow code explorer! Stars are always welcome, hehe."},
    {"Friend or family", "Friend & family", "Someone told me", "Tell them Pip says hi. And thank you!"},
    {"Other", "Other", "Somewhere else", "Ooh, mysterious. I like it."},
};

const Choice kCoding[] = {
    {"0", "Never", "I'm brand new to this", "Everybody starts somewhere! I'll show you around, one step at a time."},
    {"1", "A little", "Blocks, Scratch, or a tutorial or two", "Nice! You already know more than you think."},
    {"2", "Some", "I've made a few small things", "You'll feel right at home. Let's build something bigger!"},
    {"3", "Lots", "I code for fun or for work", "A pro! I'll stay out of your way. Mostly."},
};

const char* kEngines[] = {"Unity", "Godot", "Unreal"};
const char* kEngineLanguage[] = {"C# (Unity)", "GDScript (Godot)", "C++ (Unreal)"};

const char* kNameIdeas[] = {"Captain Pixel", "Nova", "Sam", "Pixel Wizard", "Luna", "Rex", "Kiki", "Blaze"};

const char* kPokes[] = {"Boing!", "Hehe, that tickles!", "I'm a diamond. A game-making diamond.",
                        "Fun fact: I'm made of four triangles and a lot of enthusiasm.", "Wheee!",
                        "Did you know? Every game starts as a square that moves."};

const uint32_t kProfileColors[] = {0x8B5CF6, 0x3B82F6, 0x06B6D4, 0x22C55E, 0xEAB308, 0xF97316, 0xEF4444, 0xEC4899};

const char* kPronouns[] = {"he/him", "she/her", "they/them"};

ImU32 withAlpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

float frand(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>((seed >> 8) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

// Text whose letters bob up and down in a wave (still in calm mode).
void wavyText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, float t, bool still) {
    float x = pos.x;
    int i = 0;
    for (const char* s = text; *s;) {
        unsigned int c = 0;
        int n = ImTextCharFromUtf8(&c, s, nullptr);
        if (n <= 0)
            break;
        char buf[8] = {};
        std::copy(s, s + std::min(n, 7), buf);
        float y = still ? 0.0f : std::sin(t * 3.0f - static_cast<float>(i) * 0.35f) * size * 0.06f;
        dl->AddText(font, size, {x, pos.y + y}, col, buf);
        x += font->CalcTextSizeA(size, FLT_MAX, 0, buf).x;
        s += n;
        ++i;
    }
}

// Pip: Rynax's logo diamond with a face. It bobs, blinks, looks at the mouse and hops when happy.
void drawPip(ImDrawList* dl, ImVec2 c, float s, float t, bool calm, ImU32 body, ImVec2 mouse, float hop, bool excited) {
    float bob = calm ? 0.0f : std::sin(t * 2.2f) * s * 0.05f;
    float jump = calm || hop >= 1.0f ? 0.0f : std::sin(hop * kPi) * s * 0.4f;
    float squash = calm ? 1.0f : 1.0f + std::sin(t * 2.2f) * 0.025f;
    float groundY = c.y + s * 1.08f;
    dl->AddEllipseFilled({c.x, groundY}, {s * (0.62f - jump / s * 0.2f), s * 0.1f}, IM_COL32(0, 0, 0, 45));
    c.y += bob - jump;
    float w = s * squash, h = s / squash;
    ImVec2 top{c.x, c.y - h}, right{c.x + w, c.y}, bottom{c.x, c.y + h}, left{c.x - w, c.y};
    dl->AddQuadFilled(top, right, bottom, left, body);
    // A lighter facet on the upper left, like a cut gem.
    dl->AddTriangleFilled(top, c, left, IM_COL32(255, 255, 255, 50));
    dl->AddTriangleFilled(bottom, c, right, IM_COL32(0, 0, 0, 30));
    // Eyes follow the mouse.
    ImVec2 look{mouse.x - c.x, mouse.y - c.y};
    float len = std::sqrt(look.x * look.x + look.y * look.y);
    if (len > 1.0f)
        look = {look.x / len, look.y / len};
    bool blink = !calm && std::fmod(t, 3.7f) < 0.12f;
    for (float side : {-1.0f, 1.0f}) {
        ImVec2 e{c.x + side * s * 0.27f, c.y - s * 0.08f};
        if (blink) {
            dl->AddLine({e.x - s * 0.12f, e.y}, {e.x + s * 0.12f, e.y}, IM_COL32(30, 30, 45, 255), std::max(1.5f, s * 0.05f));
            continue;
        }
        dl->AddEllipseFilled(e, {s * 0.13f, s * 0.16f}, IM_COL32(255, 255, 255, 255));
        ImVec2 pupil{e.x + look.x * s * 0.05f, e.y + look.y * s * 0.06f};
        dl->AddCircleFilled(pupil, s * 0.075f, IM_COL32(30, 30, 45, 255));
        dl->AddCircleFilled({pupil.x + s * 0.03f, pupil.y - s * 0.03f}, s * 0.025f, IM_COL32(255, 255, 255, 230));
    }
    ImVec2 m{c.x, c.y + s * 0.2f};
    if (excited) {
        dl->PathArcTo(m, s * 0.14f, 0, kPi, 16);
        dl->PathFillConvex(IM_COL32(60, 20, 40, 255));
        dl->AddCircleFilled({m.x, m.y + s * 0.08f}, s * 0.05f, IM_COL32(255, 120, 140, 255));
    } else {
        dl->PathArcTo({m.x, m.y - s * 0.05f}, s * 0.12f, kPi * 0.2f, kPi * 0.8f, 12);
        dl->PathStroke(IM_COL32(30, 30, 45, 255), 0, std::max(1.5f, s * 0.045f));
    }
    for (float side : {-1.0f, 1.0f})
        dl->AddCircleFilled({c.x + side * s * 0.48f, c.y + s * 0.12f}, s * 0.08f, IM_COL32(255, 120, 150, 110));
}

// A speech bubble under Pip, centered on `tail.x` within [left, left + width], its tail pointing up.
void speechBubble(ImDrawList* dl, ImVec2 tail, float left, float width, const std::string& text, ImFont* font, float size) {
    float pad = size * 0.7f;
    ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, width - pad * 2, text.c_str());
    float w = std::min(width, textSize.x + pad * 2);
    float x = std::clamp(tail.x - w * 0.5f, left, left + width - w);
    ImVec2 p{x, tail.y + size * 0.6f}, q{x + w, p.y + textSize.y + pad * 2};
    ImU32 bg = ImGui::GetColorU32(ImGuiCol_PopupBg), border = ImGui::GetColorU32(ImGuiCol_Border);
    dl->AddRectFilled(p, q, bg, size * 0.8f);
    dl->AddRect(p, q, border, size * 0.8f);
    ImVec2 a{tail.x - size * 0.45f, p.y + 1}, b{tail.x + size * 0.45f, p.y + 1};
    dl->AddTriangleFilled(a, b, tail, bg);
    dl->AddLine({a.x, p.y}, tail, border);
    dl->AddLine({b.x, p.y}, tail, border);
    dl->AddText(font, size, {p.x + pad, p.y + pad}, ImGui::GetColorU32(ImGuiCol_Text), text.c_str(), nullptr, width - pad * 2);
}

} // namespace

// ---------------------------------------------------------------- avatar and little sounds

void Editor::drawProfileAvatar(ImDrawList* dl, ImVec2 center, float radius, float time) {
    ImU32 bg = ImGui::ColorConvertFloat4ToU32({prefs.profileColor.r, prefs.profileColor.g, prefs.profileColor.b, 1});
    if (prefs.avatar == "picture" && !prefs.profilePicture.empty()) {
        const TextureAsset& tex = assets_.texture(prefs.profilePicture);
        if (!tex.missing && tex.width > 0 && tex.height > 0) {
            // Cropped to a square from the middle, then round.
            float aspect = static_cast<float>(tex.width) / static_cast<float>(tex.height);
            ImVec2 uv0{0, 1}, uv1{1, 0};
            if (aspect > 1) {
                float crop = (1 - 1 / aspect) * 0.5f;
                uv0.x = crop;
                uv1.x = 1 - crop;
            } else {
                float crop = (1 - aspect) * 0.5f;
                uv0.y = 1 - crop;
                uv1.y = crop;
            }
            dl->AddCircleFilled(center, radius, bg, 48);
            dl->AddImageRounded(static_cast<ImTextureID>(device_.nativeTexture(tex.handle)), {center.x - radius, center.y - radius},
                                {center.x + radius, center.y + radius}, uv0, uv1, IM_COL32_WHITE, radius);
            return;
        }
    }
    drawCritter(dl, center, radius, prefs.avatar, bg, prefs.reduceMotion ? 0.0f : time);
}

void Editor::uiSound(const char* kind) {
    if (!prefs.uiSounds || !options_.screenshot.empty())
        return;
    if (!soundPreview_)
        soundPreview_ = std::make_unique<SoundPreview>();
    auto& samples = uiSounds_[kind];
    if (samples.empty()) {
        SfxParams p = sfxPreset(kind, 7);
        p.volume *= 0.45f; // (a nudge, not a bang)
        samples = sfxSynthesize(p);
    }
    soundPreview_->play(samples);
}

// Their own picture: a copy kept with the preferences, so moving the original doesn't lose it.
bool Editor::setProfilePicture(const stdfs::path& file) {
    std::string ext = fs::extension(file);
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga") {
        notify("Pick a .png or .jpg picture.", true);
        return false;
    }
    stdfs::path dir = fs::userDataDir("Rynax Editor");
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    stdfs::path copy = dir / ("avatar-" + std::to_string(stamp) + ext); // (a new name each time: textures are cached by name)
    std::error_code ec;
    stdfs::create_directories(dir, ec);
    if (!stdfs::copy_file(file, copy, stdfs::copy_options::overwrite_existing, ec)) {
        notify("Couldn't copy that picture: " + ec.message(), true);
        return false;
    }
    const TextureAsset& tex = assets_.texture(copy.string());
    if (tex.missing) {
        stdfs::remove(copy, ec);
        notify("That picture couldn't be read. Try a .png or .jpg.", true);
        return false;
    }
    if (!prefs.profilePicture.empty() && stdfs::path(prefs.profilePicture).parent_path() == dir)
        stdfs::remove(prefs.profilePicture, ec);
    prefs.profilePicture = copy.string();
    prefs.avatar = "picture";
    prefs.save();
    pipSay("Looking good! Great picture.");
    return true;
}

// ---------------------------------------------------------------- the tour

void Editor::openOnboarding(int step) {
    showOnboarding_ = true;
    onboardStep_ = std::clamp(step, 0, static_cast<int>(StepCount) - 1);
    onboardStepTime_ = 0;
    onboardLevelChosen_ = false;
    onboardKeymap_.clear();
    onboardSay_.clear();
    confetti_.clear();
    onboardFocusName_ = true;
    if (onboardStep_ == StepDone)
        startConfetti();
}

// The keymap the tour will use: the one picked, else like the first engine they know.
std::string Editor::tourKeymap() const {
    if (!onboardKeymap_.empty())
        return onboardKeymap_;
    return prefs.enginesUsed.empty() ? prefs.keymap : prefs.enginesUsed.front();
}

void Editor::pipSay(const std::string& line) {
    onboardSay_ = line;
    onboardSayTime_ = 0;
    pipHop_ = 0;
}

void Editor::startConfetti() {
    confetti_.clear();
    if (prefs.reduceMotion)
        return;
    uint32_t seed = static_cast<uint32_t>(frameCount_) * 2654435761u + 17;
    const ImU32 colors[] = {IM_COL32(255, 99, 132, 255), IM_COL32(255, 205, 86, 255), IM_COL32(75, 192, 192, 255),
                            IM_COL32(54, 162, 235, 255), IM_COL32(153, 102, 255, 255), IM_COL32(255, 159, 64, 255),
                            IM_COL32(120, 220, 120, 255)};
    for (int i = 0; i < 160; ++i) {
        Confetti c;
        c.x = frand(seed);
        c.y = -frand(seed) * 0.6f;
        c.vx = (frand(seed) - 0.5f) * 0.25f;
        c.vy = frand(seed) * 0.2f;
        c.angle = frand(seed) * kPi * 2;
        c.spin = (frand(seed) - 0.5f) * 12;
        c.color = colors[i % 7];
        confetti_.push_back(c);
    }
}

void Editor::finishOnboarding(bool skipped) {
    prefs.onboarded = true;
    if (onboardLevelChosen_ && prefs.codingLevel >= 0)
        prefs.level = prefs.codingLevel + 1; // never coded: Starter ... lots: Pro
    // Keys like the engine they know, unless they've already set up their own.
    if (!skipped || !onboardKeymap_.empty())
        if (std::string k = tourKeymap(); k != prefs.keymap && !keymapCustomized(prefs))
            applyKeymap(prefs, k);
    prefs.ladderEngine = prefs.enginesUsed.empty() ? "" : prefs.enginesUsed.front();
    prefs.save();
    showOnboarding_ = false;
    confetti_.clear();
    if (!skipped)
        uiSound("powerup");
    if (!skipped)
        notify(prefs.profileName.empty() ? "Welcome to Rynax! Pick a template to start." : "Welcome to Rynax, " + prefs.profileName + "! Pick a template to start.");
}

void Editor::drawOnboarding(float dt) {
    if (!showOnboarding_)
        return;
    bool calm = prefs.reduceMotion;
    onboardTime_ += dt;
    onboardStepTime_ += dt;
    onboardSayTime_ += dt;
    pipHop_ = std::min(pipHop_ + dt * 2.2f, 1.0f);
    float t = onboardTime_;
    float em = ImGui::GetFontSize();
    ImFont* big = fonts.big ? fonts.big : ImGui::GetFont();
    ImFont* bold = fonts.bold ? fonts.bold : ImGui::GetFont();
    ImU32 accent = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    ImU32 textDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    bool skipEngines = prefs.codingLevel <= 0; // (new to code: no engines to ask about)

    // A dimmed backdrop over the whole window, and the card in the middle.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##onboarding", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* bg = ImGui::GetWindowDrawList();
    bg->AddRectFilled(vp->Pos, {vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y}, IM_COL32(8, 10, 18, 190));

    ImVec2 cardSize{std::min(em * 58, vp->Size.x - em * 2), std::min(em * 38, vp->Size.y - em * 2)};
    ImVec2 cardPos{vp->Pos.x + (vp->Size.x - cardSize.x) * 0.5f, vp->Pos.y + (vp->Size.y - cardSize.y) * 0.5f};
    // It rises into place when it first appears.
    float rise = calm ? 0.0f : std::max(0.0f, 1.0f - onboardTime_ * 3.0f);
    cardPos.y += rise * rise * em * 3;
    ImGui::SetCursorScreenPos(cardPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, em * 0.9f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {em * 1.4f, em * 1.2f});
    ImGui::BeginChild("##card", cardSize, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float innerW = ImGui::GetContentRegionAvail().x;

    // ---- top: the steps as stars, and "Skip for now"
    int shownSteps = skipEngines ? 5 : 6;
    int shownIndex = onboardStep_ - (skipEngines && onboardStep_ > StepEngines ? 1 : 0);
    for (int i = 0; i < shownSteps; ++i) {
        ImVec2 c{origin.x + em * 0.7f + i * em * 1.6f, origin.y + em * 0.6f};
        float r = em * (i == shownIndex ? 0.55f : 0.4f);
        if (i == shownIndex && !calm)
            r *= 1.0f + std::sin(t * 5) * 0.08f;
        ImU32 col = i <= shownIndex ? accent : ImGui::GetColorU32(ImGuiCol_FrameBg);
        // A five-pointed star.
        for (int k = 0; k < 5; ++k) {
            float a0 = -kPi * 0.5f + k * kPi * 0.4f, a1 = a0 + kPi * 0.2f, a2 = a0 - kPi * 0.2f;
            dl->AddTriangleFilled({c.x + std::cos(a0) * r, c.y + std::sin(a0) * r}, {c.x + std::cos(a1) * r * 0.45f, c.y + std::sin(a1) * r * 0.45f},
                                  {c.x + std::cos(a2) * r * 0.45f, c.y + std::sin(a2) * r * 0.45f}, col);
        }
        dl->AddCircleFilled(c, r * 0.46f, col, 12);
    }
    {
        std::string step = "Step " + std::to_string(shownIndex + 1) + " of " + std::to_string(shownSteps);
        dl->AddText({origin.x + em * 0.2f + shownSteps * em * 1.6f + em * 0.4f, origin.y + em * 0.6f - ImGui::GetFontSize() * 0.5f},
                    textDim, step.c_str());
    }
    if (onboardStep_ != StepDone) {
        const char* skip = "Skip for now";
        ImGui::SetCursorScreenPos({origin.x + innerW - ImGui::CalcTextSize(skip).x - em * 0.6f, origin.y});
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::SmallButton(skip)) {
            ImGui::PopStyleColor(2);
            finishOnboarding(true);
            ImGui::EndChild();
            ImGui::End();
            return;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Everything here is also in Preferences, and Help > Welcome Tour brings this back.");
    }

    // ---- left: Pip, with something to say
    float leftW = em * 13;
    ImVec2 pip{origin.x + leftW * 0.45f, origin.y + em * 6.8f};
    float pipSize = em * 3.2f;
    ImGui::SetCursorScreenPos({pip.x - pipSize, pip.y - pipSize});
    if (ImGui::InvisibleButton("##pip", {pipSize * 2, pipSize * 2.2f})) {
        static int poke = 0;
        pipSay(kPokes[poke++ % IM_ARRAYSIZE(kPokes)]);
        uiSound("jump");
    }
    std::string says = onboardSay_;
    if (says.empty() || onboardSayTime_ > 7.0f) {
        std::string name = prefs.profileName.empty() ? "friend" : prefs.profileName;
        switch (onboardStep_) {
        case StepYou: says = "Hi! I'm Pip, Rynax's little diamond. Let's get to know each other!"; break;
        case StepFound: says = "Nice to meet you, " + name + "! I'm curious..."; break;
        case StepCoding: says = "No wrong answers here. I just want to know how much to help."; break;
        case StepEngines: says = "Coming from another engine? I can speak its language."; break;
        case StepLook: says = "Almost done! Make Rynax feel like home."; break;
        default: says = "Yay! You're all set, " + name + "!"; break;
        }
    }
    bool excited = onboardStep_ == StepDone || (onboardSayTime_ < 1.5f && !onboardSay_.empty());
    drawPip(dl, pip, pipSize * 0.9f, t, calm, accent, ImGui::GetMousePos(), pipHop_, excited);
    speechBubble(dl, {pip.x, pip.y + pipSize * 1.25f}, origin.x, leftW - em * 0.4f, says, ImGui::GetFont(), ImGui::GetFontSize());
    (void)bold;

    // ---- right: this step
    float rightX = origin.x + leftW + em * 1.2f;
    float rightW = innerW - leftW - em * 1.2f;
    float footerH = em * 3.2f;
    ImGui::SetCursorScreenPos({rightX, origin.y + em * 2.0f});
    ImGui::BeginChild("##step", {rightW, cardSize.y - em * 2.4f - footerH - em * 2.0f}, 0, ImGuiWindowFlags_NoBackground);
    ImDrawList* sdl = ImGui::GetWindowDrawList();
    auto title = [&](const std::string& text, const char* subtitle) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float size = big->FontSize * 1.15f;
        wavyText(sdl, big, size, p, ImGui::GetColorU32(ImGuiCol_Text), text.c_str(), t, calm);
        ImGui::Dummy({0, size * 1.15f});
        if (subtitle)
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", subtitle);
        ImGui::Dummy({0, em * 0.3f});
    };
    auto label = [&](const char* text) {
        ImGui::Dummy({0, em * 0.25f});
        ImGui::PushFont(bold);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    };
    // A big, friendly button: rises a little under the mouse, glows when picked.
    auto card = [&](const char* id, ImVec2 size, bool selected, const std::function<void(ImVec2, ImVec2, bool)>& paint) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        float lift = hovered && !calm ? em * 0.12f : 0.0f;
        ImVec2 a{p.x, p.y - lift}, b{p.x + size.x, p.y + size.y - lift};
        if (lift > 0)
            sdl->AddRectFilled({a.x + 2, a.y + 4}, {b.x + 2, b.y + 4}, IM_COL32(0, 0, 0, 40), em * 0.6f);
        sdl->AddRectFilled(a, b, selected ? withAlpha(accent, 0.22f) : ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                           em * 0.6f);
        if (selected)
            sdl->AddRect(a, b, accent, em * 0.6f, 0, 2.5f);
        paint(a, b, hovered);
        if (clicked)
            uiSound("blip");
        return clicked;
    };
    auto chip = [&](const char* text, bool selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, em);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {em * 0.8f, em * 0.35f});
        bool clicked = ImGui::Button(text);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (clicked)
            uiSound("blip");
        return clicked;
    };

    bool next = false; // (Enter in the name box also goes on)
    switch (onboardStep_) {
    case StepYou: {
        title("Let's get to know each other", "Just a few things so Rynax feels like yours. It all stays on this computer.");
        label("What should we call you?");
        if (onboardFocusName_) {
            ImGui::SetKeyboardFocusHere();
            onboardFocusName_ = false;
        }
        ImGui::SetNextItemWidth(std::min(rightW, em * 18));
        const char* idea = kNameIdeas[static_cast<size_t>(onboardTime_ / 2.5f) % IM_ARRAYSIZE(kNameIdeas)];
        std::string hint = std::string("e.g. ") + idea;
        next = ImGui::InputTextWithHint("##name", hint.c_str(), &prefs.profileName, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemDeactivatedAfterEdit() && !prefs.profileName.empty())
            pipSay(prefs.profileName + "! What a great name.");

        label("Pick your picture");
        float r = em * 1.45f, gap = em * 0.45f;
        int perRow = std::max(1, static_cast<int>((rightW + gap) / (r * 2 + gap)));
        auto& all = critters();
        int count = static_cast<int>(all.size()) + 1; // + their own picture
        for (int i = 0; i < count; ++i) {
            if (i % perRow != 0)
                ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool own = i == static_cast<int>(all.size());
            bool selected = own ? prefs.avatar == "picture" : prefs.avatar == all[static_cast<size_t>(i)].id;
            if (ImGui::InvisibleButton("##av", {r * 2, r * 2})) {
                uiSound("blip");
                if (own) {
                    pickingPicture_ = true;
                } else {
                    prefs.avatar = all[static_cast<size_t>(i)].id;
                    pipSay(std::string("A ") + all[static_cast<size_t>(i)].name + "! Excellent taste.");
                }
            }
            bool hovered = ImGui::IsItemHovered();
            float grow = hovered && !calm ? 1.08f : 1.0f;
            ImVec2 c{p.x + r, p.y + r};
            ImU32 back = ImGui::ColorConvertFloat4ToU32({prefs.profileColor.r, prefs.profileColor.g, prefs.profileColor.b, 1});
            if (own) {
                if (prefs.avatar == "picture" && !prefs.profilePicture.empty()) {
                    drawProfileAvatar(sdl, c, r * grow * 0.92f, t);
                } else {
                    // A little "photo": mountains and a sun.
                    sdl->AddCircleFilled(c, r * grow * 0.92f, ImGui::GetColorU32(ImGuiCol_FrameBg), 40);
                    sdl->AddTriangleFilled({c.x - r * 0.55f, c.y + r * 0.35f}, {c.x - r * 0.15f, c.y - r * 0.2f}, {c.x + r * 0.25f, c.y + r * 0.35f},
                                           textDim);
                    sdl->AddTriangleFilled({c.x - r * 0.05f, c.y + r * 0.35f}, {c.x + r * 0.25f, c.y - r * 0.02f}, {c.x + r * 0.55f, c.y + r * 0.35f},
                                           textDim);
                    sdl->AddCircleFilled({c.x + r * 0.28f, c.y - r * 0.35f}, r * 0.13f, textDim);
                }
                if (hovered)
                    ImGui::SetTooltip("Your own picture (or drop one onto this window)");
            } else {
                drawCritter(sdl, c, r * grow * 0.92f, all[static_cast<size_t>(i)].id, back, calm ? 0.0f : t + i * 0.9f);
                if (hovered)
                    ImGui::SetTooltip("%s", all[static_cast<size_t>(i)].name);
            }
            if (selected)
                sdl->AddCircle(c, r * grow, accent, 40, 3.0f);
            ImGui::PopID();
        }
        ImGui::Dummy({0, em * 0.1f});
        ImGui::TextDisabled("Circle color");
        for (int i = 0; i < IM_ARRAYSIZE(kProfileColors); ++i) {
            ImGui::SameLine(0, em * 0.35f);
            Color c = Color::fromHex(kProfileColors[i]);
            ImGui::PushID(100 + i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float s = ImGui::GetFrameHeight();
            if (ImGui::InvisibleButton("##col", {s, s})) {
                prefs.profileColor = c;
                uiSound("blip");
            }
            sdl->AddCircleFilled({p.x + s * 0.5f, p.y + s * 0.5f}, s * 0.42f, ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, 1}), 20);
            if (std::abs(prefs.profileColor.r - c.r) + std::abs(prefs.profileColor.g - c.g) + std::abs(prefs.profileColor.b - c.b) < 0.01f)
                sdl->AddCircle({p.x + s * 0.5f, p.y + s * 0.5f}, s * 0.5f, ImGui::GetColorU32(ImGuiCol_Text), 20, 2.0f);
            ImGui::PopID();
        }

        label("Pronouns (if you like)");
        bool custom = !prefs.pronouns.empty() && std::none_of(std::begin(kPronouns), std::end(kPronouns),
                                                              [&](const char* p) { return prefs.pronouns == p; });
        const char* shown[] = {"He/him", "She/her", "They/them"};
        for (int i = 0; i < 3; ++i) {
            if (i)
                ImGui::SameLine();
            if (chip(shown[i], prefs.pronouns == kPronouns[i])) {
                prefs.pronouns = kPronouns[i];
                onboardPronounOther_ = false;
            }
        }
        ImGui::SameLine();
        if (chip("Something else", custom || onboardPronounOther_)) {
            onboardPronounOther_ = true;
            if (!custom)
                prefs.pronouns.clear();
        }
        ImGui::SameLine();
        if (chip("Rather not say", prefs.pronouns.empty() && !onboardPronounOther_)) {
            prefs.pronouns.clear();
            onboardPronounOther_ = false;
        }
        if (custom || onboardPronounOther_) {
            ImGui::SetNextItemWidth(std::min(rightW, em * 14));
            ImGui::InputTextWithHint("##pronouns", "e.g. xe/xem", &prefs.pronouns);
        }
        break;
    }
    case StepFound: {
        title("How did you find Rynax?", "Just curious! This stays on your computer: Rynax doesn't send it anywhere.");
        float gap = em * 0.6f;
        float w = (rightW - gap * 2) / 3, h = em * 6.2f;
        for (int i = 0; i < IM_ARRAYSIZE(kFound); ++i) {
            const Choice& ch = kFound[i];
            if (i % 3 != 0)
                ImGui::SameLine(0, gap);
            bool isOther = std::string(ch.id) == "Other";
            bool selected = isOther ? (!prefs.foundVia.empty() && std::none_of(std::begin(kFound), std::end(kFound) - 1,
                                                                             [&](const Choice& c) { return prefs.foundVia == c.id; }))
                                    : prefs.foundVia == ch.id;
            if (card(ch.id, {w, h}, selected, [&](ImVec2 a, ImVec2 b, bool hovered) {
                    ImVec2 c{(a.x + b.x) * 0.5f, a.y + em * 2.0f};
                    float s = em * (hovered && !calm ? 1.05f : 0.95f);
                    // A small picture for each.
                    switch (i) {
                    case 0: // a play button
                        sdl->AddRectFilled({c.x - s * 1.1f, c.y - s * 0.75f}, {c.x + s * 1.1f, c.y + s * 0.75f}, IM_COL32(239, 68, 68, 255), s * 0.35f);
                        sdl->AddTriangleFilled({c.x - s * 0.3f, c.y - s * 0.4f}, {c.x + s * 0.45f, c.y}, {c.x - s * 0.3f, c.y + s * 0.4f}, IM_COL32_WHITE);
                        break;
                    case 1: // a magnifying glass
                        sdl->AddCircle({c.x - s * 0.2f, c.y - s * 0.2f}, s * 0.55f, IM_COL32(59, 130, 246, 255), 24, s * 0.22f);
                        sdl->AddLine({c.x + s * 0.2f, c.y + s * 0.2f}, {c.x + s * 0.75f, c.y + s * 0.75f}, IM_COL32(59, 130, 246, 255), s * 0.28f);
                        break;
                    case 2: // code brackets
                        sdl->AddText(bold, s * 1.9f, {c.x - s * 1.2f, c.y - s * 1.0f}, IM_COL32(168, 85, 247, 255), "</>");
                        break;
                    case 3: // two friends
                        for (float side : {-1.0f, 1.0f}) {
                            ImU32 col = side < 0 ? IM_COL32(34, 197, 94, 255) : IM_COL32(234, 179, 8, 255);
                            sdl->AddCircleFilled({c.x + side * s * 0.45f, c.y - s * 0.35f}, s * 0.32f, col);
                            sdl->PathArcTo({c.x + side * s * 0.45f, c.y + s * 0.65f}, s * 0.55f, kPi, kPi * 2, 16);
                            sdl->PathFillConvex(col);
                        }
                        break;
                    default: // a sparkle
                        for (int k = 0; k < 4; ++k) {
                            float ang = k * kPi * 0.5f + (calm ? 0 : t);
                            sdl->AddTriangleFilled({c.x + std::cos(ang) * s * 0.9f, c.y + std::sin(ang) * s * 0.9f},
                                                   {c.x + std::cos(ang + 0.5f) * s * 0.2f, c.y + std::sin(ang + 0.5f) * s * 0.2f},
                                                   {c.x + std::cos(ang - 0.5f) * s * 0.2f, c.y + std::sin(ang - 0.5f) * s * 0.2f},
                                                   IM_COL32(236, 72, 153, 255));
                        }
                        break;
                    }
                    ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0, ch.title);
                    sdl->AddText(bold, bold->FontSize, {c.x - ts.x * 0.5f, a.y + em * 3.6f}, ImGui::GetColorU32(ImGuiCol_Text), ch.title);
                    ImVec2 ls = ImGui::CalcTextSize(ch.line);
                    sdl->AddText({c.x - ls.x * 0.5f, a.y + em * 4.8f}, textDim, ch.line);
                })) {
                prefs.foundVia = isOther ? (selected ? prefs.foundVia : std::string("Other")) : ch.id;
                pipSay(ch.quip);
            }
        }
        bool other = !prefs.foundVia.empty() && std::none_of(std::begin(kFound), std::end(kFound) - 1,
                                                             [&](const Choice& c) { return prefs.foundVia == c.id; });
        if (other) {
            ImGui::Dummy({0, em * 0.3f});
            std::string words = prefs.foundVia == "Other" ? "" : prefs.foundVia;
            ImGui::SetNextItemWidth(std::min(rightW, em * 22));
            if (ImGui::InputTextWithHint("##other", "Where did you hear about Rynax? (optional)", &words))
                prefs.foundVia = words.empty() ? "Other" : words;
        }
        break;
    }
    case StepCoding: {
        title("How much have you coded before?", "This picks how much Rynax shows at first. More unlocks as you go.");
        float h = em * 3.1f;
        for (int i = 0; i < 4; ++i) {
            const Choice& ch = kCoding[i];
            if (card(ch.id, {rightW, h}, prefs.codingLevel == i, [&](ImVec2 a, ImVec2 b, bool) {
                    // Signal bars: one to four.
                    for (int k = 0; k < 4; ++k) {
                        float bh = em * (0.5f + k * 0.35f);
                        float x = a.x + em * 0.9f + k * em * 0.55f;
                        sdl->AddRectFilled({x, b.y - em * 0.8f - bh}, {x + em * 0.38f, b.y - em * 0.8f}, k <= i ? accent : ImGui::GetColorU32(ImGuiCol_FrameBgActive),
                                           em * 0.12f);
                    }
                    float x = a.x + em * 3.6f;
                    sdl->AddText(bold, bold->FontSize, {x, a.y + em * 0.55f}, ImGui::GetColorU32(ImGuiCol_Text), ch.title);
                    sdl->AddText({x, a.y + em * 1.65f}, textDim, ch.line);
                    std::string lvl = std::string("Learn mode: ") + levelName(i + 1);
                    ImVec2 ls = ImGui::CalcTextSize(lvl.c_str());
                    sdl->AddText({b.x - ls.x - em * 0.9f, (a.y + b.y) * 0.5f - ls.y * 0.5f}, textDim, lvl.c_str());
                })) {
                prefs.codingLevel = i;
                onboardLevelChosen_ = true;
                pipSay(ch.quip);
            }
            ImGui::Dummy({0, em * 0.05f});
        }
        if (prefs.codingLevel >= 0)
            ImGui::TextWrapped("%s: %s", levelName(prefs.codingLevel + 1), levelBlurb(prefs.codingLevel + 1));
        break;
    }
    case StepEngines: {
        title("Ever used another game engine?", "Pick any you know. Rynax can use their keys and show your code in their language.");
        float gap = em * 0.6f;
        float w = (rightW - gap * 3) / 4, h = em * 5.0f;
        const ImU32 colors[] = {IM_COL32(60, 64, 72, 255), IM_COL32(71, 140, 191, 255), IM_COL32(30, 30, 36, 255)};
        for (int i = 0; i < 4; ++i) {
            if (i)
                ImGui::SameLine(0, gap);
            bool none = i == 3;
            const char* name = none ? "None of these" : kEngines[i];
            bool selected = none ? prefs.enginesUsed.empty() && !onboardKeymap_.empty()
                                 : std::find(prefs.enginesUsed.begin(), prefs.enginesUsed.end(), name) != prefs.enginesUsed.end();
            if (card(name, {w, h}, selected, [&](ImVec2 a, ImVec2 b, bool) {
                    ImVec2 c{(a.x + b.x) * 0.5f, a.y + em * 1.9f};
                    if (!none) {
                        sdl->AddCircleFilled(c, em * 1.1f, colors[i], 32);
                        char initial[2] = {name[0], 0};
                        ImVec2 is = bold->CalcTextSizeA(bold->FontSize * 1.3f, FLT_MAX, 0, initial);
                        sdl->AddText(bold, bold->FontSize * 1.3f, {c.x - is.x * 0.5f, c.y - is.y * 0.5f}, IM_COL32_WHITE, initial);
                    } else {
                        sdl->AddCircle(c, em * 1.1f, textDim, 32, 2.0f);
                        sdl->AddLine({c.x - em * 0.7f, c.y + em * 0.7f}, {c.x + em * 0.7f, c.y - em * 0.7f}, textDim, 2.0f);
                    }
                    ImVec2 ts = ImGui::CalcTextSize(name);
                    sdl->AddText({c.x - ts.x * 0.5f, a.y + em * 3.5f}, ImGui::GetColorU32(ImGuiCol_Text), name);
                })) {
                if (none) {
                    prefs.enginesUsed.clear();
                    onboardKeymap_ = "Rynax";
                    pipSay("Rynax is your first engine? Then you'll learn the good habits first!");
                } else if (selected) {
                    std::erase(prefs.enginesUsed, std::string(name));
                    onboardKeymap_ = prefs.enginesUsed.empty() ? "Rynax" : prefs.enginesUsed.front();
                } else {
                    prefs.enginesUsed.push_back(name);
                    if (prefs.enginesUsed.size() == 1)
                        onboardKeymap_ = name;
                    const char* quips[] = {"Unity! Ctrl+P plays, just like you're used to.", "Godot! F5 to play, F8 to stop. I've got you.",
                                           "Unreal! Alt+P plays. Welcome to the lighter side!"};
                    pipSay(quips[i]);
                }
            }
        }
        ImGui::Dummy({0, em * 0.4f});
        label("Keyboard shortcuts like");
        std::string pick = tourKeymap();
        for (auto& k : keymaps()) {
            if (&k != &keymaps().front())
                ImGui::SameLine();
            if (chip(k.name, pick == k.name))
                onboardKeymap_ = k.name;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", keyText(k.blurb).c_str());
        }
        if (keymapCustomized(prefs))
            ImGui::TextDisabled("(You've changed some shortcuts already: those are kept.)");
        if (!prefs.enginesUsed.empty()) {
            int index = 0;
            for (int i = 0; i < 3; ++i)
                if (prefs.enginesUsed.front() == kEngines[i])
                    index = i;
            label("The Code Ladder shows your scripts in");
            ImGui::Text("%s, next to EasyScript. (Tools > Code Ladder)", kEngineLanguage[index]);
        }
        ImGui::Dummy({0, em * 0.3f});
        if (ImGui::SmallButton("Read: moving between engines"))
            openExternal("https://github.com/ethandadev/Rynax/blob/main/docs/migrating.md");
        break;
    }
    case StepLook: {
        title("Make it yours", "Change any of these later in Preferences.");
        label("Theme");
        float gap = em * 0.5f;
        auto& themes = themePresets();
        int perRow = 5;
        float w = (rightW - gap * (perRow - 1)) / perRow, h = em * 3.4f;
        for (size_t i = 0; i < themes.size(); ++i) {
            const ThemePreset& th = themes[i];
            if (i % perRow != 0)
                ImGui::SameLine(0, gap);
            auto u32 = [](uint32_t c) { return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255); };
            if (card(th.name, {w, h}, prefs.theme == th.name, [&](ImVec2 a, ImVec2 b, bool) {
                    ImVec2 pa{a.x + em * 0.4f, a.y + em * 0.4f}, pb{b.x - em * 0.4f, a.y + em * 1.9f};
                    sdl->AddRectFilled(pa, pb, u32(th.background), em * 0.3f);
                    sdl->AddRectFilled({pa.x + em * 0.25f, pa.y + em * 0.25f}, {pa.x + (pb.x - pa.x) * 0.45f, pb.y - em * 0.25f}, u32(th.panel), em * 0.2f);
                    sdl->AddCircleFilled({pb.x - em * 0.55f, (pa.y + pb.y) * 0.5f}, em * 0.3f, u32(th.accent), 16);
                    ImVec2 ts = ImGui::CalcTextSize(th.name);
                    float scale = std::min(1.0f, (b.x - a.x - em * 0.4f) / std::max(1.0f, ts.x));
                    sdl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, {(a.x + b.x) * 0.5f - ts.x * scale * 0.5f, a.y + em * 2.15f},
                                 ImGui::GetColorU32(ImGuiCol_Text), th.name);
                })) {
                prefs.theme = th.name;
                prefs.customAccent = false;
                styleDirty_ = true;
                pipSay(std::string(th.name) + "! Ooh, fancy.");
            }
        }
        label("Size");
        struct Size {
            const char* name;
            float scale;
        };
        for (Size s : {Size{"Cozy", 0.9f}, Size{"Normal", 1.0f}, Size{"Big", 1.2f}, Size{"Huge", 1.4f}}) {
            if (s.scale != 0.9f)
                ImGui::SameLine();
            if (chip(s.name, std::abs(prefs.uiScale - s.scale) < 0.01f)) {
                prefs.uiScale = s.scale;
                styleDirty_ = true;
            }
        }
        label("A few more");
        auto toggle = [&](const char* text, bool& value, const char* quip) {
            if (ImGui::Checkbox(text, &value)) {
                uiSound("blip");
                if (value && quip)
                    pipSay(quip);
            }
        };
        bool autosave = prefs.autosaveMinutes > 0;
        toggle("Little sounds (clicks and cheers)", prefs.uiSounds, "Bleep bloop! Can you hear me now?");
        toggle("Calm mode: less motion (no bouncing or confetti)", prefs.reduceMotion, "Okay, I'll keep still. Mostly.");
        if (ImGui::Checkbox("Save my work every few minutes", &autosave))
            prefs.autosaveMinutes = autosave ? 3 : 0;
        toggle("Tips and helpers while I learn", prefs.beginnerHelpers, nullptr);
        bool match = prefs.customAccent;
        if (ImGui::Checkbox("Use my picture's color for buttons", &match)) {
            prefs.customAccent = match;
            prefs.accent = prefs.profileColor;
            styleDirty_ = true;
        }
        break;
    }
    default: { // done
        std::string name = prefs.profileName.empty() ? "friend" : prefs.profileName;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float r = em * 3.2f;
        float bounce = calm ? 0.0f : std::abs(std::sin(onboardStepTime_ * 3.0f)) * em * 0.4f * std::max(0.0f, 1.0f - onboardStepTime_ * 0.3f);
        drawProfileAvatar(sdl, {p.x + rightW * 0.5f, p.y + r + em * 0.4f - bounce}, r, t);
        ImGui::Dummy({0, r * 2 + em * 0.8f});
        std::string done = "You're all set, " + name + "!";
        float size = big->FontSize * 1.25f;
        ImVec2 ts = big->CalcTextSizeA(size, FLT_MAX, 0, done.c_str());
        wavyText(sdl, big, size, {p.x + (rightW - ts.x) * 0.5f, ImGui::GetCursorScreenPos().y}, ImGui::GetColorU32(ImGuiCol_Text), done.c_str(), t, calm);
        ImGui::Dummy({0, size * 1.3f});
        auto centered = [&](const std::string& text, bool dim) {
            ImVec2 s = ImGui::CalcTextSize(text.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (rightW - s.x) * 0.5f));
            if (dim)
                ImGui::TextDisabled("%s", text.c_str());
            else
                ImGui::TextUnformatted(text.c_str());
        };
        std::string summary = std::string("Learn mode: ") + levelName(onboardLevelChosen_ ? prefs.codingLevel + 1 : prefs.level);
        summary += "   |   Keys like " + tourKeymap() + "   |   Theme: " + prefs.theme;
        centered(summary, true);
        ImGui::Dummy({0, em * 0.6f});
        centered("Here's how to start:", false);
        centered("1. Pick a template: each one is a tiny game that already works.", true);
        centered("2. Press Play (" + chordName(boundChord(&prefs, "play")) + ") and try it. Then change anything!", true);
        centered("3. Stuck? Ask Rynax (" + chordName(boundChord(&prefs, "ask")) + ") or press " +
                     chordName(boundChord(&prefs, "explain")) + " on anything.",
                 true);
        break;
    }
    }
    ImGui::EndChild();

    // ---- bottom: Back and Next
    float bw = std::max(em * 8.5f, bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0, "Let's make games!").x + em * 2);
    ImGui::SetCursorScreenPos({origin.x + innerW - bw, origin.y + cardSize.y - em * 2.4f - footerH * 0.5f - ImGui::GetFrameHeight() * 0.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, em);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive));
    const char* nextLabel = onboardStep_ == StepDone ? "Let's make games!" : onboardStep_ == StepLook ? "Finish" : "Next";
    ImGui::PushFont(bold);
    bool enter = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !pickingPicture_;
    if (ImGui::Button(nextLabel, {bw, ImGui::GetFrameHeight() * 1.3f}) || next || enter) {
        if (onboardStep_ == StepDone) {
            ImGui::PopFont();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            finishOnboarding(false);
            ImGui::EndChild();
            ImGui::End();
            return;
        }
        int to = onboardStep_ + 1;
        if (to == StepEngines && skipEngines)
            ++to;
        onboardStep_ = to;
        onboardStepTime_ = 0;
        onboardSay_.clear();
        pipHop_ = 0;
        uiSound(to == StepDone ? "powerup" : "coin");
        if (to == StepDone) {
            startConfetti();
            prefs.save();
        }
    }
    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    if (onboardStep_ > 0 && onboardStep_ != StepDone) {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos({origin.x + innerW - bw * 2 - em * 0.6f, ImGui::GetCursorScreenPos().y});
        if (ImGui::Button("Back", {bw, ImGui::GetFrameHeight() * 1.3f})) {
            int to = onboardStep_ - 1;
            if (to == StepEngines && skipEngines)
                --to;
            onboardStep_ = std::max(0, to);
            onboardStepTime_ = 0;
            onboardSay_.clear();
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    // Confetti over everything, falling across the card.
    if (!confetti_.empty()) {
        ImDrawList* fg = ImGui::GetForegroundDrawList(vp);
        for (auto& c : confetti_) {
            c.vy += 0.9f * dt; // (in card heights per second)
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.angle += c.spin * dt;
            ImVec2 p{cardPos.x + c.x * cardSize.x, cardPos.y + c.y * cardSize.y};
            float w = em * 0.3f, h = em * 0.55f * std::abs(std::cos(c.angle * 0.7f)) + 1;
            float cs = std::cos(c.angle), sn = std::sin(c.angle);
            auto corner = [&](float x, float y) { return ImVec2{p.x + x * cs - y * sn, p.y + x * sn + y * cs}; };
            fg->AddQuadFilled(corner(-w, -h), corner(w, -h), corner(w, h), corner(-w, h), c.color);
        }
        std::erase_if(confetti_, [](const Confetti& c) { return c.y > 1.2f; });
    }

    // Choosing their own picture.
    if (pickingPicture_) {
        ImGui::OpenPopup("Choose a picture");
        pickingPicture_ = false;
    }
    ImGui::SetNextWindowSize({em * 36, em * 26}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Choose a picture", nullptr)) {
        ImGui::TextDisabled("A .png or .jpg. (You can also drop a picture onto Rynax's window.)");
        stdfs::path picked;
        if (drawFileBrowser({".png", ".jpg", ".jpeg", ".bmp", ".tga"}, &picked)) {
            if (setProfilePicture(picked))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace rynax::editor
