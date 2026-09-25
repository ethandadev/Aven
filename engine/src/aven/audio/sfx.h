#pragma once

#include "aven/core/json.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace aven {

// A retro sound effect generator (the classic "sfxr" design): a wave shape, a volume envelope,
// pitch slides, vibrato, arpeggio, filters and a phaser. Values are 0..1 unless noted.
struct SfxParams {
    enum Wave : int { Square, Sawtooth, Sine, Noise, Triangle };
    int wave = Square;
    float baseFreq = 0.3f, freqLimit = 0.0f, freqRamp = 0.0f /* -1..1 */, freqDeltaRamp = 0.0f /* -1..1 */;
    float duty = 0.0f, dutyRamp = 0.0f /* -1..1 */;
    float vibratoStrength = 0.0f, vibratoSpeed = 0.0f;
    float attack = 0.0f, sustain = 0.3f, punch = 0.0f, decay = 0.4f;
    float lpfFreq = 1.0f, lpfRamp = 0.0f /* -1..1 */, lpfResonance = 0.0f, hpfFreq = 0.0f, hpfRamp = 0.0f /* -1..1 */;
    float phaserOffset = 0.0f /* -1..1 */, phaserRamp = 0.0f /* -1..1 */;
    float repeatSpeed = 0.0f;
    float arpSpeed = 0.0f, arpMod = 0.0f /* -1..1 */;
    float volume = 0.5f;

    Json toJson() const;
    static SfxParams fromJson(const Json& j);
};

// "coin", "laser", "explosion", "powerup", "hurt", "jump", "blip" or "random". `seed` picks a variation.
SfxParams sfxPreset(const std::string& kind, uint32_t seed);
// Small random changes, for "more like this but different".
SfxParams sfxMutate(const SfxParams& p, uint32_t seed);
// Mono samples in -1..1 at 44100 Hz.
std::vector<float> sfxSynthesize(const SfxParams& p);
constexpr int kSfxSampleRate = 44100;
bool writeWav(const std::filesystem::path& path, const std::vector<float>& samples, int sampleRate = kSfxSampleRate);

// Plays samples straight away (used by the editor's Sound Maker).
class SoundPreview {
public:
    SoundPreview();
    ~SoundPreview();
    void play(std::vector<float> samples);
    bool playing() const;
    float progress() const; // 0..1 through the current sound

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aven
