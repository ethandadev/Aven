#pragma once

#include "aven/core/json.h"
#include "aven/render/font.h"
#include "aven/render/rhi.h"
#include "aven/scene/components.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aven {

// How a file is brought into the game, from the project's import.json (Assets panel > Import
// settings). Everything is optional; the defaults are what Aven always did.
struct ImportSettings {
    // Images
    enum class Filter { Auto, Pixel, Smooth } filter = Filter::Auto; // Auto: as the component says (Pixel Art)
    int maxSize = 0;      // shrink bigger images to fit (0 = full size)
    bool mipmaps = true;  // smoother when small (smooth images only)
    bool clamp = false;   // don't repeat at the edges
    // 3D models
    float scale = 1.0f;   // e.g. 0.01 for models made in centimeters
    // Sounds
    bool stream = false;  // read from disk while playing (long music) instead of all at once
    float volume = 1.0f;

    Json toJson() const; // only what isn't the default
    static ImportSettings fromJson(const Json& j);
    bool operator==(const ImportSettings&) const = default;
};

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

    // Reloads images changed on disk since they were loaded (or whose import settings changed).
    // Returns true if any changed.
    bool reloadChanged();

    // Import settings (import.json in the project). Saving applies them straight away.
    static constexpr const char* kImportFile = "import.json";
    ImportSettings importFor(const std::string& path);
    void setImport(const std::string& path, const ImportSettings& settings);
    // Goes up whenever import.json changes, so renderers know to load models again.
    int importGeneration() const { return importGeneration_; }
    void clear();

    // Decodes an image file into RGBA8 pixels.
    static bool loadImage(const std::filesystem::path& path, std::vector<uint8_t>& pixels, int& w, int& h);
    static bool savePng(const std::filesystem::path& path, const uint8_t* rgba, int w, int h, bool flipY);

private:
    struct CachedTexture {
        TextureAsset asset;
        std::string path;
        std::filesystem::path file;
        int64_t modified = 0;
        bool pixelArt = false;
    };
    Json imports_ = Json::object();
    int64_t importsModified_ = -1;
    std::chrono::steady_clock::time_point importsChecked_;
    int importGeneration_ = 0;
    void loadImports();
    TextureAsset uploadImage(const std::string& path, std::vector<uint8_t>& pixels, int w, int h, bool pixelArt);
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
