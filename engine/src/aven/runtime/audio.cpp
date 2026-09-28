#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"
#include "aven/runtime/systems.h"

#include <miniaudio.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace aven {

struct AudioSystem::Impl {
    Game& game;
    ma_engine engine{};
    bool ok = false;
    bool tried = false;
    float master = 1.0f;

    // A mixer channel: sounds -> group (volume) -> low-pass -> echo -> speakers.
    struct Bus {
        std::string name;
        ma_sound_group group{};
        ma_lpf_node lowpass{};
        ma_delay_node echo{};
        float volume = 1.0f;
        bool muted = false;
    };
    std::vector<std::unique_ptr<Bus>> buses;

    std::vector<std::unique_ptr<ma_sound>> oneShots;
    std::unique_ptr<ma_sound> music;
    std::string musicPath;
    std::unordered_map<Entity, std::unique_ptr<ma_sound>> sources;
    std::unordered_set<std::string> warned;

    explicit Impl(Game& g) : game(g) {}

    ~Impl() {
        // Sounds first (they feed the buses), then the buses, then the engine.
        for (auto& s : oneShots)
            release(s);
        for (auto& [e, s] : sources)
            release(s);
        release(music);
        for (auto& b : buses) {
            ma_sound_group_uninit(&b->group);
            ma_lpf_node_uninit(&b->lowpass, nullptr);
            ma_delay_node_uninit(&b->echo, nullptr);
        }
        if (ok)
            ma_engine_uninit(&engine);
    }

    // The sound device opens when a game first starts, so tools that only need a Game to look
    // things up (the code editor's index, --check) don't open one.
    bool ensure() {
        if (tried)
            return ok;
        tried = true;
        ma_engine_config config = ma_engine_config_init();
        ok = ma_engine_init(&config, &engine) == MA_SUCCESS;
        if (ok) {
            ma_engine_set_volume(&engine, master);
            createBuses();
        } else {
            Log::info("No audio device was found, so the game will run without sound.");
        }
        return ok;
    }

    void createBuses() {
        ma_uint32 channels = ma_engine_get_channels(&engine), rate = ma_engine_get_sample_rate(&engine);
        ma_node_graph* graph = ma_engine_get_node_graph(&engine);
        for (const AudioBus& settings : game.settings().audioBuses) {
            auto b = std::make_unique<Bus>();
            b->name = settings.name;
            float cutoff = settings.lowpass > 0 ? std::clamp(settings.lowpass, 50.0f, 20000.0f) : 20000.0f;
            ma_lpf_node_config lpf = ma_lpf_node_config_init(channels, rate, cutoff, 2);
            auto frames = static_cast<ma_uint32>(std::clamp(settings.echoDelay, 0.02f, 2.0f) * static_cast<float>(rate));
            ma_delay_node_config delay = ma_delay_node_config_init(channels, rate, frames, 0.35f);
            bool group = ma_sound_group_init(&engine, 0, nullptr, &b->group) == MA_SUCCESS;
            bool lowpass = group && ma_lpf_node_init(graph, &lpf, nullptr, &b->lowpass) == MA_SUCCESS;
            bool echo = lowpass && ma_delay_node_init(graph, &delay, nullptr, &b->echo) == MA_SUCCESS;
            if (!echo) {
                // (take apart what was made, so nothing half-built stays in the sound graph)
                if (lowpass)
                    ma_lpf_node_uninit(&b->lowpass, nullptr);
                if (group)
                    ma_sound_group_uninit(&b->group);
                Log::warn("The audio bus '", settings.name, "' couldn't be made; its sounds play straight to the speakers.");
                continue;
            }
            ma_node_attach_output_bus(&b->group, 0, &b->lowpass, 0);
            ma_node_attach_output_bus(&b->lowpass, 0, &b->echo, 0);
            ma_node_attach_output_bus(&b->echo, 0, ma_engine_get_endpoint(&engine), 0);
            b->volume = settings.volume;
            b->muted = settings.muted;
            buses.push_back(std::move(b));
            apply(*buses.back(), settings);
        }
    }

    void apply(Bus& b, const AudioBus& settings) {
        ma_sound_group_set_volume(&b.group, b.muted ? 0.0f : b.volume);
        float cutoff = settings.lowpass > 0 ? std::clamp(settings.lowpass, 50.0f, 20000.0f) : 20000.0f;
        ma_lpf_config cfg = ma_lpf_config_init(ma_format_f32, ma_engine_get_channels(&engine), ma_engine_get_sample_rate(&engine), cutoff, 2);
        ma_lpf_node_reinit(&cfg, &b.lowpass);
        ma_delay_node_set_wet(&b.echo, std::clamp(settings.echo, 0.0f, 1.0f));
        ma_delay_node_set_dry(&b.echo, 1.0f);
    }

