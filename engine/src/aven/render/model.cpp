#define CGLTF_IMPLEMENTATION
#include "aven/render/model.h"

#include "aven/core/log.h"

#include <cgltf.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace aven {

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
        pixels = stbi_load((base / uri).string().c_str(), &w, &h, &channels, 4);
    }
    if (!pixels || !device)
        return {};
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
    cgltf_data* data = nullptr;
    std::string p = path.string();
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
                    for (int k = 0; k < 4; ++k) {
                        mv.joints[k] = joints[v][k];
                        mv.weights[k] = weights[v][k];
                    }
                    part.skinned = true;
                }
            }
            if (prim.indices) {
                part.data.indices.resize(prim.indices->count);
                for (cgltf_size k = 0; k < prim.indices->count; ++k)
                    part.data.indices[k] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
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
    std::vector<bool> done(nodes.size(), false);
    std::function<const Mat4&(size_t)> global = [&](size_t i) -> const Mat4& {
        if (!done[i]) {
            Mat4 local = Mat4::trs(t[i], r[i], s[i]);
            out[i] = nodes[i].parent >= 0 ? global(static_cast<size_t>(nodes[i].parent)) * local : local;
            done[i] = true;
        }
        return out[i];
    };
    for (size_t i = 0; i < nodes.size(); ++i)
        global(i);
}

} // namespace aven
