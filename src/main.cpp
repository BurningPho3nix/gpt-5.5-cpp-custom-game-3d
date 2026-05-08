#include "vulkan_app.hpp"

#include <exception>
#include <iostream>

int main()
{
    try {
        vws::VulkanApp app;
        app.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
