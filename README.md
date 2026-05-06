# Vulkan World Shooter

A Qt-free C++/Vulkan first-person 3D prototype derived from [`gpt-5.5-c--qt-game-3d`](https://github.com/BurningPho3nix/gpt-5.5-c--qt-game-3d).

It keeps the same core play space: a 300x300 meter procedural outdoor arena with rolling terrain, grass, trees, rocks, animals, block structures, stairs, a standing mirror, jumping, crouching, scoped aiming, flying bullets, and surface-aligned bullet marks. Windowing and input are handled with SDL3, and rendering is done with Vulkan.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/vulkan-world-shooter
```

## Dependencies

- CMake 3.21+
- C++20 compiler
- Vulkan loader and headers
- SDL3
- shaderc

## Controls

- `W`, `A`, `S`, `D`: move
- `Shift`: sprint
- `Space`: jump
- `Ctrl` or `C`: crouch
- Mouse: look around after clicking the game
- Left click: shoot
- Right click: scope
- `R`: reset position and bullet marks
- `Esc`: release the mouse cursor
