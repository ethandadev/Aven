// Retro sound effects, following the well-known sfxr synthesis design by Tomas Pettersson:
// an oscillator run at 8x supersampling through an envelope, filters and a phaser.

#include "aven/audio/sfx.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace aven {

namespace {

class Rng {
public:
    explicit Rng(uint32_t seed) : s_(seed ? seed : 0x9E3779B9u) {}
    float frnd(float range) { return next() * range; } // 0..range
    int rnd(int n) { return static_cast<int>(next() * static_cast<float>(n + 1)) % (n + 1); } // 0..n
    bool coin() { return rnd(1) == 1; }

private:
    float next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return (s_ & 0xFFFFFF) / static_cast<float>(0x1000000);
    }
    uint32_t s_;
};

constexpr double kPiD = 3.14159265358979323846;

} // namespace

// ---------------------------------------------------------------- params

Json SfxParams::toJson() const {
    Json j = Json::object();
    static const char* waves[] = {"square", "sawtooth", "sine", "noise", "triangle"};
    j["wave"] = waves[std::clamp(wave, 0, 4)];
    auto put = [&](const char* k, float v) { j[k] = std::round(v * 10000.0) / 10000.0; };
    put("base_freq", baseFreq);
    put("freq_limit", freqLimit);
    put("freq_ramp", freqRamp);
    put("freq_delta_ramp", freqDeltaRamp);
    put("duty", duty);
    put("duty_ramp", dutyRamp);
    put("vibrato_strength", vibratoStrength);
    put("vibrato_speed", vibratoSpeed);
    put("attack", attack);
    put("sustain", sustain);
    put("punch", punch);
    put("decay", decay);
    put("lpf_freq", lpfFreq);
    put("lpf_ramp", lpfRamp);
    put("lpf_resonance", lpfResonance);
    put("hpf_freq", hpfFreq);
    put("hpf_ramp", hpfRamp);
    put("phaser_offset", phaserOffset);
    put("phaser_ramp", phaserRamp);
    put("repeat_speed", repeatSpeed);
    put("arp_speed", arpSpeed);
    put("arp_mod", arpMod);
    put("volume", volume);
    return j;
}

SfxParams SfxParams::fromJson(const Json& j) {
    SfxParams p;
    std::string w = j["wave"].asString("square");
    p.wave = w == "sawtooth" ? Sawtooth : w == "sine" ? Sine : w == "noise" ? Noise : w == "triangle" ? Triangle : Square;
    auto get = [&](const char* k, float& v) { v = j[k].asFloat(v); };
    get("base_freq", p.baseFreq);
    get("freq_limit", p.freqLimit);
    get("freq_ramp", p.freqRamp);
    get("freq_delta_ramp", p.freqDeltaRamp);
    get("duty", p.duty);
    get("duty_ramp", p.dutyRamp);
    get("vibrato_strength", p.vibratoStrength);
    get("vibrato_speed", p.vibratoSpeed);
    get("attack", p.attack);
    get("sustain", p.sustain);
    get("punch", p.punch);
    get("decay", p.decay);
    get("lpf_freq", p.lpfFreq);
    get("lpf_ramp", p.lpfRamp);
    get("lpf_resonance", p.lpfResonance);
    get("hpf_freq", p.hpfFreq);
    get("hpf_ramp", p.hpfRamp);
    get("phaser_offset", p.phaserOffset);
    get("phaser_ramp", p.phaserRamp);
    get("repeat_speed", p.repeatSpeed);
    get("arp_speed", p.arpSpeed);
    get("arp_mod", p.arpMod);
    get("volume", p.volume);
    return p;
}

// ---------------------------------------------------------------- presets

