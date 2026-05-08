#pragma once

#include "game.hpp"
#include "shaders.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace vws {

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

// Must match the push_constant layout in both shaders
struct PushConstantData {
    Mat4 mvp;                                      // 64 bytes
    float cameraPosX, cameraPosY, cameraPosZ, pad0; // 16 bytes (vec4)
    float fogR, fogG, fogB, worldTime;              // 16 bytes (vec4)
    float reflectionClipZ, reflectionClipSide, reflectionClipEnabled, pad1; // 16 bytes (vec4)
};                                                  // Total: 112 bytes
static_assert(sizeof(PushConstantData) == 112, "PushConstantData must be 112 bytes");

inline uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

inline VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
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
    VkPipeline mirrorDepthClearPipeline = VK_NULL_HANDLE;
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
    std::vector<Vertex> depthClearTriangles;
    std::vector<Vertex> mirrorTriangles;
    std::vector<Vertex> mirrorLines;
    std::vector<Vertex> overlayTriangles;
    std::vector<Vertex> overlayLines;
    std::vector<Vertex> frameVertices;
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

    VkPipeline createPipeline(VkPrimitiveTopology topology, bool depthTest, bool depthWrite, bool colorWrite, StencilMode stencilMode, VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS)
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
        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
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
        depth.depthCompareOp = depthCompareOp;
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
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(PushConstantData);
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
        // Depth-clear pipeline: writes far-plane depth in the stencil-masked mirror region
        mirrorDepthClearPipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, false, StencilMode::TestMirror, VK_COMPARE_OP_ALWAYS);
        // Reflected pipelines now have depth test+write so occluded faces are properly hidden
        reflectedTrianglePipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true, StencilMode::TestMirror);
        reflectedLinePipeline = createPipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, true, true, StencilMode::TestMirror);
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
                             uint32_t depthClearTriCount,
                             uint32_t mirrorTriCount,
                             uint32_t mirrorLineCount,
                             uint32_t overlayTriCount,
                             uint32_t overlayLineCount,
                             VkRect2D mirrorScissor)
    {
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        // Sky/fog color — used for both clear and fog blending
        const float fogR = 0.52f, fogG = 0.68f, fogB = 0.82f;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{fogR, fogG, fogB, 1.0f}};
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
        const Vec3 camPos = game.cameraPosition();
        const float wTime = game.getWorldTime();
        const VkShaderStageFlags allStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        // Helper to build push constant data
        auto makePushData = [&](const Mat4 &mvp, const Vec3 &pushCameraPos, bool reflectionClip) -> PushConstantData {
            PushConstantData pc{};
            pc.mvp = mvp;
            pc.cameraPosX = pushCameraPos.x;
            pc.cameraPosY = pushCameraPos.y;
            pc.cameraPosZ = pushCameraPos.z;
            pc.pad0 = 0.0f;
            pc.fogR = fogR;
            pc.fogG = fogG;
            pc.fogB = fogB;
            pc.worldTime = wTime;
            pc.reflectionClipZ = MirrorFaceZ;
            pc.reflectionClipSide = game.reflectionClipSide();
            pc.reflectionClipEnabled = reflectionClip ? 1.0f : 0.0f;
            pc.pad1 = 0.0f;
            return pc;
        };

        Mat4 worldMvp = game.viewProjection(aspect);
        Mat4 reflectionMvp = worldMvp;
        Mat4 overlayMvp = Mat4::identity();

        PushConstantData worldPC = makePushData(worldMvp, camPos, false);
        PushConstantData reflectionPC = makePushData(reflectionMvp, camPos, true);
        PushConstantData overlayPC = makePushData(overlayMvp, camPos, false);

        uint32_t first = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &worldPC);
        if (triCount > 0) {
            vkCmdDraw(commandBuffer, triCount, 1, first, 0);
        }
        first += triCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &worldPC);
        if (lineCount > 0) {
            vkCmdDraw(commandBuffer, lineCount, 1, first, 0);
        }
        first += lineCount;

        vkCmdSetScissor(commandBuffer, 0, 1, &mirrorScissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mirrorMaskPipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &worldPC);
        if (mirrorMaskTriCount > 0) {
            vkCmdDraw(commandBuffer, mirrorMaskTriCount, 1, first, 0);
        }
        first += mirrorMaskTriCount;

        // Clear the depth buffer in the mirror stencil region to far-plane (z≈1.0)
        // using a fullscreen clip-space quad with identity MVP.
        // This ensures reflected geometry (using reflected MVP) depth-tests against itself,
        // not against stale world-scene depth values.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mirrorDepthClearPipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &overlayPC);
        if (depthClearTriCount > 0) {
            vkCmdDraw(commandBuffer, depthClearTriCount, 1, first, 0);
        }
        first += depthClearTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, reflectedTrianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &reflectionPC);
        if (reflectionTriCount > 0) {
            vkCmdDraw(commandBuffer, reflectionTriCount, 1, first, 0);
        }
        first += reflectionTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, reflectedLinePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &reflectionPC);
        if (reflectionLineCount > 0) {
            vkCmdDraw(commandBuffer, reflectionLineCount, 1, first, 0);
        }
        first += reflectionLineCount;

        vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &worldPC);
        if (mirrorTriCount > 0) {
            vkCmdDraw(commandBuffer, mirrorTriCount, 1, first, 0);
        }
        first += mirrorTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &worldPC);
        if (mirrorLineCount > 0) {
            vkCmdDraw(commandBuffer, mirrorLineCount, 1, first, 0);
        }
        first += mirrorLineCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &overlayPC);
        if (overlayTriCount > 0) {
            vkCmdDraw(commandBuffer, overlayTriCount, 1, first, 0);
        }
        first += overlayTriCount;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline);
        vkCmdPushConstants(commandBuffer, pipelineLayout, allStages, 0, sizeof(PushConstantData), &overlayPC);
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
        VkRect2D mirrorScissor{{0, 0}, swapchainExtent};
        if (game.mirrorScreenRect(static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height), aspect, &mirrorScissor)) {
            game.buildReflectionMeshes(reflectionTriangles, reflectionLines);
            game.buildMirrorMask(mirrorMaskTriangles);
        } else {
            reflectionTriangles.clear();
            reflectionLines.clear();
            mirrorMaskTriangles.clear();
        }
        game.buildMirrorMeshes(mirrorTriangles, mirrorLines);
        game.buildOverlay(overlayTriangles, overlayLines, aspect);

        depthClearTriangles.clear();
        if (!mirrorMaskTriangles.empty()) {
            // Fullscreen clip-space quad at z=0.9999 for clearing depth in the mirror stencil region.
            // With the identity MVP, these clip coords map directly: x,y cover full screen, z≈1.0 = far depth.
            const Vec3 dcCol{0.0f, 0.0f, 0.0f};
            const Vec3 dcNrm{0.0f, 0.0f, 0.0f};
            depthClearTriangles.push_back({{-1.0f, -1.0f, 0.9999f}, dcCol, dcNrm});
            depthClearTriangles.push_back({{ 1.0f, -1.0f, 0.9999f}, dcCol, dcNrm});
            depthClearTriangles.push_back({{ 1.0f,  1.0f, 0.9999f}, dcCol, dcNrm});
            depthClearTriangles.push_back({{-1.0f, -1.0f, 0.9999f}, dcCol, dcNrm});
            depthClearTriangles.push_back({{ 1.0f,  1.0f, 0.9999f}, dcCol, dcNrm});
            depthClearTriangles.push_back({{-1.0f,  1.0f, 0.9999f}, dcCol, dcNrm});
        }

        frameVertices.clear();
        frameVertices.reserve(triangles.size() + lines.size() +
                              reflectionTriangles.size() + reflectionLines.size() +
                              mirrorMaskTriangles.size() + depthClearTriangles.size() +
                              mirrorTriangles.size() + mirrorLines.size() +
                              overlayTriangles.size() + overlayLines.size());
        frameVertices.insert(frameVertices.end(), triangles.begin(), triangles.end());
        frameVertices.insert(frameVertices.end(), lines.begin(), lines.end());
        frameVertices.insert(frameVertices.end(), mirrorMaskTriangles.begin(), mirrorMaskTriangles.end());
        frameVertices.insert(frameVertices.end(), depthClearTriangles.begin(), depthClearTriangles.end());
        frameVertices.insert(frameVertices.end(), reflectionTriangles.begin(), reflectionTriangles.end());
        frameVertices.insert(frameVertices.end(), reflectionLines.begin(), reflectionLines.end());
        frameVertices.insert(frameVertices.end(), mirrorTriangles.begin(), mirrorTriangles.end());
        frameVertices.insert(frameVertices.end(), mirrorLines.begin(), mirrorLines.end());
        frameVertices.insert(frameVertices.end(), overlayTriangles.begin(), overlayTriangles.end());
        frameVertices.insert(frameVertices.end(), overlayLines.begin(), overlayLines.end());
        uploadVertices(frameVertices);

        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex,
                            static_cast<uint32_t>(triangles.size()),
                            static_cast<uint32_t>(lines.size()),
                            static_cast<uint32_t>(reflectionTriangles.size()),
                            static_cast<uint32_t>(reflectionLines.size()),
                            static_cast<uint32_t>(mirrorMaskTriangles.size()),
                            static_cast<uint32_t>(depthClearTriangles.size()),
                            static_cast<uint32_t>(mirrorTriangles.size()),
                            static_cast<uint32_t>(mirrorLines.size()),
                            static_cast<uint32_t>(overlayTriangles.size()),
                            static_cast<uint32_t>(overlayLines.size()),
                            mirrorScissor);

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
        if (mirrorDepthClearPipeline) {
            vkDestroyPipeline(device, mirrorDepthClearPipeline, nullptr);
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
        mirrorDepthClearPipeline = VK_NULL_HANDLE;
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

} // namespace vws
