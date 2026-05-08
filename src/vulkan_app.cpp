#include "vulkan_app.hpp"

#include "shaders.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace vws {

VulkanApp::while (running)
{
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

VulkanApp::if (!SDL_Init(SDL_INIT_VIDEO))
{
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }

VulkanApp::if (!window)
{
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

VulkanApp::if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
{
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }

VulkanApp::if (!sdlExtensions)
{
            throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
        }

VulkanApp::if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateInstance failed");
        }

VulkanApp::for (uint32_t i = 0; i < count; ++i)
{
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

VulkanApp::for (const auto &extension : available)
{
            if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
                break;
            }
        }

VulkanApp::if (count == 0)
{
            throw std::runtime_error("No Vulkan physical devices found");
        }

VulkanApp::for (VkPhysicalDevice candidate : devices)
{
            if (deviceSuitable(candidate)) {
                physicalDevice = candidate;
                return;
            }
        }

VulkanApp::for (uint32_t queueFamily : uniqueQueues)
{
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueInfos.push_back(queueInfo);
        }

VulkanApp::if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateDevice failed");
        }

VulkanApp::for (const auto &format : formats)
{
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }

VulkanApp::for (const auto &mode : modes)
{
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }

VulkanApp::if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
{
            return capabilities.currentExtent;
        }

VulkanApp::if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
{
            imageCount = capabilities.maxImageCount;
        }

VulkanApp::if (indices.graphics != indices.present)
{
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies;
        }

VulkanApp::if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateSwapchainKHR failed");
        }

VulkanApp::if (vkCreateImageView(device, &createInfo, nullptr, &view) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateImageView failed");
        }

VulkanApp::for (size_t i = 0; i < swapchainImages.size(); ++i)
{
            swapchainImageViews[i] = createImageView(swapchainImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }

VulkanApp::if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateRenderPass failed");
        }

VulkanApp::if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateShaderModule failed");
        }

VulkanApp::if (stencilMode == StencilMode::WriteMirror)
{
            depth.front.compareOp = VK_COMPARE_OP_ALWAYS;
            depth.front.passOp = VK_STENCIL_OP_REPLACE;
            depth.front.failOp = VK_STENCIL_OP_KEEP;
            depth.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depth.front.compareMask = 0xff;
            depth.front.writeMask = 0xff;
            depth.front.reference = 1;
            depth.back = depth.front;
        }

} else VulkanApp::if (stencilMode == StencilMode::TestMirror)
{
            depth.front.compareOp = VK_COMPARE_OP_EQUAL;
            depth.front.passOp = VK_STENCIL_OP_KEEP;
            depth.front.failOp = VK_STENCIL_OP_KEEP;
            depth.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depth.front.compareMask = 0xff;
            depth.front.writeMask = 0x00;
            depth.front.reference = 1;
            depth.back = depth.front;
        }

VulkanApp::if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateGraphicsPipelines failed");
        }

VulkanApp::if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreatePipelineLayout failed");
        }

VulkanApp::if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateBuffer failed");
        }

VulkanApp::if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
{
            throw std::runtime_error("vkAllocateMemory failed");
        }

VulkanApp::if (vertexCount <= vertexBufferCapacity)
{
            return;
        }

VulkanApp::if (vertexBuffer)
{
            vkDestroyBuffer(device, vertexBuffer, nullptr);
            vkFreeMemory(device, vertexMemory, nullptr);
        }

VulkanApp::if (vkCreateImage(device, &imageInfo, nullptr, &depthImage) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateImage failed");
        }

VulkanApp::if (vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory) != VK_SUCCESS)
{
            throw std::runtime_error("vkAllocateMemory depth failed");
        }

VulkanApp::for (size_t i = 0; i < swapchainImageViews.size(); ++i)
{
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

VulkanApp::if (vkCreateCommandPool(device, &info, nullptr, &commandPool) != VK_SUCCESS)
{
            throw std::runtime_error("vkCreateCommandPool failed");
        }

VulkanApp::if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
{
            throw std::runtime_error("vkAllocateCommandBuffers failed");
        }

VulkanApp::for (int i = 0; i < MaxFramesInFlight; ++i)
{
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create sync objects");
            }
        }

