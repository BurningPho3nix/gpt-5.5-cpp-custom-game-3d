#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float DegToRad = Pi / 180.0f;
constexpr float WorldHalfSize = 150.0f;
constexpr float PlayerEyeHeight = 1.7f;
constexpr float CrouchEyeHeight = 1.05f;
constexpr float MaxClimbHeight = 34.0f;
constexpr float MirrorZ = 52.0f;
constexpr int MaxFramesInFlight = 2;

float clamp(float value, float low, float high)
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

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float dot(const Vec3 &a, const Vec3 &b)
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

Mat4 operator*(const Mat4 &a, const Mat4 &b)
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

struct Vertex {
    Vec3 position;
    Vec3 color;
};

struct WorldBox {
    Vec3 center;
    Vec3 size;
    Vec3 color;
    bool shootable = true;
};

struct BulletMark {
    Vec3 position;
    Vec3 normal;
    float age = 0.0f;
};

struct FlyingBullet {
    Vec3 start;
    Vec3 target;
    Vec3 normal;
    float distance = 0.0f;
    float traveled = 0.0f;
    bool willHit = false;
};

struct SurfaceHit {
    Vec3 position;
    Vec3 normal;
    float distance = 0.0f;
    bool hit = false;
};

float terrainNoise(float x, float z)
{
    const float rolling = std::sin(x * 0.045f) * 1.6f + std::cos(z * 0.038f) * 1.4f;
    const float ridges = std::sin((x + z) * 0.022f) * 1.2f + std::cos((x - z) * 0.031f) * 0.9f;
    const float detail = std::sin(x * 0.13f + z * 0.07f) * 0.32f;
    const float spawnFlatten = std::exp(-(x * x + (z - 70.0f) * (z - 70.0f)) / 2200.0f);
    return (rolling + ridges + detail + 2.4f) * (1.0f - spawnFlatten * 0.75f);
}

float deterministic01(int value)
{
    uint32_t x = static_cast<uint32_t>(value) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return float(x & 1023u) / 1023.0f;
}

class Game {
public:
    Game() { buildWorld(); reset(); }