SfxParams sfxPreset(const std::string& kind, uint32_t seed) {
    Rng r(seed);
    SfxParams p;
    if (kind == "coin") {
        p.baseFreq = 0.4f + r.frnd(0.5f);
        p.sustain = r.frnd(0.1f);
        p.decay = 0.1f + r.frnd(0.4f);
        p.punch = 0.3f + r.frnd(0.3f);
        if (r.coin()) {
            p.arpSpeed = 0.5f + r.frnd(0.2f);
            p.arpMod = 0.2f + r.frnd(0.4f);
        }
    } else if (kind == "laser") {
        p.wave = r.rnd(2);
        if (p.wave == SfxParams::Sine && r.coin())
            p.wave = r.rnd(1);
        p.baseFreq = 0.5f + r.frnd(0.5f);
        p.freqLimit = std::max(0.2f, p.baseFreq - 0.2f - r.frnd(0.6f));
        p.freqRamp = -0.15f - r.frnd(0.2f);
        if (r.rnd(2) == 0) {
            p.baseFreq = 0.3f + r.frnd(0.6f);
            p.freqLimit = r.frnd(0.1f);
            p.freqRamp = -0.35f - r.frnd(0.3f);
        }
        if (r.coin()) {
            p.duty = r.frnd(0.5f);
            p.dutyRamp = r.frnd(0.2f);
        } else {
            p.duty = 0.4f + r.frnd(0.5f);
            p.dutyRamp = -r.frnd(0.7f);
        }
        p.sustain = 0.1f + r.frnd(0.2f);
        p.decay = r.frnd(0.4f);
        if (r.coin())
            p.punch = r.frnd(0.3f);
        if (r.rnd(2) == 0) {
            p.phaserOffset = r.frnd(0.2f);
            p.phaserRamp = -r.frnd(0.2f);
        }
        if (r.coin())
            p.hpfFreq = r.frnd(0.3f);
    } else if (kind == "explosion") {
        p.wave = SfxParams::Noise;
        if (r.coin()) {
            p.baseFreq = 0.1f + r.frnd(0.4f);
            p.freqRamp = -0.1f + r.frnd(0.4f);
        } else {
            p.baseFreq = 0.2f + r.frnd(0.7f);
            p.freqRamp = -0.2f - r.frnd(0.2f);
        }
        p.baseFreq *= p.baseFreq;
        if (r.rnd(4) == 0)
            p.freqRamp = 0;
        if (r.rnd(2) == 0)
            p.repeatSpeed = 0.3f + r.frnd(0.5f);
        p.sustain = 0.1f + r.frnd(0.3f);
        p.decay = r.frnd(0.5f);
        if (r.coin()) {
            p.phaserOffset = -0.3f + r.frnd(0.9f);
            p.phaserRamp = -r.frnd(0.3f);
        }
        p.punch = 0.2f + r.frnd(0.6f);
        if (r.coin()) {
            p.vibratoStrength = r.frnd(0.7f);
            p.vibratoSpeed = r.frnd(0.6f);
        }
        if (r.rnd(2) == 0) {
            p.arpSpeed = 0.6f + r.frnd(0.3f);
            p.arpMod = 0.8f - r.frnd(1.6f);
        }
    } else if (kind == "powerup") {
        if (r.coin())
            p.wave = SfxParams::Sawtooth;
        else
            p.duty = r.frnd(0.6f);
        p.baseFreq = 0.2f + r.frnd(0.3f);
        if (r.coin()) {
            p.freqRamp = 0.1f + r.frnd(0.4f);
            p.repeatSpeed = 0.4f + r.frnd(0.4f);
        } else {
            p.freqRamp = 0.05f + r.frnd(0.2f);
            if (r.coin()) {
                p.vibratoStrength = r.frnd(0.7f);
                p.vibratoSpeed = r.frnd(0.6f);
            }
        }
        p.sustain = r.frnd(0.4f);
        p.decay = 0.1f + r.frnd(0.4f);
    } else if (kind == "hurt") {
        p.wave = r.rnd(2);
        if (p.wave == SfxParams::Sine)
            p.wave = SfxParams::Noise;
        if (p.wave == SfxParams::Square)
            p.duty = r.frnd(0.6f);
        p.baseFreq = 0.2f + r.frnd(0.6f);
        p.freqRamp = -0.3f - r.frnd(0.4f);
        p.sustain = r.frnd(0.1f);
        p.decay = 0.1f + r.frnd(0.2f);
        if (r.coin())
            p.hpfFreq = r.frnd(0.3f);
    } else if (kind == "jump") {
        p.duty = r.frnd(0.6f);
        p.baseFreq = 0.3f + r.frnd(0.3f);
        p.freqRamp = 0.1f + r.frnd(0.2f);
        p.sustain = 0.1f + r.frnd(0.3f);
        p.decay = 0.1f + r.frnd(0.2f);
        if (r.coin())
            p.hpfFreq = r.frnd(0.3f);
        if (r.coin())
            p.lpfFreq = 1.0f - r.frnd(0.6f);
    } else if (kind == "blip") {
        p.wave = r.rnd(1);
        if (p.wave == SfxParams::Square)
            p.duty = r.frnd(0.6f);
        p.baseFreq = 0.2f + r.frnd(0.4f);
        p.sustain = 0.1f + r.frnd(0.1f);
        p.decay = r.frnd(0.2f);
        p.hpfFreq = 0.1f;
    } else { // random
        p.wave = r.rnd(3);
        p.baseFreq = std::pow(r.frnd(2.0f) - 1.0f, 2.0f);
        if (r.coin())
            p.baseFreq = std::pow(r.frnd(2.0f) - 1.0f, 3.0f) + 0.5f;
        p.freqRamp = std::pow(r.frnd(2.0f) - 1.0f, 5.0f);
        if (p.baseFreq > 0.7f && p.freqRamp > 0.2f)
            p.freqRamp = -p.freqRamp;
        if (p.baseFreq < 0.2f && p.freqRamp < -0.05f)
            p.freqRamp = -p.freqRamp;
        p.freqDeltaRamp = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        p.duty = r.frnd(2.0f) - 1.0f;
        p.dutyRamp = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        p.vibratoStrength = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        p.vibratoSpeed = r.frnd(2.0f) - 1.0f;
        p.attack = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        p.sustain = std::pow(r.frnd(2.0f) - 1.0f, 2.0f);
        p.decay = r.frnd(2.0f) - 1.0f;
        p.punch = std::pow(r.frnd(0.8f), 2.0f);
        if (p.attack + p.sustain + p.decay < 0.2f) {
            p.sustain += 0.2f + r.frnd(0.3f);
            p.decay += 0.2f + r.frnd(0.3f);
        }
        p.lpfResonance = r.frnd(2.0f) - 1.0f;
        p.lpfFreq = 1.0f - std::pow(r.frnd(1.0f), 3.0f);
        p.lpfRamp = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        if (p.lpfFreq < 0.1f && p.lpfRamp < -0.05f)
            p.lpfRamp = -p.lpfRamp;
        p.hpfFreq = std::pow(r.frnd(1.0f), 5.0f);
        p.hpfRamp = std::pow(r.frnd(2.0f) - 1.0f, 5.0f);
        p.phaserOffset = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        p.phaserRamp = std::pow(r.frnd(2.0f) - 1.0f, 3.0f);
        p.repeatSpeed = r.frnd(2.0f) - 1.0f;
        p.arpSpeed = r.frnd(2.0f) - 1.0f;
        p.arpMod = r.frnd(2.0f) - 1.0f;
        // Keep it within the 0..1 / -1..1 ranges the sliders use.
        for (float* v : {&p.duty, &p.vibratoStrength, &p.vibratoSpeed, &p.attack, &p.decay, &p.lpfResonance, &p.repeatSpeed, &p.arpSpeed})
            *v = std::clamp(std::abs(*v), 0.0f, 1.0f);
    }
    return p;
}