VulkanApp::if (triCount > 0)
{
            vkCmdDraw(commandBuffer, triCount, 1, first, 0);
        }

VulkanApp::if (lineCount > 0)
{
            vkCmdDraw(commandBuffer, lineCount, 1, first, 0);
        }

VulkanApp::if (mirrorMaskTriCount > 0)
{
            vkCmdDraw(commandBuffer, mirrorMaskTriCount, 1, first, 0);
        }

VulkanApp::if (depthClearTriCount > 0)
{
            vkCmdDraw(commandBuffer, depthClearTriCount, 1, first, 0);
        }

VulkanApp::if (reflectionTriCount > 0)
{
            vkCmdDraw(commandBuffer, reflectionTriCount, 1, first, 0);
        }

VulkanApp::if (reflectionLineCount > 0)
{
            vkCmdDraw(commandBuffer, reflectionLineCount, 1, first, 0);
        }

VulkanApp::if (mirrorTriCount > 0)
{
            vkCmdDraw(commandBuffer, mirrorTriCount, 1, first, 0);
        }

VulkanApp::if (mirrorLineCount > 0)
{
            vkCmdDraw(commandBuffer, mirrorLineCount, 1, first, 0);
        }

VulkanApp::if (overlayTriCount > 0)
{
            vkCmdDraw(commandBuffer, overlayTriCount, 1, first, 0);
        }

VulkanApp::if (overlayLineCount > 0)
{
            vkCmdDraw(commandBuffer, overlayLineCount, 1, first, 0);
        }

VulkanApp::if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
{
            throw std::runtime_error("vkEndCommandBuffer failed");
        }

VulkanApp::if (result == VK_ERROR_OUT_OF_DATE_KHR)
{
            recreateSwapchain();
            return;
        }

VulkanApp::if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
{
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        }

VulkanApp::if (game.mirrorScreenRect(static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height), aspect, &mirrorScissor))
{
            game.buildReflectionMeshes(reflectionTriangles, reflectionLines);
            game.buildMirrorMask(mirrorMaskTriangles);
        }

VulkanApp::if (!mirrorMaskTriangles.empty())
{
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

VulkanApp::if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
{
            throw std::runtime_error("vkQueueSubmit failed");
        }

VulkanApp::if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
{
            framebufferResized = false;
            recreateSwapchain();
        }

} else VulkanApp::if (result != VK_SUCCESS)
{
            throw std::runtime_error("vkQueuePresentKHR failed");
        }

VulkanApp::for (VkFramebuffer framebuffer : framebuffers)
{
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

VulkanApp::if (depthImageView)
{
            vkDestroyImageView(device, depthImageView, nullptr);
        }

VulkanApp::if (depthImage)
{
            vkDestroyImage(device, depthImage, nullptr);
        }

VulkanApp::if (depthMemory)
{
            vkFreeMemory(device, depthMemory, nullptr);
        }

VulkanApp::if (trianglePipeline)
{
            vkDestroyPipeline(device, trianglePipeline, nullptr);
        }

VulkanApp::if (linePipeline)
{
            vkDestroyPipeline(device, linePipeline, nullptr);
        }

VulkanApp::if (mirrorMaskPipeline)
{
            vkDestroyPipeline(device, mirrorMaskPipeline, nullptr);
        }

VulkanApp::if (mirrorDepthClearPipeline)
{
            vkDestroyPipeline(device, mirrorDepthClearPipeline, nullptr);
        }

VulkanApp::if (reflectedTrianglePipeline)
{
            vkDestroyPipeline(device, reflectedTrianglePipeline, nullptr);
        }

VulkanApp::if (reflectedLinePipeline)
{
            vkDestroyPipeline(device, reflectedLinePipeline, nullptr);
        }

VulkanApp::for (VkImageView view : swapchainImageViews)
{
            vkDestroyImageView(device, view, nullptr);
        }

VulkanApp::if (swapchain)
{
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }

VulkanApp::if (width == 0 || height == 0)
{
            return;
        }

VulkanApp::if (device)
{
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

VulkanApp::if (surface)
{
            SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        }

VulkanApp::if (instance)
{
            vkDestroyInstance(instance, nullptr);
        }

VulkanApp::if (window)
{
            SDL_DestroyWindow(window);
        }

} // namespace vws
