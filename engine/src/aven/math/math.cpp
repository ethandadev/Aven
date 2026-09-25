#include "aven/math/math.h"

namespace aven {

Vec3 Quat::toEuler() const {
    Mat4 r = Mat4::rotation(normalize(*this));
    float r12 = clamp(r.at(1, 2), -1.0f, 1.0f);
    float ax = std::asin(-r12);
    float ay, az;
    if (std::abs(r12) < 0.99999f) {
        ay = std::atan2(r.at(0, 2), r.at(2, 2));
        az = std::atan2(r.at(1, 0), r.at(1, 1));
    } else {
        ay = std::atan2(-r.at(2, 0), r.at(0, 0));
        az = 0;
    }
    return {degrees(ax), degrees(ay), degrees(az)};
}

Mat4 transpose(const Mat4& m) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            r.m[c * 4 + row] = m.m[row * 4 + c];
    return r;
}

Mat4 inverse(const Mat4& mat) {
    const float* m = mat.m;
    Mat4 out;
    float* inv = out.m;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(det) < 1e-12f)
        return Mat4{};
    float invDet = 1.0f / det;
    for (float& v : out.m)
        v *= invDet;
    return out;
}

Quat quatFromMatrix(const Mat4& m) {
    float trace = m.at(0, 0) + m.at(1, 1) + m.at(2, 2);
    Quat q;
    if (trace > 0) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m.at(2, 1) - m.at(1, 2)) * s;
        q.y = (m.at(0, 2) - m.at(2, 0)) * s;
        q.z = (m.at(1, 0) - m.at(0, 1)) * s;
    } else if (m.at(0, 0) > m.at(1, 1) && m.at(0, 0) > m.at(2, 2)) {
        float s = 2.0f * std::sqrt(1.0f + m.at(0, 0) - m.at(1, 1) - m.at(2, 2));
        q.w = (m.at(2, 1) - m.at(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (m.at(0, 1) + m.at(1, 0)) / s;
        q.z = (m.at(0, 2) + m.at(2, 0)) / s;
    } else if (m.at(1, 1) > m.at(2, 2)) {
        float s = 2.0f * std::sqrt(1.0f + m.at(1, 1) - m.at(0, 0) - m.at(2, 2));
        q.w = (m.at(0, 2) - m.at(2, 0)) / s;
        q.x = (m.at(0, 1) + m.at(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (m.at(1, 2) + m.at(2, 1)) / s;
    } else {
        float s = 2.0f * std::sqrt(1.0f + m.at(2, 2) - m.at(0, 0) - m.at(1, 1));
        q.w = (m.at(1, 0) - m.at(0, 1)) / s;
        q.x = (m.at(0, 2) + m.at(2, 0)) / s;
        q.y = (m.at(1, 2) + m.at(2, 1)) / s;
        q.z = 0.25f * s;
    }
    return normalize(q);
}

bool decompose(const Mat4& m, Vec3& translation, Quat& rotation, Vec3& scale) {
    translation = {m.m[12], m.m[13], m.m[14]};
    Vec3 c0 = m.column(0).xyz(), c1 = m.column(1).xyz(), c2 = m.column(2).xyz();
    scale = {length(c0), length(c1), length(c2)};
    if (scale.x < 1e-8f || scale.y < 1e-8f || scale.z < 1e-8f)
        return false;
    if (dot(cross(c0, c1), c2) < 0)
        scale.x = -scale.x;
    Mat4 r;
    Vec3 n0 = c0 / scale.x, n1 = c1 / scale.y, n2 = c2 / scale.z;
    for (int i = 0; i < 3; ++i) {
        r.m[i] = n0[i];
        r.m[4 + i] = n1[i];
        r.m[8 + i] = n2[i];
    }
    rotation = quatFromMatrix(r);
    return true;
}

} // namespace aven