SfxParams sfxMutate(const SfxParams& in, uint32_t seed) {
    Rng r(seed);
    SfxParams p = in;
    auto nudge = [&](float& v, float lo, float hi) {
        if (r.coin())
            v = std::clamp(v + r.frnd(0.1f) - 0.05f, lo, hi);
    };
    nudge(p.baseFreq, 0, 1);
    nudge(p.freqRamp, -1, 1);
    nudge(p.freqDeltaRamp, -1, 1);
    nudge(p.duty, 0, 1);
    nudge(p.dutyRamp, -1, 1);
    nudge(p.vibratoStrength, 0, 1);
    nudge(p.vibratoSpeed, 0, 1);
    nudge(p.attack, 0, 1);
    nudge(p.sustain, 0, 1);
    nudge(p.decay, 0, 1);
    nudge(p.punch, 0, 1);
    nudge(p.lpfResonance, 0, 1);
    nudge(p.lpfFreq, 0, 1);
    nudge(p.lpfRamp, -1, 1);
    nudge(p.hpfFreq, 0, 1);
    nudge(p.hpfRamp, -1, 1);
    nudge(p.phaserOffset, -1, 1);
    nudge(p.phaserRamp, -1, 1);
    nudge(p.repeatSpeed, 0, 1);
    nudge(p.arpSpeed, 0, 1);
    nudge(p.arpMod, -1, 1);
    return p;
}

// ---------------------------------------------------------------- synthesis

