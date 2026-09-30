#define CGLTF_IMPLEMENTATION
#include "rynax/render/model.h"

#include "rynax/core/fs.h"
#include "rynax/core/log.h"

#include <cgltf.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace rynax {

Model::~Model() {
    release();
}

void Model::release() {
    if (!device_)
        return;
    for (auto& p : parts)
        p.gpu.release(*device_);
    for (auto t : textures_)
        device_->destroy(t);
    textures_.clear();
    parts.clear();
    device_ = nullptr;
}

const AnimationClip* Model::findClip(const std::string& name) const {
    if (animations.empty())
        return nullptr;
    if (name.empty())
        return &animations.front();
    for (auto& a : animations)
        if (a.name == name)
            return &a;
    return nullptr;
}

namespace {

// cgltf opens files through these: Rynax's file reading, which takes any folder name (cgltf's own
// fopen can't on Windows). Paths come in as UTF-8.
cgltf_result readFile(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
    auto bytes = fs::readBinary(fs::fromUtf8(path));
    if (!bytes)
        return cgltf_result_file_not_found;
    void* copy = std::malloc(std::max<size_t>(bytes->size(), 1));
    if (!copy)
        return cgltf_result_out_of_memory;
    if (!bytes->empty())
        std::memcpy(copy, bytes->data(), bytes->size());
    *size = bytes->size();
    *data = copy;
    return cgltf_result_success;
}

void releaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

rhi::TextureHandle loadImage(rhi::Device* device, const cgltf_image* image, const std::filesystem::path& base) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = nullptr;
    stbi_set_flip_vertically_on_load_thread(0); // glTF uses top-left texture coordinates
    if (image->buffer_view) {
        const uint8_t* data = static_cast<const uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        pixels = stbi_load_from_memory(data, static_cast<int>(image->buffer_view->size), &w, &h, &channels, 4);
    } else if (image->uri && std::string(image->uri).rfind("data:", 0) != 0) {
        std::string uri = image->uri;
        cgltf_decode_uri(uri.data());
        uri.resize(std::strlen(uri.c_str()));
        if (auto bytes = fs::readBinary(base / fs::fromUtf8(uri)); bytes && !bytes->empty())
            pixels = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &channels, 4);
    }
    if (!pixels)
        return {};
    if (!device) {
        stbi_image_free(pixels);
        return {};
    }
    rhi::TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = rhi::PixelFormat::RGBA8;
    d.wrap = rhi::Wrap::Repeat;
    d.mipmaps = true;
    d.data = pixels;
    d.label = "model texture";
    rhi::TextureHandle t = device->createTexture(d);
    stbi_image_free(pixels);
    return t;
}

template <class T> void readAccessor(const cgltf_accessor* acc, std::vector<T>& out, int components) {
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
        cgltf_accessor_read_float(acc, i, reinterpret_cast<float*>(&out[i]), static_cast<cgltf_size>(components));
}

} // namespace

