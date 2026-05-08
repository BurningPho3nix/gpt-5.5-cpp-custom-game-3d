#pragma once

#include "math.hpp"

namespace vws {

struct Vertex {
    Vec3 position;
    Vec3 color;
    Vec3 normal;
};

struct WorldBox {
    Vec3 center;
    Vec3 size;
    Vec3 color;
    bool shootable = true;
};

struct Animal {
    Vec3 position;
    float yaw = 0.0f;
    float targetYaw = 0.0f;
    float scale = 1.0f;
    int kind = 0;
    float phase = 0.0f;
    float speed = 1.0f;
    float turnTimer = 0.0f;
};

struct BulletMark {
    Vec3 position;
    Vec3 normal;
    float age = 0.0f;
    int animalIndex = -1;
    Vec3 animalLocalPosition;
    Vec3 animalLocalNormal;
};

struct FlyingBullet {
    Vec3 start;
    Vec3 target;
    Vec3 normal;
    float distance = 0.0f;
    float traveled = 0.0f;
    bool willHit = false;
    int animalIndex = -1;
    Vec3 animalLocalPosition;
    Vec3 animalLocalNormal;
};

struct SurfaceHit {
    Vec3 position;
    Vec3 normal;
    float distance = 0.0f;
    bool hit = false;
    int animalIndex = -1;
    Vec3 animalLocalPosition;
    Vec3 animalLocalNormal;
};

} // namespace vws