    void handleEvent(const SDL_Event &event)
    {
        if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured) {
            const float sensitivity = 0.12f * (1.0f - scopeAmount * 0.55f);
            yaw += event.motion.xrel * sensitivity;
            pitch = clamp(pitch - event.motion.yrel * sensitivity, -82.0f, 82.0f);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseCaptured = true;
                shoot();
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                mouseCaptured = true;
                scoped = true;
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                scoped = false;
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.scancode == SDL_SCANCODE_SPACE) {
                jumpQueued = true;
            } else if (event.key.scancode == SDL_SCANCODE_R) {
                reset();
            } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                mouseCaptured = false;
                scoped = false;
            }
        }
    }

    void update(float deltaSeconds)
    {
        const bool *keys = SDL_GetKeyboardState(nullptr);
        const bool crouching = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] || keys[SDL_SCANCODE_C];
        crouchAmount += ((crouching ? 1.0f : 0.0f) - crouchAmount) * std::min(1.0f, deltaSeconds * 12.0f);
        scopeAmount += ((scoped ? 1.0f : 0.0f) - scopeAmount) * std::min(1.0f, deltaSeconds * 14.0f);

        Vec3 movement;
        if (keys[SDL_SCANCODE_W]) {
            movement += forwardVector();
        }
        if (keys[SDL_SCANCODE_S]) {
            movement -= forwardVector();
        }
        if (keys[SDL_SCANCODE_D]) {
            movement += rightVector();
        }
        if (keys[SDL_SCANCODE_A]) {
            movement -= rightVector();
        }

        const bool sprinting = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) && !crouching && !scoped;
        walking = movement.lengthSquared() > 0.0001f;
        walkCycle += deltaSeconds * (walking ? (sprinting ? 10.0f : 7.0f) : 2.0f);

        movement.y = 0.0f;
        if (movement.lengthSquared() > 0.0001f) {
            movement = movement.normalized();
            const float speed = (sprinting ? 14.0f : 8.5f) * (1.0f - crouchAmount * 0.42f) * (1.0f - scopeAmount * 0.28f);
            position += movement * speed * deltaSeconds;
        }

        position.x = clamp(position.x, -WorldHalfSize + 2.0f, WorldHalfSize - 2.0f);
        position.z = clamp(position.z, -WorldHalfSize + 2.0f, WorldHalfSize - 2.0f);
        const float floor = groundHeightAt(position.x, position.z) + currentEyeHeight();

        if (jumpQueued && grounded && crouchAmount < 0.65f) {
            verticalVelocity = 8.8f;
            grounded = false;
        }
        jumpQueued = false;

        if (grounded) {
            const float heightError = floor - position.y;
            verticalVelocity += heightError * 38.0f * deltaSeconds;
            verticalVelocity *= std::pow(0.015f, deltaSeconds);
        } else {
            verticalVelocity -= 24.0f * deltaSeconds;
        }

        verticalVelocity = clamp(verticalVelocity, -32.0f, 14.0f);
        position.y += verticalVelocity * deltaSeconds;
        if (position.y < floor) {
            position.y = floor;
            verticalVelocity = 0.0f;
            grounded = true;
        } else if (position.y > floor + 0.08f) {
            grounded = false;
        }
        if (position.y > MaxClimbHeight) {
            position.y = MaxClimbHeight;
            verticalVelocity = std::min(0.0f, verticalVelocity);
        }

        shotFlash = std::max(0.0f, shotFlash - deltaSeconds * 5.0f);
        for (BulletMark &mark : bulletMarks) {
            mark.age += deltaSeconds;
        }

        constexpr float bulletSpeed = 95.0f;
        for (auto bullet = flyingBullets.begin(); bullet != flyingBullets.end();) {
            bullet->traveled += bulletSpeed * deltaSeconds;
            if (bullet->traveled >= bullet->distance) {
                if (bullet->willHit) {
                    bulletMarks.push_back({bullet->target, bullet->normal, 0.0f});
                    if (bulletMarks.size() > 120) {
                        bulletMarks.erase(bulletMarks.begin());
                    }
                }
                bullet = flyingBullets.erase(bullet);
            } else {
                ++bullet;
            }
        }

        fpsTimer += deltaSeconds;
        ++fpsFrames;
        if (fpsTimer >= 0.35f) {
            currentFps = float(fpsFrames) / fpsTimer;
            fpsFrames = 0;
            fpsTimer = 0.0f;
        }
    }

    void setRelativeMouseMode(SDL_Window *window) const
    {
        SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
    }

    Mat4 viewProjection(float aspect) const
    {
        const float fov = 70.0f + (28.0f - 70.0f) * scopeAmount;
        return Mat4::perspective(fov, aspect, 0.05f, 240.0f) *
               Mat4::lookAt(position, position + forwardVector(), {0.0f, 1.0f, 0.0f});
    }

    Mat4 reflectedViewProjection(float aspect) const
    {
        const float fov = 70.0f + (28.0f - 70.0f) * scopeAmount;
        Vec3 reflectedPosition = position;
        Vec3 reflectedForward = forwardVector();
        reflectedPosition.z = MirrorZ * 2.0f - reflectedPosition.z;
        reflectedForward.z = -reflectedForward.z;
        return Mat4::perspective(fov, aspect, 0.05f, 240.0f) *
               Mat4::lookAt(reflectedPosition, reflectedPosition + reflectedForward, {0.0f, 1.0f, 0.0f});
    }

    bool mirrorScreenRect(int targetWidth, int targetHeight, float aspect, VkRect2D *rect) const
    {
        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const float y0 = mirrorGround + 0.55f;
        const float y1 = mirrorGround + mirrorHeight;
        const float z = MirrorZ + 0.56f;
        const std::array<Vec3, 4> corners{{
            {-mirrorWidth * 0.5f, y0, z},
            {mirrorWidth * 0.5f, y0, z},
            {mirrorWidth * 0.5f, y1, z},
            {-mirrorWidth * 0.5f, y1, z},
        }};

        const Mat4 mvp = viewProjection(aspect);
        float minX = 1.0f;
        float minY = 1.0f;
        float maxX = -1.0f;
        float maxY = -1.0f;
        bool anyVisible = false;
        for (const Vec3 &p : corners) {
            const float clipX = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8] * p.z + mvp.m[12];
            const float clipY = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9] * p.z + mvp.m[13];
            const float clipW = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
            if (clipW <= 0.001f) {
                continue;
            }
            const float ndcX = clipX / clipW;
            const float ndcY = clipY / clipW;
            minX = std::min(minX, ndcX);
            minY = std::min(minY, ndcY);
            maxX = std::max(maxX, ndcX);
            maxY = std::max(maxY, ndcY);
            anyVisible = true;
        }
        if (!anyVisible || maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) {
            return false;
        }

        minX = clamp(minX, -1.0f, 1.0f);
        maxX = clamp(maxX, -1.0f, 1.0f);
        minY = clamp(minY, -1.0f, 1.0f);
        maxY = clamp(maxY, -1.0f, 1.0f);
        const int x0 = std::max(0, int(std::floor((minX * 0.5f + 0.5f) * float(targetWidth))));
        const int x1 = std::min(targetWidth, int(std::ceil((maxX * 0.5f + 0.5f) * float(targetWidth))));
        const int y0Screen = std::max(0, int(std::floor((0.5f - maxY * 0.5f) * float(targetHeight))));
        const int y1Screen = std::min(targetHeight, int(std::ceil((0.5f - minY * 0.5f) * float(targetHeight))));
        if (x1 <= x0 || y1Screen <= y0Screen) {
            return false;
        }
        rect->offset = {x0, y0Screen};
        rect->extent = {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1Screen - y0Screen)};
        return true;
    }

    void updateWindowTitle(SDL_Window *window) const
    {
        const std::string title = "Vulkan World Shooter | FPS " + std::to_string(int(currentFps)) +
            " | Pos " + std::to_string(int(position.x)) + ", " + std::to_string(int(position.y)) + ", " + std::to_string(int(position.z)) +
            " | Shots " + std::to_string(totalShots) + " | Hits " + std::to_string(bulletMarks.size());
        SDL_SetWindowTitle(window, title.c_str());
    }

    void buildWorldMeshes(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        triangles.clear();
        lines.clear();
        addSky(triangles);
        addGround(triangles, lines);
        addNature(triangles, lines);
        addWorldGeometry(triangles, lines);
        if (pitch < -12.0f) {
            addPlayerModel(triangles, position, yaw, pitch, crouchAmount, false, true);
        }
        addFlyingBullets(triangles, lines);
        addBulletMarks(triangles);
    }

    void buildReflectionMeshes(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        triangles.clear();
        lines.clear();
        addSky(triangles);
        addGround(triangles, lines);
        addNature(triangles, lines);
        addWorldGeometry(triangles, lines);
        addPlayerModel(triangles, position, yaw, pitch, crouchAmount, true, true);
        addFlyingBullets(triangles, lines);
        addBulletMarks(triangles);
    }

    void buildMirrorMeshes(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        triangles.clear();
        lines.clear();
        addMirror(triangles, lines);
    }

    void buildMirrorMask(std::vector<Vertex> &triangles) const
    {
        triangles.clear();
        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const float y0 = mirrorGround + 0.55f;
        const float y1 = mirrorGround + mirrorHeight;
        const float z = MirrorZ + 0.56f;
        const Vec3 color{1.0f, 1.0f, 1.0f};
        addTriangle(triangles, {-mirrorWidth * 0.5f, y0, z}, {mirrorWidth * 0.5f, y0, z}, {mirrorWidth * 0.5f, y1, z}, color);
        addTriangle(triangles, {-mirrorWidth * 0.5f, y0, z}, {mirrorWidth * 0.5f, y1, z}, {-mirrorWidth * 0.5f, y1, z}, color);
    }

    void buildOverlay(std::vector<Vertex> &triangles, std::vector<Vertex> &lines, float aspect) const
    {
        triangles.clear();
        lines.clear();

        const Vec3 crosshair = shotFlash > 0.0f ? Vec3{1.0f, 0.84f, 0.37f} : Vec3{0.92f, 0.96f, 0.93f};
        const float gap = scopeAmount > 0.5f ? 0.010f : 0.017f;
        const float length = scopeAmount > 0.5f ? 0.023f : 0.038f;
        addLine2D(lines, {-length / aspect, 0.0f, 0.0f}, {-gap / aspect, 0.0f, 0.0f}, crosshair);
        addLine2D(lines, {gap / aspect, 0.0f, 0.0f}, {length / aspect, 0.0f, 0.0f}, crosshair);
        addLine2D(lines, {0.0f, -length, 0.0f}, {0.0f, -gap, 0.0f}, crosshair);
        addLine2D(lines, {0.0f, gap, 0.0f}, {0.0f, length, 0.0f}, crosshair);

        if (scopeAmount > 0.02f) {
            const float radius = 0.68f + scopeAmount * 0.06f;
            const Vec3 scopeColor{0.47f, 0.92f, 0.88f};
            constexpr int segments = 96;
            for (int i = 0; i < segments; ++i) {
                const float a0 = float(i) / float(segments) * Pi * 2.0f;
                const float a1 = float(i + 1) / float(segments) * Pi * 2.0f;
                addLine2D(lines, {std::cos(a0) * radius / aspect, std::sin(a0) * radius, 0.0f},
                          {std::cos(a1) * radius / aspect, std::sin(a1) * radius, 0.0f}, scopeColor);
            }
            addLine2D(lines, {-radius / aspect, 0.0f, 0.0f}, {-0.04f / aspect, 0.0f, 0.0f}, scopeColor);
            addLine2D(lines, {0.04f / aspect, 0.0f, 0.0f}, {radius / aspect, 0.0f, 0.0f}, scopeColor);
            addLine2D(lines, {0.0f, -radius, 0.0f}, {0.0f, -0.04f, 0.0f}, scopeColor);
            addLine2D(lines, {0.0f, 0.04f, 0.0f}, {0.0f, radius, 0.0f}, scopeColor);
        }
    }

