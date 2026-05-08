#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vws {

inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float DegToRad = Pi / 180.0f;
inline constexpr float WorldHalfSize = 150.0f;
inline constexpr float PlayerEyeHeight = 1.7f;
inline constexpr float CrouchEyeHeight = 1.05f;
inline constexpr float MaxClimbHeight = 34.0f;
inline constexpr float MirrorZ = 52.0f;
inline constexpr float MirrorFaceZ = MirrorZ + 0.56f;
inline constexpr int MaxFramesInFlight = 2;

inline float clamp(float value, float low, float high)
{
    return std::max(low, std::min(value, high));
}

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3 operator+(const Vec3 &other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3 &other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    Vec3 &operator+=(const Vec3 &other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    Vec3 &operator-=(const Vec3 &other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }
    Vec3 normalized() const
    {
        const float len = length();
        if (len < 0.00001f) {
            return {};
        }
        return *this / len;
    }
};

inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline float dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity()
    {
        Mat4 out;
        out.m[0] = 1.0f;
        out.m[5] = 1.0f;
        out.m[10] = 1.0f;
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 perspective(float fovDegrees, float aspect, float nearPlane, float farPlane)
    {
        const float f = 1.0f / std::tan(fovDegrees * DegToRad * 0.5f);
        Mat4 out;
        out.m[0] = f / aspect;
        out.m[5] = -f;
        out.m[10] = farPlane / (nearPlane - farPlane);
        out.m[11] = -1.0f;
        out.m[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
        return out;
    }

    static Mat4 lookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
    {
        const Vec3 f = (center - eye).normalized();
        const Vec3 s = cross(f, up).normalized();
        const Vec3 u = cross(s, f);
        Mat4 out = identity();
        out.m[0] = s.x;
        out.m[4] = s.y;
        out.m[8] = s.z;
        out.m[1] = u.x;
        out.m[5] = u.y;
        out.m[9] = u.z;
        out.m[2] = -f.x;
        out.m[6] = -f.y;
        out.m[10] = -f.z;
        out.m[12] = -dot(s, eye);
        out.m[13] = -dot(u, eye);
        out.m[14] = dot(f, eye);
        return out;
    }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b)
{
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

} // namespace vws
