#include "game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace vws {

Game::if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured)
{
            const float sensitivity = 0.12f * (1.0f - scopeAmount * 0.55f);
            yaw += event.motion.xrel * sensitivity;
            pitch = clamp(pitch - event.motion.yrel * sensitivity, -82.0f, 82.0f);
        }

} else Game::if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
{
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseCaptured = true;
                fireMouseDown = true;
                tryShoot();
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                mouseCaptured = true;
                scoped = true;
            }
        }

} else Game::if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
{
            if (event.button.button == SDL_BUTTON_LEFT) {
                fireMouseDown = false;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                scoped = false;
            }
        }

} else Game::if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
{
            if (event.key.scancode == SDL_SCANCODE_SPACE) {
                jumpQueued = true;
            } else if (event.key.scancode == SDL_SCANCODE_R) {
                reset();
            } else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                mouseCaptured = false;
                scoped = false;
            }
        }

Game::if (leftPressed && !fireMouseDown)
{
            mouseCaptured = true;
            tryShoot();
        }

Game::if (keys[SDL_SCANCODE_W])
{
            movement += forwardVector();
        }

Game::if (keys[SDL_SCANCODE_S])
{
            movement -= forwardVector();
        }

Game::if (keys[SDL_SCANCODE_D])
{
            movement += rightVector();
        }

Game::if (keys[SDL_SCANCODE_A])
{
            movement -= rightVector();
        }

Game::if (movement.lengthSquared() > 0.0001f)
{
            movement = movement.normalized();
            const float speed = (sprinting ? 14.0f : 8.5f) * (1.0f - crouchAmount * 0.42f) * (1.0f - scopeAmount * 0.28f);
            moveWithCollision(movement * speed * deltaSeconds);
        }

Game::if (jumpQueued && grounded && crouchAmount < 0.65f)
{
            verticalVelocity = 8.8f;
            grounded = false;
        }

Game::if (grounded)
{
            const float heightError = floor - position.y;
            verticalVelocity += heightError * 38.0f * deltaSeconds;
            verticalVelocity *= std::pow(0.015f, deltaSeconds);
        }

Game::if (position.y < floor)
{
            position.y = floor;
            verticalVelocity = 0.0f;
            grounded = true;
        }

} else Game::if (position.y > floor + 0.08f)
{
            grounded = false;
        }

Game::if (position.y > MaxClimbHeight)
{
            position.y = MaxClimbHeight;
            verticalVelocity = std::min(0.0f, verticalVelocity);
        }

Game::for (BulletMark &mark : bulletMarks)
{
            mark.age += deltaSeconds;
        }