private:
    std::vector<WorldBox> worldBoxes;
    std::vector<FlyingBullet> flyingBullets;
    std::vector<BulletMark> bulletMarks;
    Vec3 position{0.0f, 1.7f, 74.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float verticalVelocity = 0.0f;
    float shotFlash = 0.0f;
    float fpsTimer = 0.0f;
    float currentFps = 0.0f;
    float walkCycle = 0.0f;
    float crouchAmount = 0.0f;
    float scopeAmount = 0.0f;
    int totalShots = 0;
    int fpsFrames = 0;
    bool walking = false;
    bool grounded = true;
    bool jumpQueued = false;
    bool mouseCaptured = false;
    bool scoped = false;

    void buildWorld()
    {
        auto addBox = [&](float x, float y, float z, float sx, float sy, float sz, const Vec3 &color) {
            worldBoxes.push_back({{x, y, z}, {sx, sy, sz}, color, true});
        };

        addBox(0.0f, 7.5f, -96.0f, 78.0f, 15.0f, 3.0f, {0.54f, 0.58f, 0.63f});
        addBox(-54.0f, 5.5f, -56.0f, 28.0f, 11.0f, 26.0f, {0.44f, 0.52f, 0.56f});
        addBox(54.0f, 8.0f, -46.0f, 25.0f, 16.0f, 25.0f, {0.58f, 0.49f, 0.45f});
        addBox(0.0f, 3.0f, -28.0f, 34.0f, 6.0f, 22.0f, {0.38f, 0.46f, 0.50f});
        addBox(-28.0f, 9.0f, -14.0f, 10.0f, 18.0f, 10.0f, {0.69f, 0.61f, 0.49f});
        addBox(28.0f, 12.0f, -10.0f, 9.0f, 24.0f, 9.0f, {0.43f, 0.50f, 0.66f});
        addBox(0.0f, 0.6f, 18.0f, 56.0f, 1.2f, 20.0f, {0.31f, 0.41f, 0.39f});
        addBox(-78.0f, 3.0f, 12.0f, 20.0f, 6.0f, 48.0f, {0.48f, 0.54f, 0.47f});
        addBox(82.0f, 4.0f, 22.0f, 22.0f, 8.0f, 42.0f, {0.51f, 0.45f, 0.56f});
        for (int i = 0; i < 9; ++i) {
            addBox(-14.0f + i * 3.5f, 0.35f + i * 0.45f, 48.0f - i * 4.0f, 6.0f, 0.7f + i * 0.25f, 4.0f, {0.57f, 0.52f, 0.42f});
        }
        for (int i = 0; i < 12; ++i) {
            addBox(-45.0f, 0.3f + i * 0.55f, 50.0f - i * 4.2f, 16.0f, 0.6f + i * 0.22f, 3.8f, {0.46f, 0.57f, 0.51f});
        }
        for (int i = 0; i < 10; ++i) {
            addBox(46.0f, 0.35f + i * 0.75f, 42.0f - i * 4.0f, 14.0f, 0.7f + i * 0.28f, 3.6f, {0.56f, 0.47f, 0.49f});
        }
        addBox(-118.0f, 5.0f, 0.0f, 4.0f, 10.0f, 220.0f, {0.33f, 0.38f, 0.45f});
        addBox(118.0f, 5.0f, 0.0f, 4.0f, 10.0f, 220.0f, {0.33f, 0.38f, 0.45f});
        addBox(0.0f, 5.0f, -118.0f, 220.0f, 10.0f, 4.0f, {0.33f, 0.38f, 0.45f});
        addBox(0.0f, 5.0f, 118.0f, 220.0f, 10.0f, 4.0f, {0.33f, 0.38f, 0.45f});
    }

    void reset()
    {
        position = {0.0f, groundHeightAt(0.0f, 74.0f) + PlayerEyeHeight, 74.0f};
        yaw = -90.0f;
        pitch = 0.0f;
        verticalVelocity = 0.0f;
        shotFlash = 0.0f;
        walkCycle = 0.0f;
        crouchAmount = 0.0f;
        scopeAmount = 0.0f;
        totalShots = 0;
        walking = false;
        grounded = true;
        jumpQueued = false;
        scoped = false;
        flyingBullets.clear();
        bulletMarks.clear();
    }

    Vec3 forwardVector() const
    {
        const float yawRad = yaw * DegToRad;
        const float pitchRad = pitch * DegToRad;
        return Vec3{std::cos(yawRad) * std::cos(pitchRad), std::sin(pitchRad), std::sin(yawRad) * std::cos(pitchRad)}.normalized();
    }

    Vec3 rightVector() const
    {
        return cross(forwardVector(), {0.0f, 1.0f, 0.0f}).normalized();
    }

    float currentEyeHeight() const
    {
        return PlayerEyeHeight + (CrouchEyeHeight - PlayerEyeHeight) * crouchAmount;
    }

    float terrainHeightAt(float x, float z) const
    {
        return terrainNoise(x, z);
    }

    Vec3 terrainNormalAt(float x, float z) const
    {
        const float step = 0.75f;
        const float left = terrainHeightAt(x - step, z);
        const float right = terrainHeightAt(x + step, z);
        const float back = terrainHeightAt(x, z - step);
        const float front = terrainHeightAt(x, z + step);
        return Vec3{left - right, step * 2.0f, back - front}.normalized();
    }

    float groundHeightAt(float x, float z) const
    {
        float height = terrainHeightAt(x, z);
        for (const WorldBox &box : worldBoxes) {
            const Vec3 half = box.size * 0.5f;
            if (x >= box.center.x - half.x && x <= box.center.x + half.x &&
                z >= box.center.z - half.z && z <= box.center.z + half.z) {
                const float top = box.center.y + half.y;
                if (top <= MaxClimbHeight) {
                    height = std::max(height, top);
                }
            }
        }
        return height;
    }

    static bool intersectRayBox(const Vec3 &origin, const Vec3 &direction, const WorldBox &box, float *hitDistance, Vec3 *hitNormal)
    {
        const Vec3 minCorner = box.center - box.size * 0.5f;
        const Vec3 maxCorner = box.center + box.size * 0.5f;
        float tMin = 0.001f;
        float tMax = 500.0f;
        Vec3 enterNormal;

        auto testAxis = [&](float originAxis, float directionAxis, float minAxis, float maxAxis, const Vec3 &axisNormal) {
            if (std::abs(directionAxis) < 0.0001f) {
                return originAxis >= minAxis && originAxis <= maxAxis;
            }
            float t1 = (minAxis - originAxis) / directionAxis;
            float t2 = (maxAxis - originAxis) / directionAxis;
            Vec3 normal = -axisNormal;
            if (t1 > t2) {
                std::swap(t1, t2);
                normal = axisNormal;
            }
            if (t1 > tMin) {
                tMin = t1;
                enterNormal = normal;
            }
            tMax = std::min(tMax, t2);
            return tMin <= tMax;
        };

        if (!testAxis(origin.x, direction.x, minCorner.x, maxCorner.x, {1.0f, 0.0f, 0.0f}) ||
            !testAxis(origin.y, direction.y, minCorner.y, maxCorner.y, {0.0f, 1.0f, 0.0f}) ||
            !testAxis(origin.z, direction.z, minCorner.z, maxCorner.z, {0.0f, 0.0f, 1.0f})) {
            return false;
        }

        *hitDistance = tMin;
        *hitNormal = enterNormal;
        return true;
    }

    SurfaceHit traceShot(const Vec3 &origin, const Vec3 &direction) const
    {
        SurfaceHit best;
        best.distance = 500.0f;
        for (float t = 0.5f; t < best.distance; t += 1.0f) {
            const Vec3 p = origin + direction * t;
            if (std::abs(p.x) > WorldHalfSize || std::abs(p.z) > WorldHalfSize) {
                break;
            }
            const float terrain = terrainHeightAt(p.x, p.z);
            if (p.y <= terrain) {
                best = {{p.x, terrain, p.z}, terrainNormalAt(p.x, p.z), t, true};
                break;
            }
        }
        for (const WorldBox &box : worldBoxes) {
            float distance = 0.0f;
            Vec3 normal;
            if (intersectRayBox(origin, direction, box, &distance, &normal) && distance < best.distance) {
                best = {origin + direction * distance, normal, distance, true};
            }
        }
        return best;
    }

    void shoot()
    {
        ++totalShots;
        shotFlash = 1.0f;
        const Vec3 direction = forwardVector();
        const Vec3 start = position + rightVector() * 0.18f + direction * 0.45f + Vec3{0.0f, -0.10f, 0.0f};
        const SurfaceHit hit = traceShot(position, direction);
        if (hit.hit) {
            const float distance = std::max(0.2f, (hit.position - start).length());
            flyingBullets.push_back({start, hit.position, hit.normal, distance, 0.0f, true});
        } else {
            flyingBullets.push_back({start, start + direction * 180.0f, {0.0f, 1.0f, 0.0f}, 180.0f, 0.0f, false});
        }
    }

    static void addVertex(std::vector<Vertex> &vertices, const Vec3 &position, const Vec3 &color)
    {
        vertices.push_back({position, color});
    }

    static void addTriangle(std::vector<Vertex> &triangles, const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &color)
    {
        addVertex(triangles, a, color);
        addVertex(triangles, b, color);
        addVertex(triangles, c, color);
    }

    static void addLine(std::vector<Vertex> &lines, const Vec3 &a, const Vec3 &b, const Vec3 &color)
    {
        addVertex(lines, a, color);
        addVertex(lines, b, color);
    }

    static void addLine2D(std::vector<Vertex> &lines, const Vec3 &a, const Vec3 &b, const Vec3 &color)
    {
        addLine(lines, {a.x, a.y, 0.0f}, {b.x, b.y, 0.0f}, color);
    }

    static void addBox(std::vector<Vertex> &triangles, const Vec3 &center, const Vec3 &size, const Vec3 &color)
    {
        const float x0 = center.x - size.x * 0.5f;
        const float x1 = center.x + size.x * 0.5f;
        const float y0 = center.y - size.y * 0.5f;
        const float y1 = center.y + size.y * 0.5f;
        const float z0 = center.z - size.z * 0.5f;
        const float z1 = center.z + size.z * 0.5f;
        const std::array<Vec3, 8> p = {{
            {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
        }};
        auto quad = [&](int a, int b, int c, int d, float shade) {
            const Vec3 shaded = color * shade;
            addTriangle(triangles, p[a], p[b], p[c], shaded);
            addTriangle(triangles, p[a], p[c], p[d], shaded);
        };
        quad(4, 5, 6, 7, 1.00f);
        quad(1, 0, 3, 2, 0.72f);
        quad(3, 7, 6, 2, 1.16f);
        quad(0, 1, 5, 4, 0.58f);
        quad(0, 4, 7, 3, 0.82f);
        quad(5, 1, 2, 6, 0.90f);
    }

    static Vec3 rotateY(const Vec3 &v, float degrees)
    {
        const float r = degrees * DegToRad;
        const float c = std::cos(r);
        const float s = std::sin(r);
        return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
    }

    static void addTransformedBox(std::vector<Vertex> &triangles, const Vec3 &center, const Vec3 &size, const Vec3 &color, float yawDegrees = 0.0f)
    {
        std::vector<Vertex> local;
        addBox(local, {0.0f, 0.0f, 0.0f}, size, color);
        for (Vertex &v : local) {
            v.position = rotateY(v.position, yawDegrees) + center;
            triangles.push_back(v);
        }
    }

    static void addSegmentBox(std::vector<Vertex> &triangles, const Vec3 &start, const Vec3 &end, float thickness, const Vec3 &color)
    {
        const Vec3 delta = end - start;
        const float length = delta.length();
        if (length < 0.001f) {
            return;
        }
        const float yawDegrees = std::atan2(delta.x, delta.z) / DegToRad;
        addTransformedBox(triangles, (start + end) * 0.5f, {thickness, thickness, length}, color, yawDegrees);
    }

    void addSky(std::vector<Vertex> &triangles) const
    {
        const float y = -20.0f;
        addTriangle(triangles, {-220.0f, y, -220.0f}, {220.0f, y, -220.0f}, {220.0f, 160.0f, -220.0f}, {0.38f, 0.55f, 0.72f});
        addTriangle(triangles, {-220.0f, y, -220.0f}, {220.0f, 160.0f, -220.0f}, {-220.0f, 160.0f, -220.0f}, {0.14f, 0.18f, 0.28f});
    }

    void addGround(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        constexpr int cells = 60;
        const float step = (WorldHalfSize * 2.0f) / float(cells);
        for (int z = 0; z < cells; ++z) {
            for (int x = 0; x < cells; ++x) {
                const float x0 = -WorldHalfSize + x * step;
                const float x1 = x0 + step;
                const float z0 = -WorldHalfSize + z * step;
                const float z1 = z0 + step;
                const Vec3 a{x0, terrainHeightAt(x0, z0), z0};
                const Vec3 b{x1, terrainHeightAt(x1, z0), z0};
                const Vec3 c{x1, terrainHeightAt(x1, z1), z1};
                const Vec3 d{x0, terrainHeightAt(x0, z1), z1};
                const float lush = clamp(((a.y + b.y + c.y + d.y) * 0.25f + 1.5f) / 7.5f, 0.0f, 1.0f);
                const float patch = deterministic01(x * 97 + z * 193);
                const float fine = 0.5f + 0.5f * std::sin(x0 * 0.31f + z0 * 0.27f);
                const float shade = 0.78f + patch * 0.22f + fine * 0.08f;
                const Vec3 base{0.08f + lush * 0.10f, 0.26f + lush * 0.26f, 0.10f + lush * 0.10f};
                const Vec3 color{base.x * shade, base.y * shade, base.z * shade};
                addTriangle(triangles, a, b, c, color);
                addTriangle(triangles, a, c, d, color * (0.82f + patch * 0.16f));

                if ((x + z) % 3 == 0) {
                    const float inset = step * 0.22f;
                    const float px0 = x0 + inset;
                    const float px1 = x1 - inset;
                    const float pz = z0 + step * (0.25f + patch * 0.5f);
                    addLine(lines,
                            {px0, terrainHeightAt(px0, pz) + 0.05f, pz},
                            {px1, terrainHeightAt(px1, pz) + 0.05f, pz},
                            {0.10f, 0.21f + lush * 0.18f, 0.09f});
                }
            }
        }
    }

    void addNature(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        for (int i = 0; i < 520; ++i) {
            const float x = -WorldHalfSize + deterministic01(i * 17 + 5) * WorldHalfSize * 2.0f;
            const float z = -WorldHalfSize + deterministic01(i * 29 + 11) * WorldHalfSize * 2.0f;
            if (std::abs(x) < 14.0f && z > 42.0f && z < 66.0f) {
                continue;
            }
            const float y = terrainHeightAt(x, z) + 0.14f;
            const float blade = 0.34f + deterministic01(i * 31 + 9) * 0.48f;
            const float sway = (deterministic01(i * 13 + 3) - 0.5f) * 0.22f;
            addLine(lines, {x, y, z}, {x + sway, y + blade, z + sway * 0.45f}, {0.18f + deterministic01(i) * 0.10f, 0.48f + deterministic01(i + 7) * 0.22f, 0.18f});
        }

        for (int i = 0; i < 34; ++i) {
            const float x = -130.0f + deterministic01(i * 41 + 2) * 260.0f;
            const float z = -132.0f + deterministic01(i * 47 + 8) * 264.0f;
            if ((std::abs(x) < 24.0f && z > 36.0f && z < 76.0f) || std::abs(z - MirrorZ) < 7.0f) {
                continue;
            }
            const float y = terrainHeightAt(x, z) + 0.10f;
            const float trunkHeight = 1.7f + deterministic01(i * 19) * 1.6f;
            addBox(triangles, {x, y + trunkHeight * 0.5f, z}, {0.55f, trunkHeight, 0.55f}, {0.25f, 0.16f, 0.09f});
            addBox(triangles, {x, y + 0.08f, z}, {1.10f, 0.16f, 1.10f}, {0.18f, 0.11f, 0.06f});
            addBox(triangles, {x, y + trunkHeight + 0.75f, z}, {3.0f, 1.6f, 3.0f}, {0.10f, 0.34f + deterministic01(i + 3) * 0.16f, 0.15f});
            addBox(triangles, {x, y + trunkHeight + 1.65f, z}, {2.1f, 1.3f, 2.1f}, {0.08f, 0.28f, 0.13f});
        }

        for (int i = 0; i < 46; ++i) {
            const float x = -140.0f + deterministic01(i * 53 + 1) * 280.0f;
            const float z = -140.0f + deterministic01(i * 59 + 4) * 280.0f;
            const float y = terrainHeightAt(x, z) + 0.18f;
            const float scale = 0.45f + deterministic01(i * 23) * 1.25f;
            addBox(triangles, {x, y, z}, {scale * 1.4f, scale * 0.55f, scale}, {0.28f, 0.30f, 0.28f});
        }
    }

    void addWorldGeometry(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        for (const WorldBox &box : worldBoxes) {
            addBox(triangles, box.center, box.size, box.color);
            const Vec3 half = box.size * 0.5f;
            const float y = box.center.y + half.y + 0.03f;
            const Vec3 c{0.90f, 0.96f, 0.90f};
            addLine(lines, {box.center.x - half.x, y, box.center.z - half.z}, {box.center.x + half.x, y, box.center.z - half.z}, c);
            addLine(lines, {box.center.x + half.x, y, box.center.z - half.z}, {box.center.x + half.x, y, box.center.z + half.z}, c);
            addLine(lines, {box.center.x + half.x, y, box.center.z + half.z}, {box.center.x - half.x, y, box.center.z + half.z}, c);
            addLine(lines, {box.center.x - half.x, y, box.center.z + half.z}, {box.center.x - half.x, y, box.center.z - half.z}, c);
        }
    }

    void addPlayerModel(std::vector<Vertex> &triangles, const Vec3 &pos, float yawDegrees, float pitchDegrees, float crouch, bool includeHead, bool includeWeapon) const
    {
        const float stride = walking ? std::sin(walkCycle) : 0.0f;
        const float counterStride = walking ? std::sin(walkCycle + Pi) : 0.0f;
        const float footLift = walking ? std::abs(std::sin(walkCycle)) * 0.06f : 0.0f;
        const float bodyBob = walking ? std::abs(std::sin(walkCycle * 2.0f)) * 0.035f : 0.0f;
        const float hipDrop = crouch * 0.26f;
        const float torsoDrop = crouch * 0.42f;
        const float headDrop = crouch * 0.52f;
        const float kneeForward = crouch * 0.28f;
        const float modelYaw = -(yawDegrees + 90.0f);

        auto tx = [&](Vec3 local) {
            local = rotateY(local, modelYaw);
            return local + Vec3{pos.x, pos.y - currentEyeHeight() + bodyBob, pos.z};
        };
        auto box = [&](const Vec3 &local, const Vec3 &size, const Vec3 &color) {
            addTransformedBox(triangles, tx(local), size, color, modelYaw);
        };
        auto segment = [&](const Vec3 &a, const Vec3 &b, float thickness, const Vec3 &color) {
            addSegmentBox(triangles, tx(a), tx(b), thickness, color);
        };

        const Vec3 suitDark{0.09f, 0.11f, 0.16f};
        const Vec3 suitMid{0.18f, 0.23f, 0.29f};
        const Vec3 suitLight{0.24f, 0.31f, 0.38f};
        const Vec3 armor{0.34f, 0.40f, 0.42f};
        const Vec3 skin{0.62f, 0.50f, 0.41f};
        const Vec3 black{0.035f, 0.040f, 0.050f};
        const Vec3 cyan{0.10f, 0.45f, 0.50f};

        box({0.0f, 1.18f - torsoDrop, -0.18f}, {0.72f, 0.54f, 0.34f}, suitMid);
        box({0.0f, 0.89f - hipDrop, -0.14f}, {0.56f, 0.38f, 0.30f}, suitMid);
        box({0.0f, 1.43f - torsoDrop, -0.20f}, {0.92f, 0.18f, 0.30f}, suitLight);
        box({0.0f, 1.30f - torsoDrop, -0.45f}, {0.18f, 0.12f, 0.05f}, cyan);

        const Vec3 leftHip{-0.22f, 0.66f - hipDrop, -0.10f};
        const Vec3 rightHip{0.22f, 0.66f - hipDrop, -0.10f};
        const Vec3 leftKnee{-0.21f, 0.38f + footLift * 0.4f - crouch * 0.08f, -0.12f + stride * 0.10f - kneeForward};
        const Vec3 rightKnee{0.21f, 0.38f + footLift * 0.4f - crouch * 0.08f, -0.12f + counterStride * 0.10f - kneeForward};
        const Vec3 leftAnkle{-0.21f, 0.16f + footLift, -0.18f + stride * 0.18f};
        const Vec3 rightAnkle{0.21f, 0.16f + footLift, -0.18f + counterStride * 0.18f};
        segment(leftHip, leftKnee, 0.19f, suitDark);
        segment(leftKnee, leftAnkle, 0.17f, suitDark);
        segment(rightHip, rightKnee, 0.19f, suitDark);
        segment(rightKnee, rightAnkle, 0.17f, suitDark);
        box(leftKnee, {0.23f, 0.18f, 0.22f}, armor);
        box(rightKnee, {0.23f, 0.18f, 0.22f}, armor);
        box({-0.21f, 0.06f + footLift, -0.30f + stride * 0.20f}, {0.34f, 0.16f, 0.54f}, black);
        box({0.21f, 0.06f + footLift, -0.30f + counterStride * 0.20f}, {0.34f, 0.16f, 0.54f}, black);

        const Vec3 leftShoulder{-0.46f, 1.38f - torsoDrop, -0.24f};
        const Vec3 rightShoulder{0.46f, 1.38f - torsoDrop, -0.24f};
        const Vec3 leftElbow{-0.47f, 1.10f - torsoDrop * 0.80f, -0.58f};
        const Vec3 rightElbow{0.46f, 1.04f - torsoDrop * 0.80f, -0.42f};
        const Vec3 leftHand{-0.18f, 0.88f - torsoDrop * 0.62f, -1.05f};
        const Vec3 rightHand{0.15f, 0.76f - torsoDrop * 0.62f, -0.60f};
        segment(leftShoulder, leftElbow, 0.17f, suitLight);
        segment(leftElbow, leftHand, 0.15f, suitMid);
        segment(rightShoulder, rightElbow, 0.17f, suitLight);
        segment(rightElbow, rightHand, 0.15f, suitMid);
        box(leftShoulder, {0.24f, 0.22f, 0.24f}, suitLight);
        box(rightShoulder, {0.24f, 0.22f, 0.24f}, suitLight);
        box(leftHand, {0.18f, 0.15f, 0.20f}, skin);
        box(rightHand, {0.18f, 0.15f, 0.20f}, skin);

        if (includeHead) {
            box({0.0f, 1.72f - headDrop, -0.20f}, {0.42f, 0.42f, 0.36f}, skin);
            box({0.0f, 1.98f - headDrop, -0.21f}, {0.48f, 0.16f, 0.40f}, black);
        }

        if (includeWeapon) {
            const Vec3 gunDark{0.055f, 0.065f, 0.075f};
            const Vec3 gunBody{0.13f, 0.16f, 0.18f};
            const Vec3 gunMetal{0.34f, 0.38f, 0.40f};
            const float pitchPush = -pitchDegrees * 0.004f;
            box({0.0f, 1.04f + pitchPush, -0.72f}, {0.42f, 0.28f, 0.58f}, gunBody);
            box({0.0f, 1.03f + pitchPush, -1.16f}, {0.18f, 0.18f, 0.70f}, gunDark);
            box({0.0f, 1.34f + pitchPush, -0.88f}, {0.23f, 0.17f, 0.18f}, gunDark);
            box({0.0f, 1.34f + pitchPush, -1.02f}, {0.12f, 0.09f, 0.16f}, cyan);
            box({0.0f, 0.83f + pitchPush, -0.56f}, {0.20f, 0.44f, 0.18f}, gunDark);
            box({0.0f, 1.03f + pitchPush, -1.58f}, {0.28f, 0.24f, 0.18f}, gunMetal);
            box({0.0f, 1.03f + pitchPush, -1.70f}, {0.16f, 0.16f, 0.08f}, shotFlash > 0.0f ? Vec3{1.0f, 0.70f, 0.16f} : Vec3{0.82f, 0.42f, 0.10f});
        }
    }

    void addMirror(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const float y0 = mirrorGround + 0.55f;
        const float y1 = mirrorGround + mirrorHeight;
        const float faceZ = MirrorZ + 0.56f;
        const Vec3 frame{0.05f, 0.06f, 0.08f};

        addBox(triangles, {0.0f, y0 - 0.28f, MirrorZ + 0.18f}, {mirrorWidth + 1.2f, 0.56f, 0.48f}, frame);
        addBox(triangles, {0.0f, y1 + 0.28f, MirrorZ + 0.18f}, {mirrorWidth + 1.2f, 0.56f, 0.48f}, frame);
        addBox(triangles, {-mirrorWidth * 0.5f - 0.28f, (y0 + y1) * 0.5f, MirrorZ + 0.18f}, {0.56f, mirrorHeight + 0.56f, 0.48f}, frame);
        addBox(triangles, {mirrorWidth * 0.5f + 0.28f, (y0 + y1) * 0.5f, MirrorZ + 0.18f}, {0.56f, mirrorHeight + 0.56f, 0.48f}, frame);

        addLine(lines, {-mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {-mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {-mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {-mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {-mirrorWidth * 0.36f, y1 - 0.7f, faceZ + 0.04f}, {mirrorWidth * 0.12f, y0 + 0.75f, faceZ + 0.04f}, {0.90f, 1.0f, 1.0f});
        addLine(lines, {-mirrorWidth * 0.10f, y1 - 0.45f, faceZ + 0.04f}, {mirrorWidth * 0.34f, y0 + 1.25f, faceZ + 0.04f}, {0.72f, 0.92f, 1.0f});
    }

    void addFlyingBullets(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        for (const FlyingBullet &bullet : flyingBullets) {
            const Vec3 direction = (bullet.target - bullet.start).normalized();
            const float visibleTravel = std::min(bullet.traveled, bullet.distance);
            const Vec3 p = bullet.start + direction * visibleTravel;
            const Vec3 trailStart = bullet.start + direction * std::max(0.0f, visibleTravel - 2.8f);
            addLine(lines, trailStart, p, {1.0f, 0.76f, 0.20f});
            addBox(triangles, p, {0.11f, 0.11f, 0.11f}, {1.0f, 0.92f, 0.34f});
        }
    }

    static void addDecalQuad(std::vector<Vertex> &triangles, const Vec3 &position, const Vec3 &normal, float size, const Vec3 &color)
    {
        Vec3 tangent = cross(normal, {0.0f, 1.0f, 0.0f});
        if (tangent.lengthSquared() < 0.01f) {
            tangent = cross(normal, {1.0f, 0.0f, 0.0f});
        }
        tangent = tangent.normalized();
        const Vec3 bitangent = cross(normal, tangent).normalized();
        const Vec3 p = position + normal * 0.018f;
        const Vec3 a = p - tangent * size - bitangent * size;
        const Vec3 b = p + tangent * size - bitangent * size;
        const Vec3 c = p + tangent * size + bitangent * size;
        const Vec3 d = p - tangent * size + bitangent * size;
        addTriangle(triangles, a, b, c, color);
        addTriangle(triangles, a, c, d, color);
    }

    void addBulletMarks(std::vector<Vertex> &triangles) const
    {
        for (const BulletMark &mark : bulletMarks) {
            const float glow = clamp(1.0f - mark.age * 2.2f, 0.0f, 1.0f);
            addDecalQuad(triangles, mark.position, mark.normal, 0.20f, {0.08f + glow * 0.92f, 0.02f + glow * 0.22f, 0.015f});
        }
    }
};

std::vector<uint32_t> compileShader(const char *source, shaderc_shader_kind kind, const char *name)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(source, std::strlen(source), kind, name, options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error(result.GetErrorMessage());
    }
    return {result.cbegin(), result.cend()};
}

const char *VertexShader = R"(
#version 450
layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
}
)";

const char *FragmentShader = R"(
#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(fragColor, 1.0);
}
)";

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

