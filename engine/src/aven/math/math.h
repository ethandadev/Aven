#pragma once

// Aven math: right-handed, Y-up, cameras look down -Z (OpenGL conventions).
// Matrices are column-major (m[col * 4 + row]) to match GLSL.
// Euler angles are in degrees and applied Z, then X, then Y (same as Unity).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aven {

constexpr float kPi = 3.14159265358979323846f;

inline float radians(float degrees) { return degrees * (kPi / 180.0f); }
inline float degrees(float radians) { return radians * (180.0f / kPi); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }
inline float saturate(float v) { return clamp(v, 0.0f, 1.0f); }

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr explicit Vec2(float s) : x(s), y(s) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
    constexpr Vec3(Vec2 v, float z_) : x(v.x), y(v.y), z(z_) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
    Vec2 xy() const { return {x, y}; }
};

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
    Vec3 xyz() const { return {x, y, z}; }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    static Color fromHex(uint32_t rgb, float alpha = 1.0f) {
        return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
    }
    Vec4 toVec4() const { return {r, g, b, a}; }
    Vec3 rgb() const { return {r, g, b}; }
    uint32_t toRGBA8() const {
        auto c = [](float v) { return static_cast<uint32_t>(saturate(v) * 255.0f + 0.5f); };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }
};

// --- Vec2 ---
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, Vec2 b) { return {a.x * b.x, a.y * b.y}; }
inline Vec2 operator/(Vec2 a, Vec2 b) { return {a.x / b.x, a.y / b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
inline Vec2 operator-(Vec2 a) { return {-a.x, -a.y}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { return a = a + b; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { return a = a - b; }
inline Vec2& operator*=(Vec2& a, float s) { return a = a * s; }
inline bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline Vec2 normalize(Vec2 a) {
    float l = length(a);
    return l > 1e-8f ? a / l : Vec2{};
}
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

// --- Vec3 ---
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 operator/(Vec3 a, Vec3 b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { return a = a + b; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { return a = a - b; }
inline Vec3& operator*=(Vec3& a, float s) { return a = a * s; }
inline bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline float lengthSquared(Vec3 a) { return dot(a, a); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 1e-8f ? a / l : Vec3{};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 min(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 max(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

// --- Vec4 ---
inline Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline Vec4 operator*(Vec4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline Color lerp(Color a, Color b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

// --- Quaternion ---
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat axisAngle(Vec3 axis, float angleRadians) {
        Vec3 a = normalize(axis);
        float s = std::sin(angleRadians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angleRadians * 0.5f)};
    }
    // Euler angles in degrees, applied Z then X then Y.
    static Quat fromEuler(Vec3 eulerDegrees);
    Vec3 toEuler() const;
};

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat normalize(Quat q) {
    float l = std::sqrt(dot(q, q));
    return l > 1e-8f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{};
}
inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = 2.0f * cross(u, v);
    return v + q.w * t + cross(u, t);
}
inline Quat slerp(Quat a, Quat b, float t) {
    float d = dot(a, b);
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize(Quat{lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t)});
    }
    float theta = std::acos(d);
    float s = std::sin(theta);
    float wa = std::sin((1 - t) * theta) / s, wb = std::sin(t * theta) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

inline Quat Quat::fromEuler(Vec3 e) {
    Quat qx = axisAngle({1, 0, 0}, radians(e.x));
    Quat qy = axisAngle({0, 1, 0}, radians(e.y));
    Quat qz = axisAngle({0, 0, 1}, radians(e.z));
    return qy * qx * qz;
}

// --- Mat4 ---
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    Vec4 column(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]}; }
    const float* data() const { return m; }

    static Mat4 translation(Vec3 t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    static Mat4 scaling(Vec3 s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }
    static Mat4 rotation(Quat q) {
        Mat4 r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.at(0, 0) = 1 - 2 * (yy + zz);
        r.at(0, 1) = 2 * (xy - wz);
        r.at(0, 2) = 2 * (xz + wy);
        r.at(1, 0) = 2 * (xy + wz);
        r.at(1, 1) = 1 - 2 * (xx + zz);
        r.at(1, 2) = 2 * (yz - wx);
        r.at(2, 0) = 2 * (xz - wy);
        r.at(2, 1) = 2 * (yz + wx);
        r.at(2, 2) = 1 - 2 * (xx + yy);
        return r;
    }
    static Mat4 trs(Vec3 t, Quat r, Vec3 s);
    static Mat4 perspective(float fovyRadians, float aspect, float zNear, float zFar) {
        Mat4 r;
        float f = 1.0f / std::tan(fovyRadians * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1;
        r.m[14] = 2 * zFar * zNear / (zNear - zFar);
        r.m[15] = 0;
        return r;
    }
    static Mat4 orthographic(float l, float r_, float b, float t, float n, float f) {
        Mat4 r;
        r.m[0] = 2 / (r_ - l);
        r.m[5] = 2 / (t - b);
        r.m[10] = -2 / (f - n);
        r.m[12] = -(r_ + l) / (r_ - l);
        r.m[13] = -(t + b) / (t - b);
        r.m[14] = -(f + n) / (f - n);
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x;
        r.m[4] = s.y;
        r.m[8] = s.z;
        r.m[1] = u.x;
        r.m[5] = u.y;
        r.m[9] = u.z;
        r.m[2] = -f.x;
        r.m[6] = -f.y;
        r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

inline Vec4 operator*(const Mat4& a, Vec4 v) {
    Vec4 r;
    for (int row = 0; row < 4; ++row)
        r[row] = a.m[row] * v.x + a.m[4 + row] * v.y + a.m[8 + row] * v.z + a.m[12 + row] * v.w;
    return r;
}

inline Vec3 transformPoint(const Mat4& m, Vec3 p) {
    Vec4 r = m * Vec4(p, 1.0f);
    return r.w != 0 && r.w != 1 ? r.xyz() / r.w : r.xyz();
}
inline Vec3 transformDirection(const Mat4& m, Vec3 d) { return (m * Vec4(d, 0.0f)).xyz(); }

inline Mat4 Mat4::trs(Vec3 t, Quat r, Vec3 s) {
    Mat4 m = rotation(r);
    for (int i = 0; i < 3; ++i) {
        m.m[i] *= s.x;
        m.m[4 + i] *= s.y;
        m.m[8 + i] *= s.z;
    }
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

Mat4 transpose(const Mat4& m);
Mat4 inverse(const Mat4& m);
Quat quatFromMatrix(const Mat4& m);
// Splits an affine TRS matrix back into its parts. Returns false for degenerate input.
bool decompose(const Mat4& m, Vec3& translation, Quat& rotation, Vec3& scale);

} // namespace aven