bool Model::load(rhi::Device* device, const std::filesystem::path& path, std::string* error) {
    release();
    device_ = device;
    cgltf_options options{};
    options.file.read = readFile;
    options.file.release = releaseFile;
    cgltf_data* data = nullptr;
    std::string p = fs::toUtf8(path);
    if (cgltf_parse_file(&options, p.c_str(), &data) != cgltf_result_success) {
        if (error)
            *error = "not a valid glTF file";
        return false;
    }
    std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
    if (cgltf_load_buffers(&options, data, p.c_str()) != cgltf_result_success) {
        if (error)
            *error = "the model's data (.bin) file is missing";
        return false;
    }
    // Every accessor inside its buffer, indices in range...: a damaged file is refused here
    // instead of reading past the end of its data below.
    if (cgltf_validate(data) != cgltf_result_success) {
        if (error)
            *error = "the model file is damaged (its data doesn't add up)";
        return false;
    }
    std::filesystem::path base = path.parent_path();

    std::vector<rhi::TextureHandle> imageTextures(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        imageTextures[i] = loadImage(device, &data->images[i], base);
        if (imageTextures[i])
            textures_.push_back(imageTextures[i]);
    }

    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        ModelMaterial mat;
        if (m.has_pbr_metallic_roughness) {
            auto& pbr = m.pbr_metallic_roughness;
            mat.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                             pbr.base_color_factor[3]};
            mat.metallic = pbr.metallic_factor;
            mat.roughness = pbr.roughness_factor;
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                mat.baseTexture = imageTextures[static_cast<size_t>(pbr.base_color_texture.texture->image - data->images)];
        }
        mat.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
        mat.doubleSided = m.double_sided;
        mat.transparent = m.alpha_mode == cgltf_alpha_mode_blend;
        materials.push_back(mat);
    }

    // Meshes: each primitive becomes a part; remember which parts belong to each mesh.
    std::vector<std::vector<int>> meshParts(data->meshes_count);
    bool anyBounds = false;
    bool tooManyJoints = false;
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
                continue;
            ModelPart part;
            std::vector<Vec3> positions, normals;
            std::vector<Vec2> uvs;
            std::vector<Vec4> joints, weights;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position)
                    readAccessor(attr.data, positions, 3);
                else if (attr.type == cgltf_attribute_type_normal)
                    readAccessor(attr.data, normals, 3);
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                    readAccessor(attr.data, uvs, 2);
                else if (attr.type == cgltf_attribute_type_joints && attr.index == 0)
                    readAccessor(attr.data, joints, 4);
                else if (attr.type == cgltf_attribute_type_weights && attr.index == 0)
                    readAccessor(attr.data, weights, 4);
            }
            if (positions.empty())
                continue;
            part.data.vertices.resize(positions.size());
            for (size_t v = 0; v < positions.size(); ++v) {
                MeshVertex& mv = part.data.vertices[v];
                mv.position = positions[v];
                mv.normal = v < normals.size() ? normals[v] : Vec3(0, 1, 0);
                mv.uv = v < uvs.size() ? uvs[v] : Vec2();
                if (v < joints.size() && v < weights.size()) {
                    // The shader holds 128 joints: a bone past that (rare: very detailed rigs) can't
                    // move the vertex, so its weight goes to the others.
                    float total = 0;
                    for (int k = 0; k < 4; ++k) {
                        bool usable = joints[v][k] >= 0 && joints[v][k] < 128 && std::isfinite(weights[v][k]) && weights[v][k] > 0;
                        mv.joints[k] = usable ? joints[v][k] : 0;
                        mv.weights[k] = usable ? weights[v][k] : 0;
                        total += mv.weights[k];
                        tooManyJoints = tooManyJoints || joints[v][k] >= 128;
                    }
                    if (total > 0)
                        for (int k = 0; k < 4; ++k)
                            mv.weights[k] /= total;
                    else
                        mv.weights[0] = 1; // (all on the first joint rather than collapsing to the origin)
                    part.skinned = true;
                }
            }
            if (prim.indices) {
                // Whole triangles only, and only ones whose corners exist.
                cgltf_size count = prim.indices->count - prim.indices->count % 3;
                part.data.indices.reserve(count);
                for (cgltf_size k = 0; k < count; k += 3) {
                    uint32_t tri[3];
                    bool ok = true;
                    for (int c = 0; c < 3; ++c) {
                        tri[c] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k + static_cast<cgltf_size>(c)));
                        ok = ok && tri[c] < positions.size();
                    }
                    if (ok)
                        part.data.indices.insert(part.data.indices.end(), tri, tri + 3);
                }
            } else {
                for (uint32_t k = 0; k < positions.size(); ++k)
                    part.data.indices.push_back(k);
            }
            if (normals.empty()) {
                // Flat normals for models exported without them.
                for (size_t k = 0; k + 2 < part.data.indices.size(); k += 3) {
                    auto& a = part.data.vertices[part.data.indices[k]];
                    auto& b = part.data.vertices[part.data.indices[k + 1]];
                    auto& c = part.data.vertices[part.data.indices[k + 2]];
                    Vec3 n = normalize(cross(b.position - a.position, c.position - a.position));
                    a.normal = b.normal = c.normal = n;
                }
            }
            part.data.computeBounds();
            part.material = prim.material ? static_cast<int>(prim.material - data->materials) : -1;
            meshParts[mi].push_back(static_cast<int>(parts.size()));
            parts.push_back(std::move(part));
        }
    }
    if (tooManyJoints)
        Log::warn(fs::toUtf8(path.filename()), ": some parts are moved by more than 128 bones; those bones are left out, "
                  "so they may not bend. (Fewer bones, or a simpler rig, fixes it.)");
    for (auto& part : parts)
        if (device)
            part.gpu.upload(*device, part.data);
    // Fix the CPU pointer after the vector stopped moving.
    for (auto& part : parts)
        part.gpu.cpu = &part.data;

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& n = data->nodes[ni];
        ModelNode node;
        node.name = n.name ? n.name : "";
        node.parent = n.parent ? static_cast<int>(n.parent - data->nodes) : -1;
        if (n.has_matrix) {
            Mat4 m;
            for (int k = 0; k < 16; ++k)
                m.m[k] = n.matrix[k];
            decompose(m, node.translation, node.rotation, node.scale);
        } else {
            if (n.has_translation)
                node.translation = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation)
                node.rotation = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
            if (n.has_scale)
                node.scale = {n.scale[0], n.scale[1], n.scale[2]};
        }
        if (n.mesh)
            node.parts = meshParts[static_cast<size_t>(n.mesh - data->meshes)];
        node.skin = n.skin ? static_cast<int>(n.skin - data->skins) : -1;
        nodes.push_back(node);
    }

    for (cgltf_size si = 0; si < data->skins_count; ++si) {
        const cgltf_skin& s = data->skins[si];
        ModelSkin skin;
        for (cgltf_size j = 0; j < s.joints_count; ++j) {
            skin.joints.push_back(static_cast<int>(s.joints[j] - data->nodes));
            Mat4 ib;
            if (s.inverse_bind_matrices)
                cgltf_accessor_read_float(s.inverse_bind_matrices, j, ib.m, 16);
            skin.inverseBind.push_back(ib);
        }
        skins.push_back(std::move(skin));
    }

    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& a = data->animations[ai];
        AnimationClip clip;
        clip.name = a.name ? a.name : ("Animation " + std::to_string(ai + 1));
        for (cgltf_size ci = 0; ci < a.channels_count; ++ci) {
            const cgltf_animation_channel& ch = a.channels[ci];
            if (!ch.target_node || !ch.sampler)
                continue;
            AnimationChannel c;
            c.node = static_cast<int>(ch.target_node - data->nodes);
            if (ch.target_path == cgltf_animation_path_type_translation)
                c.path = 0;
            else if (ch.target_path == cgltf_animation_path_type_rotation)
                c.path = 1;
            else if (ch.target_path == cgltf_animation_path_type_scale)
                c.path = 2;
            else
                continue;
            c.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
            bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            const cgltf_accessor* in = ch.sampler->input;
            const cgltf_accessor* out = ch.sampler->output;
            c.times.resize(in->count);
            for (cgltf_size k = 0; k < in->count; ++k)
                cgltf_accessor_read_float(in, k, &c.times[k], 1);
            int comps = c.path == 1 ? 4 : 3;
            for (cgltf_size k = 0; k < out->count; ++k) {
                // Cubic splines store in-tangent, value, out-tangent; keep only the value.
                if (cubic && k % 3 != 1)
                    continue;
                Vec4 v;
                cgltf_accessor_read_float(out, k, &v.x, static_cast<cgltf_size>(comps));
                c.values.push_back(v);
            }
            if (!c.times.empty())
                clip.duration = std::max(clip.duration, c.times.back());
            clip.channels.push_back(std::move(c));
        }
        animations.push_back(std::move(clip));
    }

    // Model bounds in rest pose.
    std::vector<Mat4> pose;
    computePose(nullptr, 0, pose);
    for (size_t ni = 0; ni < nodes.size(); ++ni)
        for (int pi : nodes[ni].parts) {
            const MeshData& d = parts[static_cast<size_t>(pi)].data;
            for (int c = 0; c < 8; ++c) {
                Vec3 corner{(c & 1) ? d.boundsMax.x : d.boundsMin.x, (c & 2) ? d.boundsMax.y : d.boundsMin.y,
                            (c & 4) ? d.boundsMax.z : d.boundsMin.z};
                Vec3 w = transformPoint(pose[ni], corner);
                boundsMin = anyBounds ? min(boundsMin, w) : w;
                boundsMax = anyBounds ? max(boundsMax, w) : w;
                anyBounds = true;
            }
        }
    return true;
}

