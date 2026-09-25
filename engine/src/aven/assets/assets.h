#pragma once

#include "aven/render/font.h"
#include "aven/render/rhi.h"
#include "aven/scene/components.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aven {

struct TextureAsset {
    rhi::TextureHandle handle;
    int width = 0;
    int height = 0;
    bool missing = false;
};

// Loads and caches project files (images, fonts...). Paths are relative to the
// project folder and always use forward slashes, so projects work on any OS.
class Assets {
public:
    explicit Assets(rhi::Device* device = nullptr);
    ~Assets();

    void setDevice(rhi::Device* device) { device_ = device; }
    rhi::Device* device() const { return device_; }
    void setRoot(const std::filesystem::path& root);
    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path resolve(const std::string& path) const;
    bool exists(const std::string& path) const;

    const TextureAsset& texture(const std::string& path, bool pixelArt = false);
    const TextureAsset& shape(Shape2D shape);
    const TextureAsset& white();
    const TextureAsset& glow(); // soft round dot for particles
    Font& defaultFont();
    Font& monoFont();
    Font& font(const std::string& path); // falls back to the default font

    // Reloads images changed on disk since they were loaded. Returns true if any changed.
    bool reloadChanged();
    void clear();

    // Decodes an image file into RGBA8 pixels.
    static bool loadImage(const std::filesystem::path& path, std::vector<uint8_t>& pixels, int& w, int& h);
    static bool savePng(const std::filesystem::path& path, const uint8_t* rgba, int w, int h, bool flipY);

private:
    struct CachedTexture {
        TextureAsset asset;
        std::filesystem::path file;
        int64_t modified = 0;
        bool pixelArt = false;
    };
    rhi::Device* device_ = nullptr;
    std::filesystem::path root_;
    std::unordered_map<std::string, CachedTexture> textures_;
    std::unordered_map<int, TextureAsset> shapes_;
    TextureAsset white_, missing_, glow_;
    std::unique_ptr<Font> defaultFont_, monoFont_;
    std::unordered_map<std::string, std::unique_ptr<Font>> fonts_;
    std::unordered_set<std::string> warned_;

    TextureAsset upload(const uint8_t* rgba, int w, int h, bool pixelArt, bool mipmaps, const char* label);
    const TextureAsset& missingTexture();
    void warnOnce(const std::string& key, const std::string& message);
};

} // namespace aven