    Bus* bus(const std::string& name) {
        for (auto& b : buses)
            if (b->name == name)
                return b.get();
        if (!name.empty() && warned.insert("bus:" + name).second)
            Log::warn("There's no audio bus called '", name, "'; the sound plays on Effects (Project Settings > Audio mixer).");
        for (auto& b : buses)
            if (b->name == "Effects")
                return b.get();
        return nullptr;
    }

    std::unique_ptr<ma_sound> load(const std::string& path, ma_uint32 flags, const std::string& busName) {
        if (!ensure() || path.empty())
            return nullptr;
        std::filesystem::path full = game.assets().resolve(path);
        // A missing file is the usual failure: catch it here. (miniaudio 0.11.25 reads memory it
        // has just freed when a file fails to load, so it's best not to ask it to.)
        if (!fs::exists(full)) {
            if (warned.insert(path).second)
                Log::warn("Couldn't play the sound '", path, "': there's no such file in the project.");
            return nullptr;
        }
        // Import settings: stream long files from disk instead of decoding them up front.
        if (game.assets().importFor(path).stream && (flags & MA_SOUND_FLAG_DECODE))
            flags = (flags & ~static_cast<ma_uint32>(MA_SOUND_FLAG_DECODE)) | MA_SOUND_FLAG_STREAM;
        auto sound = std::make_unique<ma_sound>();
        Bus* b = bus(busName);
#ifdef _WIN32
        // (the wide path: folders with names in any alphabet work)
        ma_result opened = ma_sound_init_from_file_w(&engine, full.wstring().c_str(), flags, b ? &b->group : nullptr, nullptr, sound.get());
#else
        ma_result opened = ma_sound_init_from_file(&engine, full.string().c_str(), flags, b ? &b->group : nullptr, nullptr, sound.get());
#endif
        if (opened != MA_SUCCESS) {
            if (warned.insert(path).second)
                Log::warn("Couldn't play the sound '", path, "'. Check the file exists (WAV, MP3, OGG or FLAC).");
            return nullptr;
        }
        return sound;
    }

    void release(std::unique_ptr<ma_sound>& s) {
        if (s) {
            ma_sound_uninit(s.get());
            s.reset();
        }
    }
};

AudioSystem::AudioSystem(Game& game) : impl_(std::make_unique<Impl>(game)) {}

Game& AudioSystem::game() const { return impl_->game; }

AudioSystem::~AudioSystem() {
    for (auto& s : impl_->oneShots)
        impl_->release(s);
    for (auto& [e, s] : impl_->sources)
        impl_->release(s);
}

bool AudioSystem::available() const { return impl_->ok; }

void AudioSystem::start() {
    impl_->ensure();
    Scene& scene = impl_->game.scene();
    for (Entity e : scene.registry().entitiesWith<AudioSource>())
        if (scene.isActive(e) && scene.registry().get<AudioSource>(e).playOnStart)
            playSource(e);
}

void AudioSystem::stop() {
    for (auto& s : impl_->oneShots)
        impl_->release(s);
    impl_->oneShots.clear();
    for (auto& [e, s] : impl_->sources)
        impl_->release(s);
    impl_->sources.clear();
    // Music keeps playing across scene changes, like in most games.
}

void AudioSystem::update(float) {
    if (!impl_->ok)
        return;
    auto& shots = impl_->oneShots;
    for (auto it = shots.begin(); it != shots.end();) {
        if (ma_sound_at_end(it->get()) || !ma_sound_is_playing(it->get())) {
            impl_->release(*it);
            it = shots.erase(it);
        } else {
            ++it;
        }
    }
    Scene& scene = impl_->game.scene();
    // The listener is the AudioListener object, or else the camera.
    Entity listener;
    auto listeners = scene.registry().entitiesWith<AudioListener>();
    if (!listeners.empty())
        listener = listeners.front();
    else
        listener = SceneRenderer::findCamera(scene);
    if (listener) {
        Mat4 m = scene.worldMatrix(listener);
        Vec3 fwd = normalize(transformDirection(m, {0, 0, -1}));
        ma_engine_listener_set_position(&impl_->engine, 0, m.m[12], m.m[13], m.m[14]);
        ma_engine_listener_set_direction(&impl_->engine, 0, fwd.x, fwd.y, fwd.z);
    }
    for (auto it = impl_->sources.begin(); it != impl_->sources.end();) {
        Entity e = it->first;
        auto* src = scene.valid(e) ? scene.registry().tryGet<AudioSource>(e) : nullptr;
        if (!src) {
            impl_->release(it->second);
            it = impl_->sources.erase(it);
            continue;
        }
        ma_sound* s = it->second.get();
        ma_sound_set_volume(s, src->volume * game().assets().importFor(src->clip).volume);
        ma_sound_set_pitch(s, src->pitch);
        if (src->spatial) {
            Vec3 p = scene.worldPosition(e);
            ma_sound_set_position(s, p.x, p.y, p.z);
        }
        ++it;
    }
}

