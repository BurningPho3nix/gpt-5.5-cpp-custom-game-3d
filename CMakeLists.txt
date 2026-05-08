cmake_minimum_required(VERSION 3.21)

project(VulkanWorldShooter
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
pkg_check_modules(VULKAN REQUIRED vulkan)
pkg_check_modules(SHADERC REQUIRED shaderc)

add_executable(vulkan-world-shooter
    src/main.cpp
    src/game.hpp
    src/math.hpp
    src/shaders.hpp
    src/types.hpp
    src/vulkan_app.hpp
)

target_include_directories(vulkan-world-shooter
    PRIVATE
        ${VULKAN_INCLUDE_DIRS}
        ${SHADERC_INCLUDE_DIRS}
)

target_compile_options(vulkan-world-shooter
    PRIVATE
        ${VULKAN_CFLAGS_OTHER}
        ${SHADERC_CFLAGS_OTHER}
)

target_link_libraries(vulkan-world-shooter
    PRIVATE
        SDL3::SDL3
        ${VULKAN_LIBRARIES}
        ${SHADERC_LIBRARIES}
)
