#include "aven/core/log.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"
#include "aven/runtime/systems.h"

#include <miniaudio.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace aven {

struct AudioSystem::Impl {
    Game& game;
    ma_engine engine{};
    bool ok = false;
    float master = 1.0f;
    std::vector<std::unique_ptr<ma_sound>> oneShots;
    std::unique_ptr<ma_sound> music;
    std::string musicPath;
    std::unordered_map<Entity, std::unique_ptr<ma_sound>> sources;
    std::unordered_set<std::string> warned;

    explicit Impl(Game& g) : game(g) {
        ma_engine_config config = ma_engine_config_init();
        ok = ma_engine_init(&config, &engine) == MA_SUCCESS;
        if (!ok)
            Log::info("No audio device was found, so the game will run without sound.");
    }

    ~Impl() {
        oneShots.clear();
        sources.clear();
        if (music)
            ma_sound_uninit(music.get());
        music.reset();
        if (ok)
            ma_engine_uninit(&engine);
    }

    std::unique_ptr<ma_sound> load(const std::string& path, ma_uint32 flags) {
        if (!ok || path.empty())
            return nullptr;
        std::string full = game.assets().resolve(path).string();
        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_file(&engine, full.c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
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

AudioSystem::~AudioSystem() {
    for (auto& s : impl_->oneShots)
        impl_->release(s);
    for (auto& [e, s] : impl_->sources)
        impl_->release(s);
}

bool AudioSystem::available() const { return impl_->ok; }

void AudioSystem::start() {
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
        ma_sound_set_volume(s, src->volume);
        ma_sound_set_pitch(s, src->pitch);
        if (src->spatial) {
            Vec3 p = scene.worldPosition(e);
            ma_sound_set_position(s, p.x, p.y, p.z);
        }
        ++it;
    }
}

void AudioSystem::playSound(const std::string& path, float volume, float pitch, std::optional<Vec3>) {
    auto sound = impl_->load(path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION);
    if (!sound)
        return;
    ma_sound_set_volume(sound.get(), volume);
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
        ma_sound_set_volume(impl_->music.get(), volume);
        return;
    }
    stopMusic();
    impl_->music = impl_->load(path, MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION);
    if (!impl_->music)
        return;
    impl_->musicPath = path;
    ma_sound_set_looping(impl_->music.get(), loop);
    ma_sound_set_volume(impl_->music.get(), volume);
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
    auto sound = impl_->load(src->clip, flags);
    if (!sound)
        return;
    ma_sound_set_looping(sound.get(), src->loop);
    ma_sound_set_volume(sound.get(), src->volume);
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

} // namespace aven