void AudioSystem::playSound(const std::string& path, float volume, float pitch, std::optional<Vec3>, const std::string& bus) {
    auto sound = impl_->load(path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, bus);
    if (!sound)
        return;
    ma_sound_set_volume(sound.get(), volume * game().assets().importFor(path).volume);
    ma_sound_set_pitch(sound.get(), pitch);
    ma_sound_start(sound.get());
    // Avoid hundreds of overlapping sounds if a script plays one every frame.
    if (impl_->oneShots.size() > 64) {
        impl_->release(impl_->oneShots.front());
        impl_->oneShots.erase(impl_->oneShots.begin());
    }
    impl_->oneShots.push_back(std::move(sound));
}

void AudioSystem::playMusic(const std::string& path, float volume, bool loop) {
    if (impl_->music && impl_->musicPath == path && ma_sound_is_playing(impl_->music.get())) {
        ma_sound_set_volume(impl_->music.get(), volume * game().assets().importFor(path).volume);
        return;
    }
    stopMusic();
    impl_->music = impl_->load(path, MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, "Music");
    if (!impl_->music)
        return;
    impl_->musicPath = path;
    ma_sound_set_looping(impl_->music.get(), loop);
    ma_sound_set_volume(impl_->music.get(), volume * game().assets().importFor(path).volume);
    ma_sound_start(impl_->music.get());
}

void AudioSystem::stopMusic() {
    impl_->release(impl_->music);
    impl_->musicPath.clear();
}

void AudioSystem::setMasterVolume(float volume) {
    impl_->master = clamp(volume, 0.0f, 1.0f);
    if (impl_->ok)
        ma_engine_set_volume(&impl_->engine, impl_->master);
}

float AudioSystem::masterVolume() const { return impl_->master; }

void AudioSystem::playSource(Entity e) {
    Scene& scene = impl_->game.scene();
    auto* src = scene.registry().tryGet<AudioSource>(e);
    if (!src)
        return;
    stopSource(e);
    ma_uint32 flags = src->loop ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!src->spatial)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    auto sound = impl_->load(src->clip, flags, src->bus);
    if (!sound)
        return;
    ma_sound_set_looping(sound.get(), src->loop);
    ma_sound_set_volume(sound.get(), src->volume * game().assets().importFor(src->clip).volume);
    ma_sound_set_pitch(sound.get(), src->pitch);
    if (src->spatial) {
        ma_sound_set_max_distance(sound.get(), src->range);
        Vec3 p = scene.worldPosition(e);
        ma_sound_set_position(sound.get(), p.x, p.y, p.z);
    }
    ma_sound_start(sound.get());
    impl_->sources[e] = std::move(sound);
}

void AudioSystem::stopSource(Entity e) {
    auto it = impl_->sources.find(e);
    if (it != impl_->sources.end()) {
        impl_->release(it->second);
        impl_->sources.erase(it);
    }
}

void AudioSystem::onDestroy(Entity e) { stopSource(e); }

std::vector<std::string> AudioSystem::busNames() const {
    std::vector<std::string> names;
    for (auto& b : impl_->game.settings().audioBuses)
        names.push_back(b.name);
    return names;
}

bool AudioSystem::setBusVolume(const std::string& name, float volume, std::optional<bool> muted) {
    auto& all = impl_->game.settings().audioBuses;
    auto it = std::find_if(all.begin(), all.end(), [&](const AudioBus& b) { return b.name == name; });
    if (it == all.end())
        return false;
    for (auto& b : impl_->buses)
        if (b->name == name) {
            b->volume = std::clamp(volume, 0.0f, 1.0f);
            if (muted)
                b->muted = *muted;
            ma_sound_group_set_volume(&b->group, b->muted ? 0.0f : b->volume);
            return true;
        }
    return true; // no device, or not started yet: nothing to change
}

float AudioSystem::busVolume(const std::string& name) const {
    for (auto& b : impl_->buses)
        if (b->name == name)
            return b->volume;
    for (auto& b : impl_->game.settings().audioBuses)
        if (b.name == name)
            return b.volume;
    return 0.0f;
}

bool AudioSystem::busMuted(const std::string& name) const {
    for (auto& b : impl_->buses)
        if (b->name == name)
            return b->muted;
    return false;
}

void AudioSystem::applyMixer(const std::vector<AudioBus>& buses) {
    for (auto& settings : buses)
        for (auto& b : impl_->buses)
            if (b->name == settings.name) {
                b->volume = settings.volume;
                b->muted = settings.muted;
                impl_->apply(*b, settings);
            }
}

} // namespace aven
