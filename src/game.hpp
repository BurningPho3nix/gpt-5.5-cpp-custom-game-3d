#pragma once

#include "types.hpp"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vws {

inline float terrainNoise(float x, float z)
{
    const float rolling = std::sin(x * 0.045f) * 1.6f + std::cos(z * 0.038f) * 1.4f;
    const float ridges = std::sin((x + z) * 0.022f) * 1.2f + std::cos((x - z) * 0.031f) * 0.9f;
    const float detail = std::sin(x * 0.13f + z * 0.07f) * 0.32f;
    const float spawnFlatten = std::exp(-(x * x + (z - 70.0f) * (z - 70.0f)) / 2200.0f);
    return (rolling + ridges + detail + 2.4f) * (1.0f - spawnFlatten * 0.75f);
}

inline float deterministic01(int value)
{
    uint32_t x = static_cast<uint32_t>(value) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return float(x & 1023u) / 1023.0f;
}

class Game {
public:
    Game()
    {
        buildWorld();
        buildStaticMeshes();
        reset();
    }

    void handleEvent(const SDL_Event &event)
    {
        if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured) {
            const float sensitivity = 0.12f * (1.0f - scopeAmount * 0.55f);
            yaw += event.motion.xrel * sensitivity;
            pitch = clamp(pitch - event.motion.yrel * sensitivity, -82.0f, 82.0f);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseCaptured = true;
                fireMouseDown = true;
                tryShoot();
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                mouseCaptured = true;
                scoped = true;
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                fireMouseDown = false;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
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
        fireCooldown = std::max(0.0f, fireCooldown - deltaSeconds);
        const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(nullptr, nullptr);
        const bool leftPressed = (mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
        if (leftPressed && !fireMouseDown) {
            mouseCaptured = true;
            tryShoot();
        }
        fireMouseDown = leftPressed;

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
            moveWithCollision(movement * speed * deltaSeconds);
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

        worldTime += deltaSeconds;
        updateAnimals(deltaSeconds);
        shotFlash = std::max(0.0f, shotFlash - deltaSeconds * 5.0f);
        for (BulletMark &mark : bulletMarks) {
            mark.age += deltaSeconds;
        }

        constexpr float bulletSpeed = 95.0f;
        for (auto bullet = flyingBullets.begin(); bullet != flyingBullets.end();) {
            bullet->traveled += bulletSpeed * deltaSeconds;
            if (bullet->traveled >= bullet->distance) {
                if (bullet->willHit) {
                    bulletMarks.push_back({bullet->target,
                                           bullet->normal,
                                           0.0f,
                                           bullet->animalIndex,
                                           bullet->animalLocalPosition,
                                           bullet->animalLocalNormal});
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

    float reflectionClipSide() const
    {
        return position.z >= MirrorFaceZ ? -1.0f : 1.0f;
    }

    bool mirrorScreenRect(int targetWidth, int targetHeight, float aspect, VkRect2D *rect) const
    {
        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const float y0 = mirrorGround + 0.55f;
        const float y1 = mirrorGround + mirrorHeight;
        const float z = MirrorFaceZ;
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
        const std::string title = "Wildlands | FPS " + std::to_string(int(currentFps)) +
            " | Pos " + std::to_string(int(position.x)) + ", " + std::to_string(int(position.y)) + ", " + std::to_string(int(position.z)) +
            " | Shots " + std::to_string(totalShots) + " | Hits " + std::to_string(bulletMarks.size());
        SDL_SetWindowTitle(window, title.c_str());
    }

    Vec3 cameraPosition() const { return position; }
    float getWorldTime() const { return worldTime; }

    void buildWorldMeshes(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        triangles = staticWorldTriangles;
        lines = staticWorldLines;
        addAnimals(triangles, lines);
        if (pitch < -12.0f) {
            addPlayerModel(triangles, position, yaw, pitch, crouchAmount, false, true);
        }
        addFlyingBullets(triangles, lines);
        addBulletMarks(triangles);
    }

    void buildReflectionMeshes(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        triangles = staticReflectionTriangles;
        lines = staticReflectionLines;
        const size_t dynamicTriangleStart = triangles.size();
        const size_t dynamicLineStart = lines.size();
        addAnimals(triangles, lines);
        addPlayerModel(triangles, position, yaw, pitch, crouchAmount, true, true);
        addFlyingBullets(triangles, lines);
        mirrorVerticesAcrossMirror(triangles, dynamicTriangleStart);
        mirrorVerticesAcrossMirror(lines, dynamicLineStart);
    }

    void buildMirrorMeshes(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        triangles = staticMirrorTriangles;
        lines = staticMirrorLines;
    }

    void buildMirrorMask(std::vector<Vertex> &triangles) const
    {
        triangles = staticMirrorMaskTriangles;
    }

    void buildOverlay(std::vector<Vertex> &triangles, std::vector<Vertex> &lines, float aspect) const
    {
        triangles.clear();
        lines.clear();

        const Vec3 crosshair = shotFlash > 0.0f ? Vec3{1.0f, 0.70f, 0.24f} : Vec3{0.82f, 0.94f, 0.90f};
        const float gap = scopeAmount > 0.5f ? 0.013f : 0.020f;
        const float length = scopeAmount > 0.5f ? 0.025f : 0.040f;
        addDisc2D(triangles, {0.0f, 0.0f, 0.0f}, 0.0038f, aspect, crosshair, 20);
        addLine2D(lines, {-length / aspect, 0.0f, 0.0f}, {-gap / aspect, 0.0f, 0.0f}, crosshair);
        addLine2D(lines, {gap / aspect, 0.0f, 0.0f}, {length / aspect, 0.0f, 0.0f}, crosshair);
        addLine2D(lines, {0.0f, -length, 0.0f}, {0.0f, -gap, 0.0f}, crosshair);
        addLine2D(lines, {0.0f, gap, 0.0f}, {0.0f, length, 0.0f}, crosshair);

        if (scopeAmount > 0.02f) {
            const float radius = 0.68f + scopeAmount * 0.06f;
            const Vec3 scopeColor{0.34f, 0.78f, 0.72f};
            addAnnulus2D(triangles, radius, 0.0065f, aspect, scopeColor, 128);
            addLine2D(lines, {-radius / aspect, 0.0f, 0.0f}, {radius / aspect, 0.0f, 0.0f}, scopeColor);
            addLine2D(lines, {0.0f, -radius, 0.0f}, {0.0f, radius, 0.0f}, scopeColor);
        }
    }

private:
    std::vector<WorldBox> worldBoxes;
    std::vector<Animal> animals;
    std::vector<FlyingBullet> flyingBullets;
    std::vector<BulletMark> bulletMarks;
    std::vector<Vertex> staticWorldTriangles;
    std::vector<Vertex> staticWorldLines;
    std::vector<Vertex> staticReflectionTriangles;
    std::vector<Vertex> staticReflectionLines;
    std::vector<Vertex> staticMirrorMaskTriangles;
    std::vector<Vertex> staticMirrorTriangles;
    std::vector<Vertex> staticMirrorLines;
    Vec3 position{0.0f, 1.7f, 74.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float verticalVelocity = 0.0f;
    float shotFlash = 0.0f;
    float fpsTimer = 0.0f;
    float currentFps = 0.0f;
    float walkCycle = 0.0f;
    float worldTime = 0.0f;
    float crouchAmount = 0.0f;
    float scopeAmount = 0.0f;
    float fireCooldown = 0.0f;
    int totalShots = 0;
    int fpsFrames = 0;
    bool walking = false;
    bool grounded = true;
    bool jumpQueued = false;
    bool mouseCaptured = false;
    bool fireMouseDown = false;
    bool scoped = false;

    void buildWorld()
    {
        auto addBox = [&](float x, float y, float z, float sx, float sy, float sz, const Vec3 &color) {
            worldBoxes.push_back({{x, y, z}, {sx, sy, sz}, color, true});
        };

        addBox(0.0f, 7.5f, -96.0f, 78.0f, 15.0f, 3.0f, {0.62f, 0.60f, 0.56f});  // long wall — warm concrete
        addBox(-54.0f, 5.5f, -56.0f, 28.0f, 11.0f, 26.0f, {0.52f, 0.50f, 0.48f}); // left building — grey stone
        addBox(54.0f, 8.0f, -46.0f, 25.0f, 16.0f, 25.0f, {0.58f, 0.46f, 0.38f});  // right building — sandstone
        addBox(0.0f, 3.0f, -28.0f, 34.0f, 6.0f, 22.0f, {0.45f, 0.43f, 0.42f});    // central platform — dark concrete
        addBox(-28.0f, 9.0f, -14.0f, 10.0f, 18.0f, 10.0f, {0.68f, 0.56f, 0.40f}); // left tower — warm brick
        addBox(28.0f, 12.0f, -10.0f, 9.0f, 24.0f, 9.0f, {0.44f, 0.48f, 0.58f});   // right tower — blue-grey
        addBox(0.0f, 0.6f, 18.0f, 56.0f, 1.2f, 20.0f, {0.38f, 0.40f, 0.38f});     // ground slab — mossy stone
        addBox(-78.0f, 3.0f, 12.0f, 20.0f, 6.0f, 48.0f, {0.50f, 0.52f, 0.46f});   // left bunker — olive concrete
        addBox(82.0f, 4.0f, 22.0f, 22.0f, 8.0f, 42.0f, {0.54f, 0.48f, 0.52f});    // right bunker — mauve stone
        for (int i = 0; i < 9; ++i) {
            addBox(-14.0f + i * 3.5f, 0.35f + i * 0.45f, 48.0f - i * 4.0f, 6.0f, 0.7f + i * 0.25f, 4.0f, {0.60f, 0.52f, 0.40f});
        }
        for (int i = 0; i < 12; ++i) {
            addBox(-45.0f, 0.3f + i * 0.55f, 50.0f - i * 4.2f, 16.0f, 0.6f + i * 0.22f, 3.8f, {0.48f, 0.54f, 0.48f});
        }
        for (int i = 0; i < 10; ++i) {
            addBox(46.0f, 0.35f + i * 0.75f, 42.0f - i * 4.0f, 14.0f, 0.7f + i * 0.28f, 3.6f, {0.56f, 0.50f, 0.46f});
        }
        addBox(-118.0f, 5.0f, 0.0f, 4.0f, 10.0f, 220.0f, {0.38f, 0.36f, 0.34f});  // boundary walls — dark stone
        addBox(118.0f, 5.0f, 0.0f, 4.0f, 10.0f, 220.0f, {0.38f, 0.36f, 0.34f});
        addBox(0.0f, 5.0f, -118.0f, 220.0f, 10.0f, 4.0f, {0.38f, 0.36f, 0.34f});
        addBox(0.0f, 5.0f, 118.0f, 220.0f, 10.0f, 4.0f, {0.38f, 0.36f, 0.34f});

        auto addAnimal = [&](float x, float z, int kind, float scale, float phase, float speed) {
            const float initialYaw = phase / Pi * 180.0f;
            animals.push_back({{x, terrainHeightAt(x, z) + 0.04f, z},
                               initialYaw,
                               initialYaw,
                               scale,
                               kind,
                               phase,
                               speed,
                               0.4f + phase});
        };

        addAnimal(-18.0f, 62.0f, 0, 1.02f, 0.30f, 0.55f);
        addAnimal(20.0f, 44.0f, 1, 0.92f, 2.10f, 0.45f);
        addAnimal(-9.0f, 34.0f, 2, 0.56f, 4.00f, 0.75f);

        int addedAnimals = 0;
        for (int i = 0; i < 70 && addedAnimals < 18; ++i) {
            const float x = -112.0f + deterministic01(i * 71 + 13) * 224.0f;
            const float z = -108.0f + deterministic01(i * 83 + 27) * 216.0f;
            if ((std::abs(x) < 28.0f && z > 34.0f && z < 82.0f) || std::abs(z - MirrorZ) < 9.0f) {
                continue;
            }

            bool nearStructure = false;
            for (const WorldBox &box : worldBoxes) {
                if (circleOverlapsAabb(x, z, 4.8f, box.center, box.size)) {
                    nearStructure = true;
                    break;
                }
            }
            if (nearStructure) {
                continue;
            }

            const int kind = addedAnimals % 3;
            const float scale = kind == 0
                ? 0.92f + deterministic01(i * 19 + 2) * 0.22f
                : kind == 1
                    ? 0.76f + deterministic01(i * 23 + 5) * 0.22f
                    : 0.48f + deterministic01(i * 29 + 7) * 0.16f;
            addAnimal(x, z, kind, scale, deterministic01(i * 37 + 11) * Pi * 2.0f, 0.30f + deterministic01(i * 43 + 17) * 0.35f);
            ++addedAnimals;
        }
    }

    void buildStaticMeshes()
    {
        staticWorldTriangles.clear();
        staticWorldLines.clear();
        addGround(staticWorldTriangles, staticWorldLines);
        addNature(staticWorldTriangles, staticWorldLines);
        addWorldGeometry(staticWorldTriangles, staticWorldLines);

        staticReflectionTriangles = staticWorldTriangles;
        staticReflectionLines = staticWorldLines;
        mirrorVerticesAcrossMirror(staticReflectionTriangles);
        mirrorVerticesAcrossMirror(staticReflectionLines);

        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const float y0 = mirrorGround + 0.55f;
        const float y1 = mirrorGround + mirrorHeight;
        const float z = MirrorFaceZ;
        const Vec3 color{1.0f, 1.0f, 1.0f};
        addTriangle(staticMirrorMaskTriangles, {-mirrorWidth * 0.5f, y0, z}, {mirrorWidth * 0.5f, y0, z}, {mirrorWidth * 0.5f, y1, z}, color);
        addTriangle(staticMirrorMaskTriangles, {-mirrorWidth * 0.5f, y0, z}, {mirrorWidth * 0.5f, y1, z}, {-mirrorWidth * 0.5f, y1, z}, color);

        addMirror(staticMirrorTriangles, staticMirrorLines);
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
        fireCooldown = 0.0f;
        totalShots = 0;
        walking = false;
        grounded = true;
        jumpQueued = false;
        fireMouseDown = false;
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

    static bool circleOverlapsAabb(float circleX, float circleZ, float radius, const Vec3 &center, const Vec3 &size)
    {
        const float minX = center.x - size.x * 0.5f;
        const float maxX = center.x + size.x * 0.5f;
        const float minZ = center.z - size.z * 0.5f;
        const float maxZ = center.z + size.z * 0.5f;
        const float closestX = clamp(circleX, minX, maxX);
        const float closestZ = clamp(circleZ, minZ, maxZ);
        const float dx = circleX - closestX;
        const float dz = circleZ - closestZ;
        return dx * dx + dz * dz < radius * radius;
    }

    bool animalPositionBlocked(float x, float z, float radius) const
    {
        if (std::abs(x) > WorldHalfSize - 7.0f || std::abs(z) > WorldHalfSize - 7.0f) {
            return true;
        }

        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const Vec3 mirrorCenter{0.0f, mirrorGround + 4.1f, MirrorZ + 0.18f};
        const Vec3 mirrorSize{27.4f, 9.4f, 0.90f};
        if (circleOverlapsAabb(x, z, radius + 0.8f, mirrorCenter, mirrorSize)) {
            return true;
        }

        for (const WorldBox &box : worldBoxes) {
            if (circleOverlapsAabb(x, z, radius + 1.2f, box.center, box.size)) {
                return true;
            }
        }
        return false;
    }

    static float wrapDegrees(float degrees)
    {
        while (degrees > 180.0f) {
            degrees -= 360.0f;
        }
        while (degrees < -180.0f) {
            degrees += 360.0f;
        }
        return degrees;
    }

    static float yawForDirection(float dx, float dz)
    {
        return std::atan2(-dz, dx) / DegToRad;
    }

    void updateAnimals(float deltaSeconds)
    {
        for (size_t i = 0; i < animals.size(); ++i) {
            Animal &animal = animals[i];
            const float radius = (animal.kind == 0 ? 1.22f : animal.kind == 1 ? 1.08f : 0.52f) * animal.scale;
            const int seedBase = int(worldTime * 100.0f) + int(i) * 997;

            animal.turnTimer -= deltaSeconds;
            if (animal.turnTimer <= 0.0f) {
                const float turn = (deterministic01(seedBase + 11) - 0.5f) * 150.0f;
                animal.targetYaw = wrapDegrees(animal.yaw + turn);
                animal.turnTimer = 0.55f + deterministic01(seedBase + 23) * 2.25f;
            }

            const float edge = WorldHalfSize - 18.0f;
            if (std::abs(animal.position.x) > edge || std::abs(animal.position.z) > edge) {
                animal.targetYaw = yawForDirection(-animal.position.x, -animal.position.z);
                animal.turnTimer = std::min(animal.turnTimer, 0.45f);
            }

            const float yawDelta = wrapDegrees(animal.targetYaw - animal.yaw);
            animal.yaw = wrapDegrees(animal.yaw + clamp(yawDelta, -95.0f * deltaSeconds, 95.0f * deltaSeconds));

            const float yawRad = animal.yaw * DegToRad;
            const Vec3 direction{std::cos(yawRad), 0.0f, -std::sin(yawRad)};
            const float moveSpeed = (animal.kind == 2 ? 1.6f : animal.kind == 1 ? 0.9f : 1.2f) * animal.speed;
            Vec3 candidate = animal.position + direction * moveSpeed * deltaSeconds;

            if (animalPositionBlocked(candidate.x, candidate.z, radius)) {
                const float turn = 105.0f + deterministic01(seedBase + 37) * 120.0f;
                animal.targetYaw = wrapDegrees(animal.yaw + turn);
                animal.yaw = animal.targetYaw;
                animal.turnTimer = 0.35f + deterministic01(seedBase + 41) * 0.8f;
                const float newYawRad = animal.yaw * DegToRad;
                const Vec3 newDirection{std::cos(newYawRad), 0.0f, -std::sin(newYawRad)};
                candidate = animal.position + newDirection * moveSpeed * deltaSeconds;
            }

            if (!animalPositionBlocked(candidate.x, candidate.z, radius)) {
                animal.position.x = clamp(candidate.x, -WorldHalfSize + 7.0f, WorldHalfSize - 7.0f);
                animal.position.z = clamp(candidate.z, -WorldHalfSize + 7.0f, WorldHalfSize - 7.0f);
                animal.position.y = terrainHeightAt(animal.position.x, animal.position.z) + 0.04f;
            }
        }
    }

    bool playerOverlapsObjectAt(float x, float z) const
    {
        constexpr float playerRadius = 0.62f;
        const float playerBottom = position.y - currentEyeHeight();
        const float playerTop = position.y + 0.18f;

        auto verticalOverlap = [&](float bottom, float top) {
            return playerTop > bottom && playerBottom < top;
        };

        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const Vec3 mirrorCenter{0.0f, mirrorGround + mirrorHeight * 0.5f, MirrorZ + 0.18f};
        const Vec3 mirrorSize{mirrorWidth + 1.4f, mirrorHeight + 1.2f, 0.90f};
        if (verticalOverlap(mirrorGround, mirrorGround + mirrorHeight + 1.0f) &&
            circleOverlapsAabb(x, z, playerRadius, mirrorCenter, mirrorSize)) {
            return true;
        }

        for (int i = 0; i < 34; ++i) {
            const float treeX = -130.0f + deterministic01(i * 41 + 2) * 260.0f;
            const float treeZ = -132.0f + deterministic01(i * 47 + 8) * 264.0f;
            if ((std::abs(treeX) < 24.0f && treeZ > 36.0f && treeZ < 76.0f) || std::abs(treeZ - MirrorZ) < 7.0f) {
                continue;
            }
            const float treeBottom = terrainHeightAt(treeX, treeZ);
            const float trunkHeight = 1.7f + deterministic01(i * 19) * 1.6f;
            const float dx = x - treeX;
            const float dz = z - treeZ;
            constexpr float treeRadius = 0.92f;
            if (verticalOverlap(treeBottom, treeBottom + trunkHeight + 0.4f) &&
                dx * dx + dz * dz < (playerRadius + treeRadius) * (playerRadius + treeRadius)) {
                return true;
            }
        }

        for (const Animal &animal : animals) {
            const float animalHeight = (animal.kind == 2 ? 0.95f : 1.75f) * animal.scale;
            const float animalRadius = (animal.kind == 0 ? 1.22f : animal.kind == 1 ? 1.08f : 0.52f) * animal.scale;
            const float dx = x - animal.position.x;
            const float dz = z - animal.position.z;
            if (verticalOverlap(animal.position.y, animal.position.y + animalHeight) &&
                dx * dx + dz * dz < (playerRadius + animalRadius) * (playerRadius + animalRadius)) {
                return true;
            }
        }

        return false;
    }

    void moveWithCollision(const Vec3 &delta)
    {
        Vec3 candidate = position;
        candidate.x = clamp(candidate.x + delta.x, -WorldHalfSize + 2.0f, WorldHalfSize - 2.0f);
        if (!playerOverlapsObjectAt(candidate.x, candidate.z)) {
            position.x = candidate.x;
        }

        candidate = position;
        candidate.z = clamp(candidate.z + delta.z, -WorldHalfSize + 2.0f, WorldHalfSize - 2.0f);
        if (!playerOverlapsObjectAt(candidate.x, candidate.z)) {
            position.z = candidate.z;
        }
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

        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const WorldBox mirrorBox{
            {0.0f, mirrorGround + 4.1f, MirrorZ + 0.18f},
            {27.4f, 9.4f, 0.90f},
            {0.05f, 0.06f, 0.08f},
            true,
        };
        float mirrorDistance = 0.0f;
        Vec3 mirrorNormal;
        if (intersectRayBox(origin, direction, mirrorBox, &mirrorDistance, &mirrorNormal) && mirrorDistance < best.distance) {
            best = {origin + direction * mirrorDistance, mirrorNormal, mirrorDistance, true};
        }

        for (int i = 0; i < 34; ++i) {
            const float treeX = -130.0f + deterministic01(i * 41 + 2) * 260.0f;
            const float treeZ = -132.0f + deterministic01(i * 47 + 8) * 264.0f;
            if ((std::abs(treeX) < 24.0f && treeZ > 36.0f && treeZ < 76.0f) || std::abs(treeZ - MirrorZ) < 7.0f) {
                continue;
            }
            const float treeY = terrainHeightAt(treeX, treeZ) + 0.10f;
            const float trunkHeight = 1.7f + deterministic01(i * 19) * 1.6f;
            const WorldBox trunkBox{
                {treeX, treeY + trunkHeight * 0.5f, treeZ},
                {0.82f, trunkHeight, 0.82f},
                {0.25f, 0.16f, 0.09f},
                true,
            };
            float treeDistance = 0.0f;
            Vec3 treeNormal;
            if (intersectRayBox(origin, direction, trunkBox, &treeDistance, &treeNormal) && treeDistance < best.distance) {
                best = {origin + direction * treeDistance, treeNormal, treeDistance, true};
            }
        }

        for (size_t animalIndex = 0; animalIndex < animals.size(); ++animalIndex) {
            const Animal &animal = animals[animalIndex];
            const Vec3 bodyLocalCenter = animal.kind == 0
                ? Vec3{0.0f, 0.82f, 0.0f}
                : animal.kind == 1
                    ? Vec3{0.0f, 0.56f, 0.0f}
                    : Vec3{0.0f, 0.28f, 0.0f};
            const Vec3 bodySize = animal.kind == 0
                ? Vec3{1.70f, 0.72f, 0.58f} * animal.scale
                : animal.kind == 1
                    ? Vec3{1.72f, 0.72f, 0.72f} * animal.scale
                    : Vec3{1.05f, 0.44f, 0.44f} * animal.scale;
            const Vec3 bodyCenter = animal.position + rotateY(bodyLocalCenter * animal.scale, animal.yaw);
            const WorldBox bodyBox{bodyCenter, bodySize, {0.55f, 0.25f, 0.16f}, true};

            auto localSurfaceHit = [](const Vec3 &localHit, const Vec3 &localCenter, const Vec3 &size, Vec3 *localNormal) {
                const Vec3 half = size * 0.5f;
                Vec3 surface{
                    clamp(localHit.x, localCenter.x - half.x, localCenter.x + half.x),
                    clamp(localHit.y, localCenter.y - half.y, localCenter.y + half.y),
                    clamp(localHit.z, localCenter.z - half.z, localCenter.z + half.z),
                };
                const float dxMin = std::abs(localHit.x - (localCenter.x - half.x));
                const float dxMax = std::abs(localHit.x - (localCenter.x + half.x));
                const float dyMin = std::abs(localHit.y - (localCenter.y - half.y));
                const float dyMax = std::abs(localHit.y - (localCenter.y + half.y));
                const float dzMin = std::abs(localHit.z - (localCenter.z - half.z));
                const float dzMax = std::abs(localHit.z - (localCenter.z + half.z));
                float bestFace = dxMin;
                int face = 0;
                auto testFace = [&](float distance, int candidateFace) {
                    if (distance < bestFace) {
                        bestFace = distance;
                        face = candidateFace;
                    }
                };
                testFace(dxMax, 1);
                testFace(dyMin, 2);
                testFace(dyMax, 3);
                testFace(dzMin, 4);
                testFace(dzMax, 5);
                if (face == 0) {
                    surface.x = localCenter.x - half.x;
                    *localNormal = {-1.0f, 0.0f, 0.0f};
                } else if (face == 1) {
                    surface.x = localCenter.x + half.x;
                    *localNormal = {1.0f, 0.0f, 0.0f};
                } else if (face == 2) {
                    surface.y = localCenter.y - half.y;
                    *localNormal = {0.0f, -1.0f, 0.0f};
                } else if (face == 3) {
                    surface.y = localCenter.y + half.y;
                    *localNormal = {0.0f, 1.0f, 0.0f};
                } else if (face == 4) {
                    surface.z = localCenter.z - half.z;
                    *localNormal = {0.0f, 0.0f, -1.0f};
                } else {
                    surface.z = localCenter.z + half.z;
                    *localNormal = {0.0f, 0.0f, 1.0f};
                }
                return surface;
            };

            float animalDistance = 0.0f;
            Vec3 animalNormal;
            if (intersectRayBox(origin, direction, bodyBox, &animalDistance, &animalNormal) && animalDistance < best.distance) {
                const Vec3 hitPosition = origin + direction * animalDistance;
                const Vec3 localHit = rotateY(hitPosition - animal.position, -animal.yaw);
                Vec3 localNormal;
                const Vec3 localPosition = localSurfaceHit(localHit, bodyLocalCenter * animal.scale, bodySize, &localNormal);
                best = {hitPosition,
                        rotateY(localNormal, animal.yaw).normalized(),
                        animalDistance,
                        true,
                        static_cast<int>(animalIndex),
                        localPosition,
                        localNormal};
            }

            const Vec3 headLocalCenter = animal.kind == 0
                ? Vec3{1.12f, 1.36f, -0.02f}
                : animal.kind == 1
                    ? Vec3{0.78f, 0.62f, 0.0f}
                    : Vec3{0.48f, 0.48f, 0.0f};
            const Vec3 headSize = animal.kind == 2
                ? Vec3{0.42f, 0.36f, 0.34f} * animal.scale
                : animal.kind == 1
                    ? Vec3{0.62f, 0.50f, 0.48f} * animal.scale
                    : Vec3{0.58f, 0.40f, 0.34f} * animal.scale;
            const WorldBox headBox{animal.position + rotateY(headLocalCenter * animal.scale, animal.yaw),
                                   headSize,
                                   {0.55f, 0.25f, 0.16f},
                                   true};
            if (intersectRayBox(origin, direction, headBox, &animalDistance, &animalNormal) && animalDistance < best.distance) {
                const Vec3 hitPosition = origin + direction * animalDistance;
                const Vec3 localHit = rotateY(hitPosition - animal.position, -animal.yaw);
                Vec3 localNormal;
                const Vec3 localPosition = localSurfaceHit(localHit, headLocalCenter * animal.scale, headSize, &localNormal);
                best = {hitPosition,
                        rotateY(localNormal, animal.yaw).normalized(),
                        animalDistance,
                        true,
                        static_cast<int>(animalIndex),
                        localPosition,
                        localNormal};
            }
        }
        return best;
    }

    void tryShoot()
    {
        if (fireCooldown > 0.0f) {
            return;
        }
        fireCooldown = 0.16f;
        shoot();
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
            flyingBullets.push_back({start,
                                     hit.position,
                                     hit.normal,
                                     distance,
                                     0.0f,
                                     true,
                                     hit.animalIndex,
                                     hit.animalLocalPosition,
                                     hit.animalLocalNormal});
        } else {
            flyingBullets.push_back({start, start + direction * 180.0f, {0.0f, 1.0f, 0.0f}, 180.0f, 0.0f, false});
        }
    }

    static void mirrorVerticesAcrossMirror(std::vector<Vertex> &vertices, size_t startIndex = 0)
    {
        for (size_t i = startIndex; i < vertices.size(); ++i) {
            vertices[i].position.z = MirrorFaceZ * 2.0f - vertices[i].position.z;
            vertices[i].normal.z = -vertices[i].normal.z;
        }
    }

    static void addVertex(std::vector<Vertex> &vertices, const Vec3 &position, const Vec3 &color, const Vec3 &normal = {0.0f, 1.0f, 0.0f})
    {
        vertices.push_back({position, color, normal});
    }

    static void addTriangle(std::vector<Vertex> &triangles, const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &color)
    {
        const Vec3 normal = cross(b - a, c - a).normalized();
        addVertex(triangles, a, color, normal);
        addVertex(triangles, b, color, normal);
        addVertex(triangles, c, color, normal);
    }

    static void addTriangleWithNormals(std::vector<Vertex> &triangles, const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &color, const Vec3 &na, const Vec3 &nb, const Vec3 &nc)
    {
        addVertex(triangles, a, color, na);
        addVertex(triangles, b, color, nb);
        addVertex(triangles, c, color, nc);
    }

    static void addTriangleSmooth(std::vector<Vertex> &triangles,
                                  const Vec3 &a, const Vec3 &b, const Vec3 &c,
                                  const Vec3 &ca, const Vec3 &cb, const Vec3 &cc,
                                  const Vec3 &na, const Vec3 &nb, const Vec3 &nc)
    {
        addVertex(triangles, a, ca, na);
        addVertex(triangles, b, cb, nb);
        addVertex(triangles, c, cc, nc);
    }

    static void addLine(std::vector<Vertex> &lines, const Vec3 &a, const Vec3 &b, const Vec3 &color)
    {
        addVertex(lines, a, color, {0.0f, 0.0f, 0.0f});
        addVertex(lines, b, color, {0.0f, 0.0f, 0.0f});
    }

    static void addLine2D(std::vector<Vertex> &lines, const Vec3 &a, const Vec3 &b, const Vec3 &color)
    {
        addLine(lines, {a.x, a.y, 0.0f}, {b.x, b.y, 0.0f}, color);
    }

    static void addDisc2D(std::vector<Vertex> &triangles, const Vec3 &center, float radius, float aspect, const Vec3 &color, int segments)
    {
        for (int i = 0; i < segments; ++i) {
            const float a0 = float(i) / float(segments) * Pi * 2.0f;
            const float a1 = float(i + 1) / float(segments) * Pi * 2.0f;
            addVertex(triangles, center, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, {center.x + std::cos(a0) * radius / aspect, center.y + std::sin(a0) * radius, 0.0f}, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, {center.x + std::cos(a1) * radius / aspect, center.y + std::sin(a1) * radius, 0.0f}, color, {0.0f, 0.0f, 0.0f});
        }
    }

    static void addAnnulus2D(std::vector<Vertex> &triangles, float radius, float thickness, float aspect, const Vec3 &color, int segments)
    {
        const float inner = std::max(0.0f, radius - thickness);
        const float outer = radius + thickness;
        for (int i = 0; i < segments; ++i) {
            const float a0 = float(i) / float(segments) * Pi * 2.0f;
            const float a1 = float(i + 1) / float(segments) * Pi * 2.0f;
            const Vec3 i0{std::cos(a0) * inner / aspect, std::sin(a0) * inner, 0.0f};
            const Vec3 o0{std::cos(a0) * outer / aspect, std::sin(a0) * outer, 0.0f};
            const Vec3 i1{std::cos(a1) * inner / aspect, std::sin(a1) * inner, 0.0f};
            const Vec3 o1{std::cos(a1) * outer / aspect, std::sin(a1) * outer, 0.0f};
            addVertex(triangles, i0, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, o0, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, o1, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, i0, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, o1, color, {0.0f, 0.0f, 0.0f});
            addVertex(triangles, i1, color, {0.0f, 0.0f, 0.0f});
        }
    }

    static void addBox(std::vector<Vertex> &triangles, const Vec3 &center, const Vec3 &size, const Vec3 &color)
    {
        // A compact bevelled-box mesh. Collision remains the original AABB, while
        // the visible mesh gets broad planar faces, curved edges and rounded corners.
        const Vec3 half = size * 0.5f;
        const float radius = std::max(0.015f, std::min({half.x, half.y, half.z}) * 0.34f);
        const Vec3 inner{
            std::max(0.0f, half.x - radius),
            std::max(0.0f, half.y - radius),
            std::max(0.0f, half.z - radius),
        };

        auto roundedPoint = [&](const Vec3 &cubePoint) {
            const Vec3 nearest{
                clamp(cubePoint.x, -inner.x, inner.x),
                clamp(cubePoint.y, -inner.y, inner.y),
                clamp(cubePoint.z, -inner.z, inner.z),
            };
            Vec3 outward = cubePoint - nearest;
            if (outward.lengthSquared() < 0.000001f) {
                outward = {0.0f, 1.0f, 0.0f};
            }
            const Vec3 normal = outward.normalized();
            return std::pair<Vec3, Vec3>{center + nearest + normal * radius, normal};
        };

        constexpr int bevelSegments = 3;
        auto face = [&](int axis, float sign, bool reverse) {
            for (int row = 0; row < bevelSegments; ++row) {
                for (int col = 0; col < bevelSegments; ++col) {
                    const float u0 = -1.0f + 2.0f * float(col) / float(bevelSegments);
                    const float u1 = -1.0f + 2.0f * float(col + 1) / float(bevelSegments);
                    const float v0 = -1.0f + 2.0f * float(row) / float(bevelSegments);
                    const float v1 = -1.0f + 2.0f * float(row + 1) / float(bevelSegments);
                    auto cube = [&](float u, float v) {
                        if (axis == 0) return Vec3{sign * half.x, v * half.y, u * half.z};
                        if (axis == 1) return Vec3{u * half.x, sign * half.y, v * half.z};
                        return Vec3{u * half.x, v * half.y, sign * half.z};
                    };
                    auto p0 = roundedPoint(cube(u0, v0));
                    auto p1 = roundedPoint(cube(u1, v0));
                    auto p2 = roundedPoint(cube(u1, v1));
                    auto p3 = roundedPoint(cube(u0, v1));
                    if (reverse) {
                        std::swap(p1, p3);
                    }
                    addTriangleWithNormals(triangles, p0.first, p1.first, p2.first, color, p0.second, p1.second, p2.second);
                    addTriangleWithNormals(triangles, p0.first, p2.first, p3.first, color, p0.second, p2.second, p3.second);
                }
            }
        };
        face(0, 1.0f, true);
        face(0, -1.0f, false);
        face(1, 1.0f, false);
        face(1, -1.0f, true);
        face(2, 1.0f, false);
        face(2, -1.0f, true);
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
            v.normal = rotateY(v.normal, yawDegrees);
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
        const Vec3 forward = delta / length;
        const Vec3 reference = std::abs(forward.y) > 0.92f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
        const Vec3 right = cross(reference, forward).normalized();
        const Vec3 up = cross(forward, right).normalized();
        const float halfThickness = thickness * 0.5f;
        // Smooth octagonal limb/branch with softly capped ends.
        constexpr int sides = 10;
        for (int i = 0; i < sides; ++i) {
            const float a0 = float(i) / float(sides) * Pi * 2.0f;
            const float a1 = float(i + 1) / float(sides) * Pi * 2.0f;
            const Vec3 n0 = right * std::cos(a0) + up * std::sin(a0);
            const Vec3 n1 = right * std::cos(a1) + up * std::sin(a1);
            const Vec3 s0 = start + n0 * halfThickness;
            const Vec3 s1 = start + n1 * halfThickness;
            const Vec3 e0 = end + n0 * halfThickness;
            const Vec3 e1 = end + n1 * halfThickness;
            addTriangleWithNormals(triangles, s0, e0, e1, color, n0, n0, n1);
            addTriangleWithNormals(triangles, s0, e1, s1, color, n0, n1, n1);
            addTriangleWithNormals(triangles, start - forward * halfThickness * 0.28f, s1, s0, color, -forward, -forward, -forward);
            addTriangleWithNormals(triangles, end + forward * halfThickness * 0.28f, e0, e1, color, forward, forward, forward);
        }
    }

    static void addEllipsoid(std::vector<Vertex> &triangles, const Vec3 &center, const Vec3 &size, const Vec3 &color, float yawDegrees = 0.0f, int rings = 5, int sides = 10)
    {
        const Vec3 radius = size * 0.5f;
        auto sample = [&](float latitude, float longitude) {
            const Vec3 unit{
                std::cos(latitude) * std::cos(longitude),
                std::sin(latitude),
                std::cos(latitude) * std::sin(longitude),
            };
            Vec3 local{unit.x * radius.x, unit.y * radius.y, unit.z * radius.z};
            Vec3 normal{
                unit.x / std::max(radius.x, 0.001f),
                unit.y / std::max(radius.y, 0.001f),
                unit.z / std::max(radius.z, 0.001f),
            };
            normal = normal.normalized();
            return std::pair<Vec3, Vec3>{center + rotateY(local, yawDegrees), rotateY(normal, yawDegrees)};
        };
        for (int ring = 0; ring < rings; ++ring) {
            const float lat0 = -Pi * 0.5f + Pi * float(ring) / float(rings);
            const float lat1 = -Pi * 0.5f + Pi * float(ring + 1) / float(rings);
            for (int side = 0; side < sides; ++side) {
                const float lon0 = Pi * 2.0f * float(side) / float(sides);
                const float lon1 = Pi * 2.0f * float(side + 1) / float(sides);
                const auto p0 = sample(lat0, lon0);
                const auto p1 = sample(lat0, lon1);
                const auto p2 = sample(lat1, lon1);
                const auto p3 = sample(lat1, lon0);
                addTriangleWithNormals(triangles, p0.first, p2.first, p1.first, color, p0.second, p2.second, p1.second);
                addTriangleWithNormals(triangles, p0.first, p3.first, p2.first, color, p0.second, p3.second, p2.second);
            }
        }
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
                // Per-vertex terrain normals for smooth shading
                const Vec3 na = terrainNormalAt(x0, z0);
                const Vec3 nb = terrainNormalAt(x1, z0);
                const Vec3 nc = terrainNormalAt(x1, z1);
                const Vec3 nd = terrainNormalAt(x0, z1);
                auto terrainColor = [](const Vec3 &p) {
                    const float lush = clamp((p.y + 1.5f) / 7.5f, 0.0f, 1.0f);
                    const float broad = std::sin(p.x * 0.045f) * std::cos(p.z * 0.052f);
                    const float fine = std::sin(p.x * 0.19f + p.z * 0.14f) * 0.5f;
                    const float shade = 0.91f + broad * 0.055f + fine * 0.025f;
                    return Vec3{
                        (0.105f + lush * 0.065f) * shade,
                        (0.30f + lush * 0.22f) * shade,
                        (0.085f + lush * 0.065f) * shade,
                    };
                };
                const Vec3 ca = terrainColor(a);
                const Vec3 cb = terrainColor(b);
                const Vec3 cc = terrainColor(c);
                const Vec3 cd = terrainColor(d);
                addTriangleSmooth(triangles, a, b, c, ca, cb, cc, na, nb, nc);
                addTriangleSmooth(triangles, a, c, d, ca, cc, cd, na, nc, nd);
            }
        }
    }

    static void addAnimalModel(std::vector<Vertex> &triangles, std::vector<Vertex> &lines, const Animal &animal, float time)
    {
        const float runCycle = time * animal.speed * (animal.kind == 2 ? 11.0f : 7.5f) + animal.phase;
        const float stride = std::sin(runCycle) * (animal.kind == 2 ? 0.24f : 0.18f);
        const float counterStride = -stride;
        const float footLift = std::abs(std::sin(runCycle)) * (animal.kind == 2 ? 0.13f : 0.08f);
        const float runBounce = std::abs(std::sin(runCycle)) * (animal.kind == 2 ? 0.11f : 0.035f);
        const float bob = (std::sin(time * 1.7f + animal.phase) * 0.035f + runBounce) * animal.scale;
        const float headBob = std::sin(time * 2.1f + animal.phase * 1.3f) * 0.05f * animal.scale;
        const float tailWag = std::sin(time * 3.3f + animal.phase) * 0.22f;
        const float modelYaw = animal.yaw + std::sin(time * 0.37f + animal.phase) * 3.0f;

        auto tx = [&](Vec3 local) {
            local = local * animal.scale;
            local.y += bob;
            return rotateY(local, modelYaw) + animal.position;
        };
        auto box = [&](const Vec3 &local, const Vec3 &size, const Vec3 &color) {
            addEllipsoid(triangles, tx(local), size * animal.scale, color, modelYaw);
        };
        auto segment = [&](const Vec3 &a, const Vec3 &b, float thickness, const Vec3 &color) {
            addSegmentBox(triangles, tx(a), tx(b), thickness * animal.scale, color);
        };
        auto line = [&](const Vec3 &a, const Vec3 &b, const Vec3 &color) {
            addLine(lines, tx(a), tx(b), color);
        };

        if (animal.kind == 0) {
            const Vec3 hide{0.43f, 0.25f, 0.12f};
            const Vec3 hideDark{0.25f, 0.13f, 0.07f};
            const Vec3 cream{0.74f, 0.60f, 0.38f};
            box({0.0f, 0.82f, 0.0f}, {1.70f, 0.72f, 0.58f}, hide);
            box({-0.05f, 0.56f, 0.02f}, {1.25f, 0.20f, 0.50f}, cream);
            box({0.88f, 1.06f, -0.02f}, {0.28f, 0.72f, 0.24f}, hide);
            box({1.12f, 1.36f + headBob, -0.02f}, {0.58f, 0.40f, 0.34f}, hide);
            box({1.48f, 1.31f + headBob, -0.02f}, {0.28f, 0.20f, 0.24f}, hideDark);
            box({1.10f, 1.67f + headBob, -0.18f}, {0.13f, 0.32f, 0.10f}, hide);
            box({1.10f, 1.67f + headBob, 0.18f}, {0.13f, 0.32f, 0.10f}, hide);
            line({1.02f, 1.66f + headBob, -0.14f}, {0.92f, 2.05f + headBob, -0.28f}, cream);
            line({1.02f, 1.66f + headBob, 0.14f}, {0.92f, 2.05f + headBob, 0.28f}, cream);
            line({0.95f, 1.92f + headBob, -0.24f}, {0.72f, 2.08f + headBob, -0.40f}, cream);
            line({0.95f, 1.92f + headBob, 0.24f}, {0.72f, 2.08f + headBob, 0.40f}, cream);
            for (float side : std::array{-1.0f, 1.0f}) {
                segment({-0.58f, 0.80f, side * 0.18f}, {-0.68f + counterStride, 0.08f + footLift, side * 0.22f}, 0.16f, hideDark);
                segment({0.52f, 0.80f, side * 0.18f}, {0.60f + stride, 0.08f + footLift, side * 0.22f}, 0.16f, hideDark);
            }
            segment({-0.95f, 0.90f, 0.0f}, {-1.22f, 0.98f + tailWag * 0.08f, 0.0f}, 0.12f, cream);
        } else if (animal.kind == 1) {
            const Vec3 fur{0.26f, 0.19f, 0.14f};
            const Vec3 furDark{0.13f, 0.10f, 0.08f};
            const Vec3 tusk{0.86f, 0.80f, 0.62f};
            box({0.0f, 0.56f, 0.0f}, {1.72f, 0.72f, 0.72f}, fur);
            box({0.78f, 0.62f + headBob * 0.4f, 0.0f}, {0.62f, 0.50f, 0.48f}, fur);
            box({1.17f, 0.52f + headBob * 0.4f, 0.0f}, {0.30f, 0.22f, 0.34f}, furDark);
            box({0.60f, 0.98f + headBob * 0.4f, -0.24f}, {0.14f, 0.22f, 0.10f}, furDark);
            box({0.60f, 0.98f + headBob * 0.4f, 0.24f}, {0.14f, 0.22f, 0.10f}, furDark);
            line({1.22f, 0.45f, -0.14f}, {1.42f, 0.54f, -0.28f}, tusk);
            line({1.22f, 0.45f, 0.14f}, {1.42f, 0.54f, 0.28f}, tusk);
            for (float side : std::array{-1.0f, 1.0f}) {
                segment({-0.54f, 0.55f, side * 0.22f}, {-0.56f + counterStride * 0.75f, 0.04f + footLift * 0.65f, side * 0.24f}, 0.15f, furDark);
                segment({0.46f, 0.55f, side * 0.22f}, {0.48f + stride * 0.75f, 0.04f + footLift * 0.65f, side * 0.24f}, 0.15f, furDark);
            }
            line({-0.92f, 0.68f, 0.0f}, {-1.12f, 0.72f + tailWag * 0.08f, 0.0f}, furDark);
        } else {
            const Vec3 fur{0.62f, 0.53f, 0.42f};
            const Vec3 furDark{0.36f, 0.29f, 0.23f};
            const Vec3 white{0.84f, 0.78f, 0.68f};
            box({0.0f, 0.28f, 0.0f}, {1.05f, 0.44f, 0.44f}, fur);
            box({0.48f, 0.48f + headBob, 0.0f}, {0.42f, 0.36f, 0.34f}, fur);
            box({0.52f, 0.87f + headBob, -0.12f}, {0.12f, 0.52f, 0.08f}, fur);
            box({0.52f, 0.87f + headBob, 0.12f}, {0.12f, 0.52f, 0.08f}, fur);
            box({-0.58f, 0.38f, 0.0f}, {0.25f, 0.25f, 0.25f}, white);
            for (float side : std::array{-1.0f, 1.0f}) {
                segment({-0.24f, 0.27f, side * 0.10f}, {-0.42f + counterStride, 0.02f + footLift, side * 0.16f}, 0.10f, furDark);
                segment({0.28f, 0.27f, side * 0.10f}, {0.44f + stride, 0.02f + footLift, side * 0.16f}, 0.10f, furDark);
            }
        }
    }

    void addAnimals(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        for (const Animal &animal : animals) {
            addAnimalModel(triangles, lines, animal, worldTime);
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
            // Richer grass blade colors — more varied greens and yellowy-greens
            const float gr = 0.15f + deterministic01(i) * 0.12f;
            const float gg = 0.42f + deterministic01(i + 7) * 0.28f;
            const float gb = 0.12f + deterministic01(i + 3) * 0.08f;
            addLine(lines, {x, y, z}, {x + sway, y + blade, z + sway * 0.45f}, {gr, gg, gb});
        }

        for (int i = 0; i < 34; ++i) {
            const float x = -130.0f + deterministic01(i * 41 + 2) * 260.0f;
            const float z = -132.0f + deterministic01(i * 47 + 8) * 264.0f;
            if ((std::abs(x) < 24.0f && z > 36.0f && z < 76.0f) || std::abs(z - MirrorZ) < 7.0f) {
                continue;
            }
            const float y = terrainHeightAt(x, z) + 0.10f;
            const float trunkHeight = 1.7f + deterministic01(i * 19) * 1.6f;
            // Warmer, richer trunk brown
            addSegmentBox(triangles, {x, y, z}, {x, y + trunkHeight, z}, 0.58f, {0.34f, 0.22f, 0.12f});
            // Root flare — darker
            addEllipsoid(triangles, {x, y + 0.08f, z}, {1.10f, 0.22f, 1.10f}, {0.24f, 0.15f, 0.08f});
            // Foliage — richer, more varied greens
            const float foliageVariation = deterministic01(i + 3) * 0.18f;
            addEllipsoid(triangles, {x - 0.22f, y + trunkHeight + 0.72f, z}, {3.25f, 1.85f, 3.05f}, {0.08f + foliageVariation * 0.3f, 0.32f + foliageVariation, 0.10f + foliageVariation * 0.2f});
            addEllipsoid(triangles, {x + 0.32f, y + trunkHeight + 1.60f, z - 0.18f}, {2.35f, 1.55f, 2.25f}, {0.06f, 0.26f + foliageVariation * 0.5f, 0.09f});
        }

        for (int i = 0; i < 46; ++i) {
            const float x = -140.0f + deterministic01(i * 53 + 1) * 280.0f;
            const float z = -140.0f + deterministic01(i * 59 + 4) * 280.0f;
            const float y = terrainHeightAt(x, z) + 0.18f;
            const float scale = 0.45f + deterministic01(i * 23) * 1.25f;
            // Rocks with subtle warm/cool color variation
            const float rockTone = deterministic01(i * 31 + 5) * 0.08f;
            addEllipsoid(triangles, {x, y, z}, {scale * 1.4f, scale * 0.62f, scale}, {0.32f + rockTone, 0.30f + rockTone * 0.5f, 0.28f - rockTone * 0.5f}, deterministic01(i * 11) * 180.0f, 5, 10);
        }
    }

    void addWorldGeometry(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        for (const WorldBox &box : worldBoxes) {
            addBox(triangles, box.center, box.size, box.color);
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
            addEllipsoid(triangles, tx(local), size, color, modelYaw, 6, 12);
        };
        auto segment = [&](const Vec3 &a, const Vec3 &b, float thickness, const Vec3 &color) {
            addSegmentBox(triangles, tx(a), tx(b), thickness, color);
        };
        auto blend = [](const Vec3 &a, const Vec3 &b, float amount) {
            return a + (b - a) * amount;
        };

        const Vec3 suitDark{0.055f, 0.068f, 0.078f};
        const Vec3 suitMid{0.11f, 0.14f, 0.15f};
        const Vec3 suitLight{0.17f, 0.21f, 0.21f};
        const Vec3 armor{0.24f, 0.28f, 0.27f};
        const Vec3 skin{0.55f, 0.39f, 0.29f};
        const Vec3 black{0.035f, 0.040f, 0.050f};
        const Vec3 cyan{0.055f, 0.25f, 0.26f};

        box({0.0f, 1.18f - torsoDrop, -0.18f}, {0.72f, 0.54f, 0.34f}, suitMid);
        box({0.0f, 0.89f - hipDrop, -0.14f}, {0.56f, 0.38f, 0.30f}, suitMid);
        box({0.0f, 1.43f - torsoDrop, -0.20f}, {0.92f, 0.18f, 0.30f}, suitLight);
        box({0.0f, 1.30f - torsoDrop, -0.45f}, {0.18f, 0.12f, 0.05f}, cyan);

        const float aimLift = scopeAmount * scopeAmount * (3.0f - scopeAmount * 2.0f);
        const Vec3 weaponAimOffset{0.0f, 0.34f - crouch * 0.10f, 0.34f};

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
        const Vec3 leftElbow = blend({-0.47f, 1.10f - torsoDrop * 0.80f, -0.58f},
                                     {-0.43f, 1.31f - headDrop * 0.55f, -0.48f},
                                     aimLift);
        const Vec3 rightElbow = blend({0.46f, 1.04f - torsoDrop * 0.80f, -0.42f},
                                      {0.43f, 1.27f - headDrop * 0.55f, -0.40f},
                                      aimLift);
        const Vec3 leftHand = blend({-0.18f, 0.88f - torsoDrop * 0.62f, -1.05f},
                                    {-0.16f, 1.38f - headDrop * 0.62f, -0.60f},
                                    aimLift);
        const Vec3 rightHand = blend({0.15f, 0.76f - torsoDrop * 0.62f, -0.60f},
                                     {0.16f, 1.34f - headDrop * 0.62f, -0.34f},
                                     aimLift);
        segment(leftShoulder, leftElbow, 0.17f, suitLight);
        segment(leftElbow, leftHand, 0.15f, suitMid);
        segment(rightShoulder, rightElbow, 0.17f, suitLight);
        segment(rightElbow, rightHand, 0.15f, suitMid);
        box(leftShoulder, {0.24f, 0.22f, 0.24f}, suitLight);
        box(rightShoulder, {0.24f, 0.22f, 0.24f}, suitLight);
        box(leftHand, {0.18f, 0.15f, 0.20f}, skin);
        box(rightHand, {0.18f, 0.15f, 0.20f}, skin);

        if (includeHead) {
            segment({0.0f, 1.46f - headDrop, -0.18f}, {0.0f, 1.57f - headDrop, -0.19f}, 0.20f, skin * 0.78f);
            box({0.0f, 1.75f - headDrop, -0.20f}, {0.37f, 0.46f, 0.35f}, skin);
            box({0.0f, 1.96f - headDrop, -0.20f}, {0.40f, 0.15f, 0.36f}, black);
            const Vec3 eyeColor{0.025f, 0.030f, 0.026f};
            addEllipsoid(triangles, tx({-0.075f, 1.80f - headDrop, -0.372f}), {0.050f, 0.032f, 0.022f}, eyeColor, modelYaw, 4, 8);
            addEllipsoid(triangles, tx({0.075f, 1.80f - headDrop, -0.372f}), {0.050f, 0.032f, 0.022f}, eyeColor, modelYaw, 4, 8);
            addEllipsoid(triangles, tx({0.0f, 1.72f - headDrop, -0.387f}), {0.055f, 0.085f, 0.045f}, skin * 0.86f, modelYaw, 4, 8);
            addEllipsoid(triangles, tx({0.0f, 1.635f - headDrop, -0.374f}), {0.105f, 0.025f, 0.018f}, {0.20f, 0.075f, 0.055f}, modelYaw, 3, 8);
        }

        if (includeWeapon) {
            const Vec3 gunDark{0.055f, 0.065f, 0.075f};
            const Vec3 gunBody{0.13f, 0.16f, 0.18f};
            const Vec3 gunMetal{0.34f, 0.38f, 0.40f};
            const float pitchPush = -pitchDegrees * 0.004f;
            const Vec3 scopedWeaponOffset = weaponAimOffset * aimLift;
            auto gunPart = [&](const Vec3 &local, const Vec3 &size, const Vec3 &color) {
                addTransformedBox(triangles, tx(local), size, color, modelYaw);
            };
            gunPart(Vec3{0.0f, 1.04f + pitchPush, -0.72f} + scopedWeaponOffset, {0.42f, 0.28f, 0.58f}, gunBody);
            segment(Vec3{0.0f, 1.03f + pitchPush, -0.88f} + scopedWeaponOffset,
                    Vec3{0.0f, 1.03f + pitchPush, -1.54f} + scopedWeaponOffset, 0.18f, gunDark);
            gunPart(Vec3{0.0f, 1.19f + pitchPush, -0.90f} + scopedWeaponOffset, {0.28f, 0.08f, 0.55f}, gunMetal);
            gunPart(Vec3{0.0f, 1.27f + pitchPush, -0.76f} + scopedWeaponOffset, {0.10f, 0.18f, 0.09f}, gunDark);
            gunPart(Vec3{0.0f, 1.27f + pitchPush, -1.04f} + scopedWeaponOffset, {0.10f, 0.18f, 0.09f}, gunDark);
            segment(Vec3{0.0f, 1.36f + pitchPush, -0.68f} + scopedWeaponOffset,
                    Vec3{0.0f, 1.36f + pitchPush, -1.13f} + scopedWeaponOffset, 0.20f, gunDark);
            addEllipsoid(triangles, tx(Vec3{0.0f, 1.36f + pitchPush, -0.66f} + scopedWeaponOffset),
                         {0.25f, 0.25f, 0.16f}, gunMetal, modelYaw, 5, 12);
            addEllipsoid(triangles, tx(Vec3{0.0f, 1.36f + pitchPush, -1.15f} + scopedWeaponOffset),
                         {0.25f, 0.25f, 0.17f}, gunMetal, modelYaw, 5, 12);
            addEllipsoid(triangles, tx(Vec3{0.0f, 1.36f + pitchPush, -1.245f} + scopedWeaponOffset),
                         {0.17f, 0.17f, 0.035f}, cyan, modelYaw, 4, 12);
            gunPart(Vec3{0.0f, 0.83f + pitchPush, -0.56f} + scopedWeaponOffset, {0.20f, 0.44f, 0.18f}, gunDark);
            addEllipsoid(triangles, tx(Vec3{0.0f, 1.03f + pitchPush, -1.60f} + scopedWeaponOffset),
                         {0.27f, 0.23f, 0.24f}, gunMetal, modelYaw, 5, 10);
            addEllipsoid(triangles, tx(Vec3{0.0f, 1.03f + pitchPush, -1.72f} + scopedWeaponOffset),
                         {0.17f, 0.17f, 0.10f}, shotFlash > 0.0f ? Vec3{1.0f, 0.70f, 0.16f} : Vec3{0.055f, 0.050f, 0.045f}, modelYaw, 5, 10);
        }
    }

    void addMirror(std::vector<Vertex> &triangles, std::vector<Vertex> &lines) const
    {
        const float mirrorGround = terrainHeightAt(0.0f, MirrorZ);
        const float mirrorWidth = 26.0f;
        const float mirrorHeight = 8.2f;
        const float y0 = mirrorGround + 0.55f;
        const float y1 = mirrorGround + mirrorHeight;
        const float faceZ = MirrorFaceZ;
        const Vec3 frame{0.05f, 0.06f, 0.08f};

        addBox(triangles, {0.0f, y0 - 0.28f, MirrorZ + 0.18f}, {mirrorWidth + 1.2f, 0.56f, 0.48f}, frame);
        addBox(triangles, {0.0f, y1 + 0.28f, MirrorZ + 0.18f}, {mirrorWidth + 1.2f, 0.56f, 0.48f}, frame);
        addBox(triangles, {-mirrorWidth * 0.5f - 0.28f, (y0 + y1) * 0.5f, MirrorZ + 0.18f}, {0.56f, mirrorHeight + 0.56f, 0.48f}, frame);
        addBox(triangles, {mirrorWidth * 0.5f + 0.28f, (y0 + y1) * 0.5f, MirrorZ + 0.18f}, {0.56f, mirrorHeight + 0.56f, 0.48f}, frame);

        addLine(lines, {-mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {-mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
        addLine(lines, {-mirrorWidth * 0.5f, y1, faceZ + 0.02f}, {-mirrorWidth * 0.5f, y0, faceZ + 0.02f}, {0.92f, 0.98f, 1.0f});
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

    static void addDecalDisc(std::vector<Vertex> &triangles, const Vec3 &position, const Vec3 &normal, float radius, const Vec3 &color, int segments = 18)
    {
        Vec3 tangent = cross(normal, {0.0f, 1.0f, 0.0f});
        if (tangent.lengthSquared() < 0.01f) {
            tangent = cross(normal, {1.0f, 0.0f, 0.0f});
        }
        tangent = tangent.normalized();
        const Vec3 bitangent = cross(normal, tangent).normalized();
        const Vec3 p = position + normal * 0.018f;
        for (int i = 0; i < segments; ++i) {
            const float a0 = float(i) / float(segments) * Pi * 2.0f;
            const float a1 = float(i + 1) / float(segments) * Pi * 2.0f;
            const float irregular0 = radius * (0.88f + 0.12f * std::sin(float(i) * 5.73f));
            const float irregular1 = radius * (0.88f + 0.12f * std::sin(float(i + 1) * 5.73f));
            const Vec3 edge0 = p + tangent * (std::cos(a0) * irregular0) + bitangent * (std::sin(a0) * irregular0);
            const Vec3 edge1 = p + tangent * (std::cos(a1) * irregular1) + bitangent * (std::sin(a1) * irregular1);
            addVertex(triangles, p, color, normal);
            addVertex(triangles, edge0, color * 0.62f, normal);
            addVertex(triangles, edge1, color * 0.62f, normal);
        }
    }

    static void addBasisBox(std::vector<Vertex> &triangles, const Vec3 &center, const Vec3 &xAxis, const Vec3 &yAxis, const Vec3 &zAxis, const Vec3 &size, const Vec3 &color)
    {
        const Vec3 hx = xAxis * (size.x * 0.5f);
        const Vec3 hy = yAxis * (size.y * 0.5f);
        const Vec3 hz = zAxis * (size.z * 0.5f);
        const std::array<Vec3, 8> p = {{
            center - hx - hy - hz,
            center + hx - hy - hz,
            center + hx + hy - hz,
            center - hx + hy - hz,
            center - hx - hy + hz,
            center + hx - hy + hz,
            center + hx + hy + hz,
            center - hx + hy + hz,
        }};
        auto quadN = [&](int a, int b, int c, int d, const Vec3 &normal) {
            addVertex(triangles, p[a], color, normal);
            addVertex(triangles, p[b], color, normal);
            addVertex(triangles, p[c], color, normal);
            addVertex(triangles, p[a], color, normal);
            addVertex(triangles, p[c], color, normal);
            addVertex(triangles, p[d], color, normal);
        };
        quadN(4, 5, 6, 7, zAxis);      // +Z face
        quadN(1, 0, 3, 2, -zAxis);     // -Z face
        quadN(3, 7, 6, 2, yAxis);      // +Y face
        quadN(0, 1, 5, 4, -yAxis);     // -Y face
        quadN(0, 4, 7, 3, -xAxis);     // -X face
        quadN(5, 1, 2, 6, xAxis);      // +X face
    }

    static void addAnimalImpact(std::vector<Vertex> &triangles, const Vec3 &position, const Vec3 &normal, float glow)
    {
        const Vec3 surfaceNormal = normal.normalized();
        Vec3 tangent = cross(surfaceNormal, {0.0f, 1.0f, 0.0f});
        if (tangent.lengthSquared() < 0.01f) {
            tangent = cross(surfaceNormal, {1.0f, 0.0f, 0.0f});
        }
        tangent = tangent.normalized();
        const Vec3 bitangent = cross(surfaceNormal, tangent).normalized();
        const Vec3 darkBlood{0.18f + glow * 0.22f, 0.015f, 0.01f};
        const Vec3 freshBlood{0.42f + glow * 0.35f, 0.025f + glow * 0.04f, 0.015f};
        addDecalDisc(triangles, position, surfaceNormal, 0.13f, darkBlood, 18);
        addDecalDisc(triangles, position + tangent * 0.055f + bitangent * 0.018f, surfaceNormal, 0.055f, freshBlood, 14);
        addDecalDisc(triangles, position - tangent * 0.072f - bitangent * 0.028f, surfaceNormal, 0.036f, darkBlood * 0.72f, 12);
    }

    void addBulletMarks(std::vector<Vertex> &triangles) const
    {
        for (const BulletMark &mark : bulletMarks) {
            const float glow = clamp(1.0f - mark.age * 2.2f, 0.0f, 1.0f);
            Vec3 position = mark.position;
            Vec3 normal = mark.normal;
            if (mark.animalIndex >= 0 && mark.animalIndex < static_cast<int>(animals.size())) {
                const Animal &animal = animals[static_cast<size_t>(mark.animalIndex)];
                position = animal.position + rotateY(mark.animalLocalPosition, animal.yaw);
                normal = rotateY(mark.animalLocalNormal, animal.yaw).normalized();
                addAnimalImpact(triangles, position, normal, glow);
                continue;
            }
            const Vec3 hotCore{0.10f + glow * 0.78f, 0.028f + glow * 0.16f, 0.012f};
            addDecalDisc(triangles, position, normal, 0.115f, {0.055f, 0.045f, 0.035f}, 20);
            addDecalDisc(triangles, position + normal * 0.006f, normal, 0.052f, hotCore, 16);
        }
    }
};

} // namespace vws
