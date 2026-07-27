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

    // Soft late-afternoon sunlight. Rounded geometry reads through broad,
    // low-contrast highlights instead of the old hard polygon shading.
    vec3 sunDir = normalize(vec3(0.44, 0.78, 0.31));
    vec3 sunColor = vec3(1.10, 0.96, 0.82);
    float NdotL = max(dot(N, sunDir), 0.0);
    float wrappedDiffuse = clamp((NdotL + 0.16) / 1.16, 0.0, 1.0);
    vec3 diffuse = sunColor * wrappedDiffuse * 0.68;

    // Hemisphere ambient: warm from above, cool from below
    vec3 skyAmbient = vec3(0.34, 0.43, 0.56);
    vec3 groundAmbient = vec3(0.15, 0.12, 0.075);
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi) * 0.62;

    // Secondary fill light from opposite side (subtle)
    vec3 fillDir = normalize(vec3(-0.3, 0.2, -0.5));
    vec3 fillColor = vec3(0.20, 0.25, 0.34);
    float fillNdotL = max(dot(N, fillDir), 0.0);
    vec3 fill = fillColor * fillNdotL * 0.35;

    // Wide, restrained highlights resemble rough painted metal, foliage and stone.
    vec3 viewDir = normalize(pc.cameraPos.xyz - fragWorldPos);
    vec3 halfDir = normalize(sunDir + viewDir);
    float spec = pow(max(dot(N, halfDir), 0.0), 20.0);
    float fresnel = pow(1.0 - max(dot(N, viewDir), 0.0), 4.0);
    vec3 specular = sunColor * spec * 0.13 + pc.fogColor.xyz * fresnel * 0.035;

    // Combine lighting
    float surfaceVariation =
        sin(fragWorldPos.x * 6.7 + fragWorldPos.z * 2.1) *
        sin(fragWorldPos.y * 8.3 - fragWorldPos.z * 4.9);
    vec3 materialColor = fragColor * (1.0 + surfaceVariation * 0.022);
    vec3 litColor = materialColor * (ambient + diffuse + fill) + specular;

    // Distance fog — blend toward sky color
    float dist = length(fragWorldPos - pc.cameraPos.xyz);
    float fogStart = 62.0;
    float fogEnd = 205.0;
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogFactor = fogFactor * fogFactor;  // Quadratic falloff for softer look
    vec3 finalColor = mix(litColor, pc.fogColor.xyz, fogFactor);

    // Filmic tone curve with gently rolled highlights and natural saturation.
    finalColor *= 0.72;
    finalColor = finalColor * (2.51 * finalColor + 0.03) /
                 (finalColor * (2.43 * finalColor + 0.59) + 0.14);
    finalColor = pow(clamp(finalColor, 0.0, 1.0), vec3(1.0 / 2.2));
    float luminance = dot(finalColor, vec3(0.2126, 0.7152, 0.0722));
    finalColor = mix(vec3(luminance), finalColor, 1.12);
    finalColor = clamp((finalColor - 0.5) * 1.12 + 0.5, 0.0, 1.0);

    outColor = vec4(finalColor, 1.0);
}
)";

} // namespace vws
