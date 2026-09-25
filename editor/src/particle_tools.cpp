// Particle tools: one-click looks (fire, smoke, sparkles...) and a live preview of the
// selected emitters in the scene view, so effects can be tuned without pressing Play.

#include "editor.h"

#include "aven/runtime/particles.h"
#include "particle_presets.h"

#include <imgui.h>

namespace aven::editor {

void Editor::drawParticlePresets(Entity e, const std::vector<Entity>& selection) {
    auto& reg = scene().registry();
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("##preset", "Choose a look...")) {
        for (auto& p : particlePresets()) {
            if (ImGui::Selectable(p.name)) {
                if (!playing_)
                    recordUndo(std::string("Particles: ") + p.name);
                for (Entity target : selection)
                    if (auto* em = reg.tryGet<ParticleEmitter>(target)) {
                        *em = p.settings;
                        if (playing_)
                            noteLiveChange(target, "ParticleEmitter/rate");
                    }
                markDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", p.description);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Start from a ready-made effect, then change any setting below.");
    ImGui::SameLine();
    ImGui::Checkbox("Preview", &prefs.previewParticles);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play the selected particles in the scene view while editing.");
    if (!playing_) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Burst")) {
            for (Entity target : selection)
                if (reg.has<ParticleEmitter>(target))
                    emitParticles(*scene_, target, 30, previewRng_);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shoot out a handful of particles once.");
    }
    (void)e;
}

void Editor::previewParticles(float dt) {
    auto& reg = scene_->registry();
    // Only selected emitters play; others drop their preview particles.
    std::vector<Entity> stale;
    reg.each<ParticleState>([&](Entity x, ParticleState&) {
        if (!prefs.previewParticles || !isSelected(x) || !reg.has<ParticleEmitter>(x))
            stale.push_back(x);
    });
    for (Entity x : stale)
        reg.remove<ParticleState>(x);
    if (!prefs.previewParticles)
        return;
    for (Entity x : selectedEntities())
        if (auto* em = reg.tryGet<ParticleEmitter>(x))
            simulateParticles(*scene_, x, *em, dt, previewRng_);
}

} // namespace aven::editor
