#include "aven/assets/assets.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <cmath>

namespace aven {

namespace embedded {
const unsigned char* find(const char* name, std::size_t* size);
}

namespace {

float polygonDistance(const Vec2* v, int n, Vec2 p) {
    float d = dot(p - v[0], p - v[0]);
    float sign = 1.0f;
    for (int i = 0, j = n - 1; i < n; j = i, ++i) {
        Vec2 e = v[j] - v[i];
        Vec2 w = p - v[i];
        Vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0f, 1.0f);
        d = std::min(d, dot(b, b));
        bool c1 = p.y >= v[i].y, c2 = p.y < v[j].y, c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3))
            sign = -sign;
    }
    return sign * std::sqrt(d);
}

// Signed distance (negative inside) for built-in shapes; p is in [-1, 1] with y up.
float shapeDistance(Shape2D shape, Vec2 p) {
    switch (shape) {
    case Shape2D::Square: return std::max(std::abs(p.x), std::abs(p.y)) - 1.0f;
    case Shape2D::Circle: return length(p) - 0.97f;
    case Shape2D::RoundedSquare: {
        float r = 0.35f;
        float qx = std::abs(p.x) - (0.97f - r), qy = std::abs(p.y) - (0.97f - r);
        return length(Vec2(std::max(qx, 0.0f), std::max(qy, 0.0f))) + std::min(std::max(qx, qy), 0.0f) - r;
    }
    case Shape2D::Diamond: {
        const Vec2 v[] = {{0, 0.97f}, {-0.97f, 0}, {0, -0.97f}, {0.97f, 0}};
        return polygonDistance(v, 4, p);
    }
    case Shape2D::Triangle: {
        const Vec2 v[] = {{0, 0.97f}, {-0.97f, -0.85f}, {0.97f, -0.85f}};
        return polygonDistance(v, 3, p);
    }
    case Shape2D::Star: {
        Vec2 v[10];
        for (int i = 0; i < 10; ++i) {
            float angle = kPi * 0.5f + i * kPi / 5.0f;
            float r = i % 2 == 0 ? 0.98f : 0.42f;
            v[i] = {std::cos(angle) * r, std::sin(angle) * r - 0.06f};
        }
        return polygonDistance(v, 10, p);
    }
    case Shape2D::Heart: {
        // Inigo Quilez's heart, scaled to fill the square.
        const float scale = 1.65f;
        Vec2 q = (p - Vec2(0, -0.9f)) / scale;
        q.x = std::abs(q.x);
        float d;
        if (q.y + q.x > 1.0f) {
            d = length(q - Vec2(0.25f, 0.75f)) - std::sqrt(2.0f) / 4.0f;
        } else {
            Vec2 a = q - Vec2(0, 1);
            Vec2 b = q - Vec2(0.5f, 0.5f) * std::max(q.x + q.y, 0.0f);
            d = std::sqrt(std::min(dot(a, a), dot(b, b))) * (q.x - q.y > 0 ? 1.0f : -1.0f);
        }
        return d * scale;
    }
    }
    return 0;
}

} // namespace

Assets::Assets(rhi::Device* device) : device_(device) {}

Assets::~Assets() {
    clear();
}

void Assets::clear() {
    if (device_) {
        for (auto& [k, t] : textures_)
            if (!t.asset.missing)
                device_->destroy(t.asset.handle);
        for (auto& [k, t] : shapes_)
            device_->destroy(t.handle);
        if (white_.handle)
            device_->destroy(white_.handle);
        if (missing_.handle)
            device_->destroy(missing_.handle);
        if (glow_.handle)
            device_->destroy(glow_.handle);
    }
    textures_.clear();
    shapes_.clear();
    white_ = {};
    missing_ = {};
    glow_ = {};
    fonts_.clear();
    defaultFont_.reset();
    monoFont_.reset();
}

void Assets::setRoot(const std::filesystem::path& root) {
    root_ = root;
}

std::filesystem::path Assets::resolve(const std::string& path) const {
    std::filesystem::path p(path);
    if (p.is_absolute() || root_.empty())
        return p;
    return root_ / p;
}

bool Assets::exists(const std::string& path) const {
    return !path.empty() && fs::exists(resolve(path));
}

void Assets::warnOnce(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second)
        Log::warn(message);
}

TextureAsset Assets::upload(const uint8_t* rgba, int w, int h, bool pixelArt, bool mipmaps, const char* label) {
    TextureAsset t;
    t.width = w;
    t.height = h;
    if (!device_)
        return t;
    rhi::TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = rhi::PixelFormat::RGBA8;
    d.filter = pixelArt ? rhi::Filter::Nearest : rhi::Filter::Linear;
    d.wrap = rhi::Wrap::Repeat;
    d.mipmaps = mipmaps && !pixelArt;
    d.data = rgba;
    d.label = label;
    t.handle = device_->createTexture(d);
    return t;
}

bool Assets::loadImage(const std::filesystem::path& path, std::vector<uint8_t>& pixels, int& w, int& h) {
    auto bytes = fs::readBinary(path);
    if (!bytes)
        return false;
    int channels = 0;
    // GPU textures have their first row at the bottom, so flip while loading.
    stbi_set_flip_vertically_on_load_thread(1);
    stbi_uc* data = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &channels, 4);
    if (!data)
        return false;
    pixels.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    return true;
}

bool Assets::savePng(const std::filesystem::path& path, const uint8_t* rgba, int w, int h, bool flipY) {
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    stbi_flip_vertically_on_write(flipY ? 1 : 0);
    int ok = stbi_write_png(path.string().c_str(), w, h, 4, rgba, w * 4);
    stbi_flip_vertically_on_write(0);
    return ok != 0;
}

