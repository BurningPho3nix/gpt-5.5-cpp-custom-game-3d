#pragma once

#include <shaderc/shaderc.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace vws {

inline std::vector<uint32_t> compileShader(const char *source, shaderc_shader_kind kind, const char *name)
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

inline constexpr const char *VertexShader = R"(
#version 450
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 cameraPos;   // xyz = camera position, w = unused
    vec4 fogColor;    // xyz = fog/sky color, w = worldTime
    vec4 reflectionClip; // x = mirror plane z, y = kept side, z = enabled
} pc;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragNormal;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragWorldPos = inPosition;
    fragNormal = inNormal;
}
)";

inline constexpr const char *FragmentShader = R"(
#version 450
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 cameraPos;
    vec4 fogColor;
    vec4 reflectionClip;
} pc;
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;
void main() {
    if (pc.reflectionClip.z > 0.5 && (fragWorldPos.z - pc.reflectionClip.x) * pc.reflectionClip.y < -0.01) {
        discard;
    }

    vec3 N = fragNormal;
    float normalLen = length(N);

    // If normal is zero-length (lines, overlay), pass color through without lighting
    if (normalLen < 0.01) {
        outColor = vec4(fragColor, 1.0);
        return;
    }

    N = normalize(N);

    // Directional sunlight — warm golden light from upper-right
    vec3 sunDir = normalize(vec3(0.48, 0.74, 0.32));
    vec3 sunColor = vec3(1.12, 0.96, 0.78);
    float NdotL = max(dot(N, sunDir), 0.0);
    vec3 diffuse = sunColor * NdotL * 0.72;

    // Hemisphere ambient: warm from above, cool from below
    vec3 skyAmbient = vec3(0.32, 0.40, 0.55);   // cool blue-grey from sky
    vec3 groundAmbient = vec3(0.12, 0.10, 0.06); // warm earth from below
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi) * 0.62;

    // Secondary fill light from opposite side (subtle)
    vec3 fillDir = normalize(vec3(-0.3, 0.2, -0.5));
    vec3 fillColor = vec3(0.18, 0.22, 0.32);
    float fillNdotL = max(dot(N, fillDir), 0.0);
    vec3 fill = fillColor * fillNdotL * 0.35;

    // Blinn-Phong specular
    vec3 viewDir = normalize(pc.cameraPos.xyz - fragWorldPos);
    vec3 halfDir = normalize(sunDir + viewDir);
    float spec = pow(max(dot(N, halfDir), 0.0), 32.0);
    vec3 specular = sunColor * spec * 0.18;

    // Combine lighting
    vec3 litColor = fragColor * (ambient + diffuse + fill) + specular;

    // Distance fog — blend toward sky color
    float dist = length(fragWorldPos - pc.cameraPos.xyz);
    float fogStart = 55.0;
    float fogEnd = 210.0;
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogFactor = fogFactor * fogFactor;  // Quadratic falloff for softer look
    vec3 finalColor = mix(litColor, pc.fogColor.xyz, fogFactor);

    // Tone mapping — subtle, prevents harsh whites
    finalColor = finalColor / (finalColor + vec3(0.8));
    finalColor = pow(finalColor, vec3(1.0/1.8));  // Gamma-like brightening

    outColor = vec4(finalColor, 1.0);
}
)";

} // namespace vws