std::vector<float> sfxSynthesize(const SfxParams& p) {
    Rng noise(1234567);
    std::vector<float> out;
    int phase = 0;
    double fperiod = 0, fmaxperiod = 0, fslide = 0, fdslide = 0;
    int period = 0;
    float squareDuty = 0, squareSlide = 0;
    double arpMod = 0;
    int arpTime = 0, arpLimit = 0;
    float fltp = 0, fltdp = 0, fltw = 0, fltwD = 0, fltdmp = 0, fltphp = 0, flthp = 0, flthpD = 0;
    float vibPhase = 0, vibSpeed = 0, vibAmp = 0;
    float envVol = 0;
    int envStage = 0, envTime = 0, envLength[3] = {1, 1, 1};
    float fphase = 0, fdphase = 0;
    int iphase = 0, ipp = 0;
    float phaserBuffer[1024] = {};
    float noiseBuffer[32];
    int repTime = 0, repLimit = 0;

    auto reset = [&](bool restart) {
        if (!restart)
            phase = 0;
        fperiod = 100.0 / (p.baseFreq * p.baseFreq + 0.001);
        period = static_cast<int>(fperiod);
        fmaxperiod = 100.0 / (p.freqLimit * p.freqLimit + 0.001);
        fslide = 1.0 - std::pow(static_cast<double>(p.freqRamp), 3.0) * 0.01;
        fdslide = -std::pow(static_cast<double>(p.freqDeltaRamp), 3.0) * 0.000001;
        squareDuty = 0.5f - p.duty * 0.5f;
        squareSlide = -p.dutyRamp * 0.00005f;
        arpMod = p.arpMod >= 0 ? 1.0 - std::pow(static_cast<double>(p.arpMod), 2.0) * 0.9 : 1.0 + std::pow(static_cast<double>(p.arpMod), 2.0) * 10.0;
        arpTime = 0;
        arpLimit = p.arpSpeed >= 1.0f ? 0 : static_cast<int>(std::pow(1.0f - p.arpSpeed, 2.0f) * 20000 + 32);
        if (!restart) {
            fltp = fltdp = 0;
            fltw = std::pow(p.lpfFreq, 3.0f) * 0.1f;
            fltwD = 1.0f + p.lpfRamp * 0.0001f;
            fltdmp = std::min(0.8f, 5.0f / (1.0f + std::pow(p.lpfResonance, 2.0f) * 20.0f) * (0.01f + fltw));
            fltphp = 0;
            flthp = std::pow(p.hpfFreq, 2.0f) * 0.1f;
            flthpD = 1.0f + p.hpfRamp * 0.0003f;
            vibPhase = 0;
            vibSpeed = std::pow(p.vibratoSpeed, 2.0f) * 0.01f;
            vibAmp = p.vibratoStrength * 0.5f;
            envVol = 0;
            envStage = 0;
            envTime = 0;
            envLength[0] = std::max(1, static_cast<int>(p.attack * p.attack * 100000.0f));
            envLength[1] = std::max(1, static_cast<int>(p.sustain * p.sustain * 100000.0f));
            envLength[2] = std::max(1, static_cast<int>(p.decay * p.decay * 100000.0f));
            fphase = std::pow(p.phaserOffset, 2.0f) * 1020.0f;
            if (p.phaserOffset < 0)
                fphase = -fphase;
            fdphase = std::pow(p.phaserRamp, 2.0f);
            if (p.phaserRamp < 0)
                fdphase = -fdphase;
            iphase = std::abs(static_cast<int>(fphase));
            ipp = 0;
            std::fill(std::begin(phaserBuffer), std::end(phaserBuffer), 0.0f);
            for (float& n : noiseBuffer)
                n = noise.frnd(2.0f) - 1.0f;
            repTime = 0;
            repLimit = p.repeatSpeed == 0 ? 0 : static_cast<int>(std::pow(1.0f - p.repeatSpeed, 2.0f) * 20000 + 32);
        }
    };
    reset(false);
    // jsfxr's loudness curve: a square wave at volume 0.5 peaks near 0.3, leaving room for punch.
    const float gain = std::exp(p.volume) - 1.0f;
    const size_t maxSamples = kSfxSampleRate * 6; // never longer than 6 seconds
    bool playing = true;
    while (playing && out.size() < maxSamples) {
        ++repTime;
        if (repLimit != 0 && repTime >= repLimit) {
            repTime = 0;
            reset(true);
        }
        ++arpTime;
        if (arpLimit != 0 && arpTime >= arpLimit) {
            arpLimit = 0;
            fperiod *= arpMod;
        }
        fslide += fdslide;
        fperiod *= fslide;
        if (fperiod > fmaxperiod) {
            fperiod = fmaxperiod;
            if (p.freqLimit > 0)
                playing = false;
        }
        double rfperiod = fperiod;
        if (vibAmp > 0) {
            vibPhase += vibSpeed;
            rfperiod = fperiod * (1.0 + std::sin(vibPhase) * vibAmp);
        }
        period = std::max(8, static_cast<int>(rfperiod));
        squareDuty = std::clamp(squareDuty + squareSlide, 0.0f, 0.5f);
        ++envTime;
        if (envTime > envLength[envStage]) {
            envTime = 0;
            if (++envStage == 3)
                break;
        }
        if (envStage == 0)
            envVol = static_cast<float>(envTime) / envLength[0];
        else if (envStage == 1)
            envVol = 1.0f + (1.0f - static_cast<float>(envTime) / envLength[1]) * 2.0f * p.punch;
        else
            envVol = 1.0f - static_cast<float>(envTime) / envLength[2];
        fphase += fdphase;
        iphase = std::min(1023, std::abs(static_cast<int>(fphase)));
        if (flthpD != 0)
            flthp = std::clamp(flthp * flthpD, 0.00001f, 0.1f);
        float ssample = 0;
        for (int si = 0; si < 8; ++si) {
            float sample = 0;
            ++phase;
            if (phase >= period) {
                phase %= period;
                if (p.wave == SfxParams::Noise)
                    for (float& n : noiseBuffer)
                        n = noise.frnd(2.0f) - 1.0f;
            }
            float fp = static_cast<float>(phase) / period;
            switch (p.wave) {
            case SfxParams::Square: sample = fp < squareDuty ? 0.5f : -0.5f; break;
            case SfxParams::Sawtooth: sample = 1.0f - fp * 2; break;
            case SfxParams::Sine: sample = static_cast<float>(std::sin(fp * 2 * kPiD)); break;
            case SfxParams::Noise: sample = noiseBuffer[phase * 32 / period]; break;
            case SfxParams::Triangle: sample = (fp < 0.5f ? fp * 4 - 1 : 3 - fp * 4) * 0.5f; break;
            }
            float pp = fltp;
            fltw = std::clamp(fltw * fltwD, 0.0f, 0.1f);
            if (p.lpfFreq != 1.0f) {
                fltdp += (sample - fltp) * fltw;
                fltdp -= fltdp * fltdmp;
            } else {
                fltp = sample;
                fltdp = 0;
            }
            fltp += fltdp;
            fltphp += fltp - pp;
            fltphp -= fltphp * flthp;
            sample = fltphp;
            phaserBuffer[ipp & 1023] = sample;
            sample += phaserBuffer[(ipp - iphase + 1024) & 1023];
            ipp = (ipp + 1) & 1023;
            ssample += sample * envVol;
        }
        ssample = ssample / 8 * gain;
        out.push_back(std::clamp(ssample, -1.0f, 1.0f));
    }
    return out;
}