const TextureAsset& Assets::missingTexture() {
    if (!missing_.handle && device_) {
        // Pink checkerboard: impossible to miss, so beginners notice a broken image path.
        uint8_t px[8 * 8 * 4];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                bool on = ((x / 2) + (y / 2)) % 2 == 0;
                uint8_t* p = &px[(y * 8 + x) * 4];
                p[0] = on ? 255 : 40;
                p[1] = on ? 0 : 40;
                p[2] = on ? 220 : 40;
                p[3] = 255;
            }
        missing_ = upload(px, 8, 8, true, false, "missing texture");
        missing_.missing = true;
    }
    return missing_;
}

const TextureAsset& Assets::texture(const std::string& path, bool pixelArt) {
    std::string key = path + (pixelArt ? "#p" : "#s");
    auto it = textures_.find(key);
    if (it != textures_.end())
        return it->second.asset;
    CachedTexture c;
    c.file = resolve(path);
    c.pixelArt = pixelArt;
    c.modified = fs::modifiedTime(c.file);
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!loadImage(c.file, pixels, w, h)) {
        warnOnce(key, "Couldn't load the image '" + path + "'. Check that the file exists in your project folder.");
        c.asset = missingTexture();
        c.asset.missing = true;
    } else {
        c.asset = upload(pixels.data(), w, h, pixelArt, true, path.c_str());
    }
    return textures_.emplace(key, std::move(c)).first->second.asset;
}

bool Assets::reloadChanged() {
    bool changed = false;
    for (auto& [key, c] : textures_) {
        int64_t m = fs::modifiedTime(c.file);
        if (m == 0 || m == c.modified)
            continue;
        c.modified = m;
        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
        if (!loadImage(c.file, pixels, w, h))
            continue;
        if (!c.asset.missing && device_)
            device_->destroy(c.asset.handle);
        c.asset = upload(pixels.data(), w, h, c.pixelArt, true, key.c_str());
        Log::info("Reloaded image ", c.file.filename().string());
        changed = true;
    }
    return changed;
}

const TextureAsset& Assets::white() {
    if (!white_.handle && device_) {
        uint8_t px[4] = {255, 255, 255, 255};
        white_ = upload(px, 1, 1, true, false, "white");
    }
    return white_;
}

const TextureAsset& Assets::glow() {
    if (!glow_.handle && device_) {
        const int size = 64;
        std::vector<uint8_t> px(static_cast<size_t>(size * size * 4));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                float dx = (x + 0.5f) / size * 2 - 1, dy = (y + 0.5f) / size * 2 - 1;
                float d = std::sqrt(dx * dx + dy * dy);
                float a = saturate(1.0f - d);
                a = a * a * (3 - 2 * a);
                uint8_t* p = &px[static_cast<size_t>((y * size + x) * 4)];
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<uint8_t>(a * 255.0f);
            }
        glow_ = upload(px.data(), size, size, false, true, "glow");
    }
    return glow_;
}

const TextureAsset& Assets::shape(Shape2D s) {
    if (s == Shape2D::Square)
        return white();
    auto it = shapes_.find(static_cast<int>(s));
    if (it != shapes_.end())
        return it->second;
    const int size = 256;
    std::vector<uint8_t> px(static_cast<size_t>(size * size * 4));
    float pixel = 2.0f / size;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // Texture rows are stored bottom-up, matching how images are loaded.
            Vec2 p{(x + 0.5f) / size * 2.0f - 1.0f, (y + 0.5f) / size * 2.0f - 1.0f};
            float d = shapeDistance(s, p);
            float a = saturate(0.5f - d / (pixel * 1.5f));
            uint8_t* out = &px[static_cast<size_t>((y * size + x) * 4)];
            out[0] = out[1] = out[2] = 255;
            out[3] = static_cast<uint8_t>(a * 255.0f + 0.5f);
        }
    TextureAsset t = upload(px.data(), size, size, false, true, "shape");
    return shapes_.emplace(static_cast<int>(s), t).first->second;
}

static std::unique_ptr<Font> loadEmbeddedFont(rhi::Device* device, const char* name) {
    auto font = std::make_unique<Font>();
    size_t size = 0;
    const unsigned char* data = embedded::find(name, &size);
    if (!data || !font->load(device, data, size))
        Log::error("Built-in font ", name, " is missing.");
    return font;
}

Font& Assets::defaultFont() {
    if (!defaultFont_)
        defaultFont_ = loadEmbeddedFont(device_, "Roboto-Medium.ttf");
    return *defaultFont_;
}

Font& Assets::monoFont() {
    if (!monoFont_)
        monoFont_ = loadEmbeddedFont(device_, "Cousine-Regular.ttf");
    return *monoFont_;
}

Font& Assets::font(const std::string& path) {
    if (path.empty())
        return defaultFont();
    auto it = fonts_.find(path);
    if (it != fonts_.end())
        return it->second ? *it->second : defaultFont();
    auto bytes = fs::readBinary(resolve(path));
    std::unique_ptr<Font> f;
    if (bytes) {
        f = std::make_unique<Font>();
        if (!f->load(device_, bytes->data(), bytes->size()))
            f.reset();
    }
    if (!f)
        warnOnce(path, "Couldn't load the font '" + path + "'. Using the default font instead.");
    Font* result = f.get();
    fonts_[path] = std::move(f);
    return result ? *result : defaultFont();
}

} // namespace aven