Game::for (auto bullet = flyingBullets.begin(); bullet != flyingBullets.end();)
{
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

Game::if (fpsTimer >= 0.35f)
{
            currentFps = float(fpsFrames) / fpsTimer;
            fpsFrames = 0;
            fpsTimer = 0.0f;
        }

Mat4::Game::lookAt(position, position + forwardVector(),
{0.0f, 1.0f, 0.0f}

Game::for (const Vec3 &p : corners)
{
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

Game::if (!anyVisible || maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f)
{
            return false;
        }

Game::if (x1 <= x0 || y1Screen <= y0Screen)
{
            return false;
        }

Vec3 Game::cameraPosition() const
{ return position; }

float Game::getWorldTime() const
{ return worldTime; }

Game::if (pitch < -12.0f)
{
            addPlayerModel(triangles, position, yaw, pitch, crouchAmount, false, true);
        }

Game::if (scopeAmount > 0.02f)
{
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

Game::for (int i = 0; i < 9; ++i)
{
            addBox(-14.0f + i * 3.5f, 0.35f + i * 0.45f, 48.0f - i * 4.0f, 6.0f, 0.7f + i * 0.25f, 4.0f, {0.60f, 0.52f, 0.40f});
        }

Game::for (int i = 0; i < 12; ++i)
{
            addBox(-45.0f, 0.3f + i * 0.55f, 50.0f - i * 4.2f, 16.0f, 0.6f + i * 0.22f, 3.8f, {0.48f, 0.54f, 0.48f});
        }

Game::for (int i = 0; i < 10; ++i)
{
            addBox(46.0f, 0.35f + i * 0.75f, 42.0f - i * 4.0f, 14.0f, 0.7f + i * 0.28f, 3.6f, {0.56f, 0.50f, 0.46f});
        }

Game::for (int i = 0; i < 70 && addedAnimals < 18; ++i)
{
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

return Game::cross(forwardVector(),
{0.0f, 1.0f, 0.0f}

Game::for (const WorldBox &box : worldBoxes)
{
            const Vec3 half = box.size * 0.5f;
            if (x >= box.center.x - half.x && x <= box.center.x + half.x &&
                z >= box.center.z - half.z && z <= box.center.z + half.z) {
                const float top = box.center.y + half.y;
                if (top <= MaxClimbHeight) {
                    height = std::max(height, top);
                }
            }
        }

Game::if (std::abs(x) > WorldHalfSize - 7.0f || std::abs(z) > WorldHalfSize - 7.0f)
{
            return true;
        }

Game::if (circleOverlapsAabb(x, z, radius + 0.8f, mirrorCenter, mirrorSize))
{
            return true;
        }

Game::for (const WorldBox &box : worldBoxes)
{
            if (circleOverlapsAabb(x, z, radius + 1.2f, box.center, box.size)) {
                return true;
            }
        }

Game::while (degrees > 180.0f)
{
            degrees -= 360.0f;
        }

Game::while (degrees < -180.0f)
{
            degrees += 360.0f;
        }

Game::for (size_t i = 0; i < animals.size(); ++i)
{
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

Game::circleOverlapsAabb(x, z, playerRadius, mirrorCenter, mirrorSize))
{
            return true;
        }

Game::for (int i = 0; i < 34; ++i)
{
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

Game::for (const Animal &animal : animals)
{
            const float animalHeight = (animal.kind == 2 ? 0.95f : 1.75f) * animal.scale;
            const float animalRadius = (animal.kind == 0 ? 1.22f : animal.kind == 1 ? 1.08f : 0.52f) * animal.scale;
            const float dx = x - animal.position.x;
            const float dz = z - animal.position.z;
            if (verticalOverlap(animal.position.y, animal.position.y + animalHeight) &&
                dx * dx + dz * dz < (playerRadius + animalRadius) * (playerRadius + animalRadius)) {
                return true;
            }
        }

Game::if (!playerOverlapsObjectAt(candidate.x, candidate.z))
{
            position.x = candidate.x;
        }

Game::if (!playerOverlapsObjectAt(candidate.x, candidate.z))
{
            position.z = candidate.z;
        }

!Game::testAxis(origin.z, direction.z, minCorner.z, maxCorner.z, {0.0f, 0.0f, 1.0f}))
{
            return false;
        }

Game::for (float t = 0.5f; t < best.distance; t += 1.0f)
{
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

Game::for (const WorldBox &box : worldBoxes)
{
            float distance = 0.0f;
            Vec3 normal;
            if (intersectRayBox(origin, direction, box, &distance, &normal) && distance < best.distance) {
                best = {origin + direction * distance, normal, distance, true};
            }
        }

Game::if (intersectRayBox(origin, direction, mirrorBox, &mirrorDistance, &mirrorNormal) && mirrorDistance < best.distance)
{
            best = {origin + direction * mirrorDistance, mirrorNormal, mirrorDistance, true};
        }

Game::for (int i = 0; i < 34; ++i)
{
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

Game::for (size_t animalIndex = 0; animalIndex < animals.size(); ++animalIndex)
{
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

Game::if (fireCooldown > 0.0f)
{
            return;
        }

const Vec3 start = position + Game::rightVector() * 0.18f + direction * 0.45f + Vec3
{0.0f, -0.10f, 0.0f}

Game::if (hit.hit)
{
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
        }

Game::for (size_t i = startIndex; i < vertices.size(); ++i)
{
            vertices[i].position.z = MirrorFaceZ * 2.0f - vertices[i].position.z;
            vertices[i].normal.z = -vertices[i].normal.z;
        }

Game::for (Vertex &v : local)
{
            v.position = rotateY(v.position, yawDegrees) + center;
            v.normal = rotateY(v.normal, yawDegrees);
            triangles.push_back(v);
        }

Game::if (length < 0.001f)
{
            return;
        }

const Vec3 reference = std::Game::abs(forward.y) > 0.92f ? Vec3
{1.0f, 0.0f, 0.0f}

const Vec3 reference = std::Game::abs(forward.y) > 0.92f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3
{0.0f, 1.0f, 0.0f}

Game::for (int z = 0; z < cells; ++z)
{
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
                const float lush = clamp(((a.y + b.y + c.y + d.y) * 0.25f + 1.5f) / 7.5f, 0.0f, 1.0f);
                const float patch = deterministic01(x * 97 + z * 193);
                const float fine = 0.5f + 0.5f * std::sin(x0 * 0.31f + z0 * 0.27f);
                const float shade = 0.82f + patch * 0.18f + fine * 0.06f;
                // Richer, more natural terrain colors
                const Vec3 base{0.10f + lush * 0.08f, 0.28f + lush * 0.28f, 0.08f + lush * 0.08f};
                const Vec3 color{base.x * shade, base.y * shade, base.z * shade};
                const Vec3 color2 = color * (0.88f + patch * 0.12f);
                addTriangleWithNormals(triangles, a, b, c, color, na, nb, nc);
                addTriangleWithNormals(triangles, a, c, d, color2, na, nc, nd);
            }
        }

Game::if (animal.kind == 0)
{
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
                segment({-0.58f, 0.48f, side * 0.20f}, {-0.68f + counterStride, 0.08f + footLift, side * 0.22f}, 0.13f, hideDark);
                segment({0.52f, 0.48f, side * 0.20f}, {0.60f + stride, 0.08f + footLift, side * 0.22f}, 0.13f, hideDark);
            }
            segment({-0.95f, 0.90f, 0.0f}, {-1.22f, 0.98f + tailWag * 0.08f, 0.0f}, 0.12f, cream);
        }

} else Game::if (animal.kind == 1)
{
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
                segment({-0.54f, 0.28f, side * 0.24f}, {-0.56f + counterStride * 0.75f, 0.04f + footLift * 0.65f, side * 0.24f}, 0.12f, furDark);
                segment({0.46f, 0.28f, side * 0.24f}, {0.48f + stride * 0.75f, 0.04f + footLift * 0.65f, side * 0.24f}, 0.12f, furDark);
            }
            line({-0.92f, 0.68f, 0.0f}, {-1.12f, 0.72f + tailWag * 0.08f, 0.0f}, furDark);
        }

Game::for (float side : std::array{-1.0f, 1.0f})
{
                segment({-0.24f, 0.10f, side * 0.12f}, {-0.42f + counterStride, 0.02f + footLift, side * 0.16f}, 0.08f, furDark);
                segment({0.28f, 0.10f, side * 0.12f}, {0.44f + stride, 0.02f + footLift, side * 0.16f}, 0.08f, furDark);
            }

Game::for (const Animal &animal : animals)
{
            addAnimalModel(triangles, lines, animal, worldTime);
        }

Game::for (int i = 0; i < 520; ++i)
{
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

Game::for (int i = 0; i < 34; ++i)
{
            const float x = -130.0f + deterministic01(i * 41 + 2) * 260.0f;
            const float z = -132.0f + deterministic01(i * 47 + 8) * 264.0f;
            if ((std::abs(x) < 24.0f && z > 36.0f && z < 76.0f) || std::abs(z - MirrorZ) < 7.0f) {
                continue;
            }
            const float y = terrainHeightAt(x, z) + 0.10f;
            const float trunkHeight = 1.7f + deterministic01(i * 19) * 1.6f;
            // Warmer, richer trunk brown
            addBox(triangles, {x, y + trunkHeight * 0.5f, z}, {0.55f, trunkHeight, 0.55f}, {0.34f, 0.22f, 0.12f});
            // Root flare — darker
            addBox(triangles, {x, y + 0.08f, z}, {1.10f, 0.16f, 1.10f}, {0.24f, 0.15f, 0.08f});
            // Foliage — richer, more varied greens
            const float foliageVariation = deterministic01(i + 3) * 0.18f;
            addBox(triangles, {x, y + trunkHeight + 0.75f, z}, {3.0f, 1.6f, 3.0f}, {0.08f + foliageVariation * 0.3f, 0.32f + foliageVariation, 0.10f + foliageVariation * 0.2f});
            addBox(triangles, {x, y + trunkHeight + 1.65f, z}, {2.1f, 1.3f, 2.1f}, {0.06f, 0.26f + foliageVariation * 0.5f, 0.09f});
        }

Game::for (int i = 0; i < 46; ++i)
{
            const float x = -140.0f + deterministic01(i * 53 + 1) * 280.0f;
            const float z = -140.0f + deterministic01(i * 59 + 4) * 280.0f;
            const float y = terrainHeightAt(x, z) + 0.18f;
            const float scale = 0.45f + deterministic01(i * 23) * 1.25f;
            // Rocks with subtle warm/cool color variation
            const float rockTone = deterministic01(i * 31 + 5) * 0.08f;
            addBox(triangles, {x, y, z}, {scale * 1.4f, scale * 0.55f, scale}, {0.32f + rockTone, 0.30f + rockTone * 0.5f, 0.28f - rockTone * 0.5f});
        }

Game::for (const WorldBox &box : worldBoxes)
{
            addBox(triangles, box.center, box.size, box.color);
            const Vec3 half = box.size * 0.5f;
            const float y = box.center.y + half.y + 0.03f;
            const Vec3 c{0.90f, 0.96f, 0.90f};
            addLine(lines, {box.center.x - half.x, y, box.center.z - half.z}, {box.center.x + half.x, y, box.center.z - half.z}, c);
            addLine(lines, {box.center.x + half.x, y, box.center.z - half.z}, {box.center.x + half.x, y, box.center.z + half.z}, c);
            addLine(lines, {box.center.x + half.x, y, box.center.z + half.z}, {box.center.x - half.x, y, box.center.z + half.z}, c);
            addLine(lines, {box.center.x - half.x, y, box.center.z + half.z}, {box.center.x - half.x, y, box.center.z - half.z}, c);
        }

Game::if (includeHead)
{
            box({0.0f, 1.72f - headDrop, -0.20f}, {0.42f, 0.42f, 0.36f}, skin);
            box({0.0f, 1.98f - headDrop, -0.21f}, {0.48f, 0.16f, 0.40f}, black);
        }

Game::if (includeWeapon)
{
            const Vec3 gunDark{0.055f, 0.065f, 0.075f};
            const Vec3 gunBody{0.13f, 0.16f, 0.18f};
            const Vec3 gunMetal{0.34f, 0.38f, 0.40f};
            const float pitchPush = -pitchDegrees * 0.004f;
            const Vec3 scopedWeaponOffset = weaponAimOffset * aimLift;
            box(Vec3{0.0f, 1.04f + pitchPush, -0.72f} + scopedWeaponOffset, {0.42f, 0.28f, 0.58f}, gunBody);
            box(Vec3{0.0f, 1.03f + pitchPush, -1.16f} + scopedWeaponOffset, {0.18f, 0.18f, 0.70f}, gunDark);
            box(Vec3{0.0f, 1.20f + pitchPush, -0.88f} + scopedWeaponOffset, {0.24f, 0.08f, 0.48f}, gunMetal);
            box(Vec3{-0.12f, 1.27f + pitchPush, -0.79f} + scopedWeaponOffset, {0.06f, 0.18f, 0.08f}, gunMetal);
            box(Vec3{0.12f, 1.27f + pitchPush, -0.79f} + scopedWeaponOffset, {0.06f, 0.18f, 0.08f}, gunMetal);
            box(Vec3{-0.12f, 1.27f + pitchPush, -0.97f} + scopedWeaponOffset, {0.06f, 0.18f, 0.08f}, gunMetal);
            box(Vec3{0.12f, 1.27f + pitchPush, -0.97f} + scopedWeaponOffset, {0.06f, 0.18f, 0.08f}, gunMetal);
            box(Vec3{0.0f, 1.34f + pitchPush, -0.88f} + scopedWeaponOffset, {0.23f, 0.17f, 0.18f}, gunDark);
            box(Vec3{0.0f, 1.34f + pitchPush, -1.02f} + scopedWeaponOffset, {0.12f, 0.09f, 0.16f}, cyan);
            box(Vec3{0.0f, 0.83f + pitchPush, -0.56f} + scopedWeaponOffset, {0.20f, 0.44f, 0.18f}, gunDark);
            box(Vec3{0.0f, 1.03f + pitchPush, -1.58f} + scopedWeaponOffset, {0.28f, 0.24f, 0.18f}, gunMetal);
            box(Vec3{0.0f, 1.03f + pitchPush, -1.70f} + scopedWeaponOffset, {0.16f, 0.16f, 0.08f}, shotFlash > 0.0f ? Vec3{1.0f, 0.70f, 0.16f} : Vec3{0.82f, 0.42f, 0.10f});
        }

Game::addBox(triangles, {-mirrorWidth * 0.5f - 0.28f, (y0 + y1) * 0.5f, MirrorZ + 0.18f},
{0.56f, mirrorHeight + 0.56f, 0.48f}

Game::addBox(triangles, {mirrorWidth * 0.5f + 0.28f, (y0 + y1) * 0.5f, MirrorZ + 0.18f},
{0.56f, mirrorHeight + 0.56f, 0.48f}

Game::for (const FlyingBullet &bullet : flyingBullets)
{
            const Vec3 direction = (bullet.target - bullet.start).normalized();
            const float visibleTravel = std::min(bullet.traveled, bullet.distance);
            const Vec3 p = bullet.start + direction * visibleTravel;
            const Vec3 trailStart = bullet.start + direction * std::max(0.0f, visibleTravel - 2.8f);
            addLine(lines, trailStart, p, {1.0f, 0.76f, 0.20f});
            addBox(triangles, p, {0.11f, 0.11f, 0.11f}, {1.0f, 0.92f, 0.34f});
        }

Game::if (tangent.lengthSquared() < 0.01f)
{
            tangent = cross(normal, {1.0f, 0.0f, 0.0f});
        }

Game::if (tangent.lengthSquared() < 0.01f)
{
            tangent = cross(surfaceNormal, {1.0f, 0.0f, 0.0f});
        }

Game::for (const BulletMark &mark : bulletMarks)
{
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
            addDecalQuad(triangles, position, normal, 0.20f, {0.08f + glow * 0.92f, 0.02f + glow * 0.22f, 0.015f});
        }

} // namespace vws