enum class StencilMode {
    Off,
    WriteMirror,
    TestMirror,
};

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable Vulkan memory type found");
}

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("No supported Vulkan depth/stencil format found");
}

class VulkanApp {
public:
    VulkanApp()
    {
        initWindow();
        initVulkan();
    }

    ~VulkanApp()
    {
        cleanup();
    }

    void run()
    {
        auto previous = std::chrono::steady_clock::now();
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                } else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    framebufferResized = true;
                }
                game.handleEvent(event);
            }

            const auto now = std::chrono::steady_clock::now();
            const float dt = std::min(0.05f, std::chrono::duration<float>(now - previous).count());
            previous = now;
            game.update(dt);
            game.setRelativeMouseMode(window);
            titleTimer += dt;
            if (titleTimer > 0.35f) {
                game.updateWindowTitle(window);
                titleTimer = 0.0f;
            }
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

private:
    SDL_Window *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline trianglePipeline = VK_NULL_HANDLE;
    VkPipeline linePipeline = VK_NULL_HANDLE;
    VkPipeline mirrorMaskPipeline = VK_NULL_HANDLE;
    VkPipeline reflectedTrianglePipeline = VK_NULL_HANDLE;
    VkPipeline reflectedLinePipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    size_t currentFrame = 0;
    bool framebufferResized = false;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    size_t vertexBufferCapacity = 0;

    Game game;
    std::vector<Vertex> triangles;
    std::vector<Vertex> lines;
    std::vector<Vertex> reflectionTriangles;
    std::vector<Vertex> reflectionLines;
    std::vector<Vertex> mirrorMaskTriangles;
    std::vector<Vertex> mirrorTriangles;
    std::vector<Vertex> mirrorLines;
    std::vector<Vertex> overlayTriangles;
    std::vector<Vertex> overlayLines;
    float titleTimer = 0.0f;

    void initWindow()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        window = SDL_CreateWindow("Vulkan World Shooter", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
    }

    void initVulkan()
    {
        createInstance();
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createPipelines();
        createDepthResources();
        createFramebuffers();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    void createInstance()
    {
        Uint32 sdlExtensionCount = 0;
        const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
        if (!sdlExtensions) {
            throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
        }
        std::vector<const char *> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Vulkan World Shooter";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "Custom SDL3 Vulkan";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateInstance failed");
        }
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice candidate) const
    {
        QueueFamilyIndices indices;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
        for (uint32_t i = 0; i < count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics = i;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &presentSupport);
            if (presentSupport) {
                indices.present = i;
            }
            if (indices.complete()) {
                break;
            }
        }
        return indices;
    }

    bool deviceSuitable(VkPhysicalDevice candidate) const
    {
        const QueueFamilyIndices indices = findQueueFamilies(candidate);
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> available(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, available.data());
        bool hasSwapchain = false;
        for (const auto &extension : available) {
            if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
                break;
            }
        }
        return indices.complete() && hasSwapchain;
    }

    void pickPhysicalDevice()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0) {
            throw std::runtime_error("No Vulkan physical devices found");
        }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (VkPhysicalDevice candidate : devices) {
            if (deviceSuitable(candidate)) {
                physicalDevice = candidate;
                return;
            }
        }
        throw std::runtime_error("No suitable Vulkan physical device found");
    }

    void createLogicalDevice()
    {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        std::set<uint32_t> uniqueQueues = {*indices.graphics, *indices.present};
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (uint32_t queueFamily : uniqueQueues) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueInfos.push_back(queueInfo);
        }

        const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceFeatures features{};
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDevice failed");
        }
        vkGetDeviceQueue(device, *indices.graphics, 0, &graphicsQueue);
        vkGetDeviceQueue(device, *indices.present, 0, &presentQueue);
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const
    {
        for (const auto &format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.front();
    }

    VkFormat depthStencilFormat() const
    {
        return findSupportedFormat(physicalDevice,
                                   {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &modes) const
    {
        for (const auto &mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        return {
            std::clamp(static_cast<uint32_t>(std::max(1, width)), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(static_cast<uint32_t>(std::max(1, height)), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    void createSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

        const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
        const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
        const VkExtent2D extent = chooseExtent(capabilities);
        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        const uint32_t queueFamilies[] = {*indices.graphics, *indices.present};
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (indices.graphics != indices.present) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateSwapchainKHR failed");
        }
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
        swapchainImageFormat = surfaceFormat.format;
        swapchainExtent = extent;
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = format;
        createInfo.subresourceRange.aspectMask = aspect;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        VkImageView view;
        if (vkCreateImageView(device, &createInfo, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView failed");
        }
        return view;
    }

    void createImageViews()
    {
        swapchainImageViews.resize(swapchainImages.size());
        for (size_t i = 0; i < swapchainImages.size(); ++i) {
            swapchainImageViews[i] = createImageView(swapchainImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    void createRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthStencilFormat();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};
        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateRenderPass failed");
        }
    }

    VkShaderModule createShaderModule(const std::vector<uint32_t> &code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateShaderModule failed");
        }
        return module;
    }

    VkPipeline createPipeline(VkPrimitiveTopology topology, bool depthTest, bool depthWrite, bool colorWrite, StencilMode stencilMode)
    {
        const auto vertCode = compileShader(VertexShader, shaderc_vertex_shader, "world.vert");
        const auto fragCode = compileShader(FragmentShader, shaderc_fragment_shader, "world.frag");
        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = topology;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        depth.stencilTestEnable = stencilMode == StencilMode::Off ? VK_FALSE : VK_TRUE;
        if (stencilMode == StencilMode::WriteMirror) {
            depth.front.compareOp = VK_COMPARE_OP_ALWAYS;
            depth.front.passOp = VK_STENCIL_OP_REPLACE;
            depth.front.failOp = VK_STENCIL_OP_KEEP;
            depth.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depth.front.compareMask = 0xff;
            depth.front.writeMask = 0xff;
            depth.front.reference = 1;
            depth.back = depth.front;
        } else if (stencilMode == StencilMode::TestMirror) {
            depth.front.compareOp = VK_COMPARE_OP_EQUAL;
            depth.front.passOp = VK_STENCIL_OP_KEEP;
            depth.front.failOp = VK_STENCIL_OP_KEEP;
            depth.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depth.front.compareMask = 0xff;
            depth.front.writeMask = 0x00;
            depth.front.reference = 1;
            depth.back = depth.front;
        }

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.colorWriteMask = colorWrite
            ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            : 0;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &colorBlend;

        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.stageCount = 2;
        createInfo.pStages = stages;
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &assembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterizer;
        createInfo.pMultisampleState = &multisample;
        createInfo.pDepthStencilState = &depth;
        createInfo.pColorBlendState = &blend;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = pipelineLayout;
        createInfo.renderPass = renderPass;
        createInfo.subpass = 0;
        VkPipeline pipeline;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateGraphicsPipelines failed");
        }
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        return pipeline;
    }

    void createPipelines()
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Mat4);
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("vkCreatePipelineLayout failed");
        }
        trianglePipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true, StencilMode::Off);
        linePipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, true, true, StencilMode::Off);
        mirrorMaskPipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, false, StencilMode::WriteMirror);
        reflectedTrianglePipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false, true, StencilMode::TestMirror);
        reflectedLinePipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, false, true, StencilMode::TestMirror);
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &memory)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateBuffer failed");
        }
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits, properties);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateMemory failed");
        }
        vkBindBufferMemory(device, buffer, memory, 0);
    }

    void ensureVertexBuffer(size_t vertexCount)
    {
        if (vertexCount <= vertexBufferCapacity) {
            return;
        }
        vkDeviceWaitIdle(device);
        if (vertexBuffer) {
            vkDestroyBuffer(device, vertexBuffer, nullptr);
            vkFreeMemory(device, vertexMemory, nullptr);
        }
        vertexBufferCapacity = std::max(vertexCount, vertexBufferCapacity * 2 + 8192);
        createBuffer(sizeof(Vertex) * vertexBufferCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vertexBuffer, vertexMemory);
    }

    void createDepthResources()
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthStencilFormat();
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &imageInfo, nullptr, &depthImage) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImage failed");
        }
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, depthImage, &requirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateMemory depth failed");
        }
        vkBindImageMemory(device, depthImage, depthMemory, 0);
        depthImageView = createImageView(depthImage, depthStencilFormat(), VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    }

    void createFramebuffers()
    {
        framebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
            std::array<VkImageView, 2> attachments{swapchainImageViews[i], depthImageView};
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = renderPass;
            info.attachmentCount = static_cast<uint32_t>(attachments.size());
            info.pAttachments = attachments.data();
            info.width = swapchainExtent.width;
            info.height = swapchainExtent.height;
            info.layers = 1;
            if (vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateFramebuffer failed");
            }
        }
    }

    void createCommandPool()
    {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = *indices.graphics;
        if (vkCreateCommandPool(device, &info, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateCommandPool failed");
        }
    }

    void createCommandBuffers()
    {
        commandBuffers.resize(MaxFramesInFlight);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateCommandBuffers failed");
        }
    }

    void createSyncObjects()
    {
        imageAvailableSemaphores.resize(MaxFramesInFlight);
        renderFinishedSemaphores.resize(MaxFramesInFlight);
        inFlightFences.resize(MaxFramesInFlight);
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MaxFramesInFlight; ++i) {
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create sync objects");
            }
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer,
                             uint32_t imageIndex,
                             uint32_t triCount,
                             uint32_t lineCount,
                             uint32_t reflectionTriCount,
                             uint32_t reflectionLineCount,
                             uint32_t mirrorMaskTriCount,
                             uint32_t mirrorTriCount,
                             uint32_t mirrorLineCount,
                             uint32_t overlayTriCount,
                             uint32_t overlayLineCount)
    {
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.36f, 0.49f, 0.68f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderInfo.renderPass = renderPass;
        renderInfo.framebuffer = framebuffers[imageIndex];
        renderInfo.renderArea.offset = {0, 0};
        renderInfo.renderArea.extent = swapchainExtent;
        renderInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkBuffer buffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

        VkViewport fullViewport{0.0f, 0.0f, float(swapchainExtent.width), float(swapchainExtent.height), 0.0f, 1.0f};
        VkRect2D fullScissor{{0, 0}, swapchainExtent};
        vkCmdSetViewport(commandBuffer, 0, 1, &fullViewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);

        const float aspect = float(swapchainExtent.width) / float(std::max(1u, swapchainExtent.height));
        Mat4 worldMvp = game.viewProjection(aspect);
        Mat4 reflectionMvp = game.reflectedViewProjection(aspect);
        Mat4 overlayMvp = Mat4::identity();

        uint32_t first = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), worldMvp.m.data());
        if (triCount > 0) {
            vkCmdDraw(commandBuffer, triCount, 1, first, 0);
        }
        first += triCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), worldMvp.m.data());
        if (lineCount > 0) {
            vkCmdDraw(commandBuffer, lineCount, 1, first, 0);
        }
        first += lineCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mirrorMaskPipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), worldMvp.m.data());
        if (mirrorMaskTriCount > 0) {
            vkCmdDraw(commandBuffer, mirrorMaskTriCount, 1, first, 0);
        }
        first += mirrorMaskTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, reflectedTrianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), reflectionMvp.m.data());
        if (reflectionTriCount > 0) {
            vkCmdDraw(commandBuffer, reflectionTriCount, 1, first, 0);
        }
        first += reflectionTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, reflectedLinePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), reflectionMvp.m.data());
        if (reflectionLineCount > 0) {
            vkCmdDraw(commandBuffer, reflectionLineCount, 1, first, 0);
        }
        first += reflectionLineCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), worldMvp.m.data());
        if (mirrorTriCount > 0) {
            vkCmdDraw(commandBuffer, mirrorTriCount, 1, first, 0);
        }
        first += mirrorTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), worldMvp.m.data());
        if (mirrorLineCount > 0) {
            vkCmdDraw(commandBuffer, mirrorLineCount, 1, first, 0);
        }
        first += mirrorLineCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), overlayMvp.m.data());
        if (overlayTriCount > 0) {
            vkCmdDraw(commandBuffer, overlayTriCount, 1, first, 0);
        }
        first += overlayTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), overlayMvp.m.data());
        if (overlayLineCount > 0) {
            vkCmdDraw(commandBuffer, overlayLineCount, 1, first, 0);
        }

        vkCmdEndRenderPass(commandBuffer);
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkEndCommandBuffer failed");
        }
    }

    void uploadVertices(const std::vector<Vertex> &all)
    {
        ensureVertexBuffer(all.size());
        void *data = nullptr;
        vkMapMemory(device, vertexMemory, 0, sizeof(Vertex) * all.size(), 0, &data);
        std::memcpy(data, all.data(), sizeof(Vertex) * all.size());
        vkUnmapMemory(device, vertexMemory);
    }

    void drawFrame()
    {
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        }

        const float aspect = float(swapchainExtent.width) / float(std::max(1u, swapchainExtent.height));
        game.buildWorldMeshes(triangles, lines);
        game.buildReflectionMeshes(reflectionTriangles, reflectionLines);
        game.buildMirrorMask(mirrorMaskTriangles);
        game.buildMirrorMeshes(mirrorTriangles, mirrorLines);
        game.buildOverlay(overlayTriangles, overlayLines, aspect);
        std::vector<Vertex> all;
        all.reserve(triangles.size() + lines.size() +
                    reflectionTriangles.size() + reflectionLines.size() +
                    mirrorMaskTriangles.size() +
                    mirrorTriangles.size() + mirrorLines.size() +
                    overlayTriangles.size() + overlayLines.size());
        all.insert(all.end(), triangles.begin(), triangles.end());
        all.insert(all.end(), lines.begin(), lines.end());
        all.insert(all.end(), mirrorMaskTriangles.begin(), mirrorMaskTriangles.end());
        all.insert(all.end(), reflectionTriangles.begin(), reflectionTriangles.end());
        all.insert(all.end(), reflectionLines.begin(), reflectionLines.end());
        all.insert(all.end(), mirrorTriangles.begin(), mirrorTriangles.end());
        all.insert(all.end(), mirrorLines.begin(), mirrorLines.end());
        all.insert(all.end(), overlayTriangles.begin(), overlayTriangles.end());
        all.insert(all.end(), overlayLines.begin(), overlayLines.end());
        uploadVertices(all);

        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex,
                            static_cast<uint32_t>(triangles.size()),
                            static_cast<uint32_t>(lines.size()),
                            static_cast<uint32_t>(reflectionTriangles.size()),
                            static_cast<uint32_t>(reflectionLines.size()),
                            static_cast<uint32_t>(mirrorMaskTriangles.size()),
                            static_cast<uint32_t>(mirrorTriangles.size()),
                            static_cast<uint32_t>(mirrorLines.size()),
                            static_cast<uint32_t>(overlayTriangles.size()),
                            static_cast<uint32_t>(overlayLines.size()));

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("vkQueueSubmit failed");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapchain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("vkQueuePresentKHR failed");
        }
        currentFrame = (currentFrame + 1) % MaxFramesInFlight;
    }

    void cleanupSwapchain()
    {
        for (VkFramebuffer framebuffer : framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        framebuffers.clear();
        if (depthImageView) {
            vkDestroyImageView(device, depthImageView, nullptr);
        }
        if (depthImage) {
            vkDestroyImage(device, depthImage, nullptr);
        }
        if (depthMemory) {
            vkFreeMemory(device, depthMemory, nullptr);
        }
        depthImageView = VK_NULL_HANDLE;
        depthImage = VK_NULL_HANDLE;
        depthMemory = VK_NULL_HANDLE;
        if (trianglePipeline) {
            vkDestroyPipeline(device, trianglePipeline, nullptr);
        }
        if (linePipeline) {
            vkDestroyPipeline(device, linePipeline, nullptr);
        }
        if (mirrorMaskPipeline) {
            vkDestroyPipeline(device, mirrorMaskPipeline, nullptr);
        }
        if (reflectedTrianglePipeline) {
            vkDestroyPipeline(device, reflectedTrianglePipeline, nullptr);
        }
        if (reflectedLinePipeline) {
            vkDestroyPipeline(device, reflectedLinePipeline, nullptr);
        }
        trianglePipeline = VK_NULL_HANDLE;
        linePipeline = VK_NULL_HANDLE;
        mirrorMaskPipeline = VK_NULL_HANDLE;
        reflectedTrianglePipeline = VK_NULL_HANDLE;
        reflectedLinePipeline = VK_NULL_HANDLE;
        for (VkImageView view : swapchainImageViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        swapchainImageViews.clear();
        if (swapchain) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        swapchain = VK_NULL_HANDLE;
    }

    void recreateSwapchain()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width == 0 || height == 0) {
            return;
        }
        vkDeviceWaitIdle(device);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createPipelines();
        createDepthResources();
        createFramebuffers();
    }

    void cleanup()
    {
        if (device) {
            vkDeviceWaitIdle(device);
            cleanupSwapchain();
            if (vertexBuffer) {
                vkDestroyBuffer(device, vertexBuffer, nullptr);
                vkFreeMemory(device, vertexMemory, nullptr);
            }
            for (int i = 0; i < MaxFramesInFlight; ++i) {
                if (imageAvailableSemaphores.size() > size_t(i)) {
                    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
                    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
                    vkDestroyFence(device, inFlightFences[i], nullptr);
                }
            }
            if (commandPool) {
                vkDestroyCommandPool(device, commandPool, nullptr);
            }
            if (pipelineLayout) {
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            }
            if (renderPass) {
                vkDestroyRenderPass(device, renderPass, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (surface) {
            SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        }
        if (instance) {
            vkDestroyInstance(instance, nullptr);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
};

} // namespace

int main()
{
    try {
        VulkanApp app;
        app.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