bool writeWav(const std::filesystem::path& path, const std::vector<float>& samples, int sampleRate) {
    std::string d;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            d += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    auto u16 = [&](uint16_t v) {
        d += static_cast<char>(v & 0xFF);
        d += static_cast<char>(v >> 8);
    };
    uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    d += "RIFF";
    u32(36 + dataBytes);
    d += "WAVEfmt ";
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate) * 2);
    u16(2);
    u16(16);
    d += "data";
    u32(dataBytes);
    for (float s : samples)
        u16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f)));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return fs::writeText(path, d);
}

// ---------------------------------------------------------------- preview

struct SoundPreview::Impl {
    ma_device device{};
    bool ready = false;
    std::mutex mutex;
    std::vector<float> samples;
    std::atomic<size_t> position{0};

    static void callback(ma_device* dev, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        auto* out = static_cast<float*>(output);
        std::lock_guard lock(self->mutex);
        size_t pos = self->position;
        for (ma_uint32 i = 0; i < frames; ++i)
            out[i] = pos < self->samples.size() ? self->samples[pos++] : 0.0f;
        self->position = pos;
    }
};

SoundPreview::SoundPreview() : impl_(std::make_unique<Impl>()) {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = kSfxSampleRate;
    config.dataCallback = &Impl::callback;
    config.pUserData = impl_.get();
    impl_->ready = ma_device_init(nullptr, &config, &impl_->device) == MA_SUCCESS && ma_device_start(&impl_->device) == MA_SUCCESS;
    if (!impl_->ready)
        Log::warn("No sound output found, so sounds can't be previewed.");
}

SoundPreview::~SoundPreview() {
    if (impl_->ready)
        ma_device_uninit(&impl_->device);
}

void SoundPreview::play(std::vector<float> samples) {
    std::lock_guard lock(impl_->mutex);
    impl_->samples = std::move(samples);
    impl_->position = 0;
}

bool SoundPreview::playing() const { return impl_->position < impl_->samples.size(); }

float SoundPreview::progress() const {
    size_t n = impl_->samples.size();
    return n ? static_cast<float>(impl_->position) / static_cast<float>(n) : 1.0f;
}

} // namespace aven