void Model::computePose(const AnimationClip* clip, float time, std::vector<Mat4>& out) const {
    std::vector<Vec3> t(nodes.size()), s(nodes.size());
    std::vector<Quat> r(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        t[i] = nodes[i].translation;
        r[i] = nodes[i].rotation;
        s[i] = nodes[i].scale;
    }
    if (clip) {
        for (auto& ch : clip->channels) {
            if (ch.times.empty() || ch.values.empty() || ch.node < 0 || static_cast<size_t>(ch.node) >= nodes.size())
                continue;
            size_t k = 0;
            while (k + 1 < ch.times.size() && ch.times[k + 1] <= time)
                ++k;
            size_t k1 = std::min(k + 1, ch.values.size() - 1);
            k = std::min(k, ch.values.size() - 1);
            float span = k1 < ch.times.size() && k < ch.times.size() ? ch.times[k1] - ch.times[k] : 0;
            float f = span > 1e-6f ? clamp((time - ch.times[k]) / span, 0.0f, 1.0f) : 0.0f;
            if (ch.step)
                f = 0;
            const Vec4& a = ch.values[k];
            const Vec4& b = ch.values[k1];
            size_t n = static_cast<size_t>(ch.node);
            if (ch.path == 0)
                t[n] = lerp(a.xyz(), b.xyz(), f);
            else if (ch.path == 2)
                s[n] = lerp(a.xyz(), b.xyz(), f);
            else
                r[n] = slerp(Quat{a.x, a.y, a.z, a.w}, Quat{b.x, b.y, b.z, b.w}, f);
        }
    }
    out.assign(nodes.size(), Mat4{});
    // 0 = not yet, 1 = working on it (a parent chain that loops back is treated as the top), 2 = done
    std::vector<uint8_t> state(nodes.size(), 0);
    Mat4 rootScale = importScale != 1.0f ? Mat4::trs({}, Quat{}, {importScale, importScale, importScale}) : Mat4{};
    std::function<const Mat4&(size_t)> global = [&](size_t i) -> const Mat4& {
        if (state[i] == 0) {
            state[i] = 1;
            Mat4 local = Mat4::trs(t[i], r[i], s[i]);
            int parent = nodes[i].parent;
            bool hasParent = parent >= 0 && static_cast<size_t>(parent) < nodes.size() && state[static_cast<size_t>(parent)] != 1;
            out[i] = hasParent ? global(static_cast<size_t>(parent)) * local : rootScale * local;
            state[i] = 2;
        }
        return out[i];
    };
    for (size_t i = 0; i < nodes.size(); ++i)
        global(i);
}

} // namespace rynax
