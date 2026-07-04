#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <functional>

// Minimal Vulkan context for ImGui-only overlay rendering.
// No 3D pipelines, no depth buffer - just what's needed for 2D UI.
class OverlayVulkanContext {
public:
    OverlayVulkanContext() = default;
    ~OverlayVulkanContext();

    // Initialize all Vulkan resources
    bool init(GLFWwindow* window);

    // Clean up all Vulkan resources
    void cleanup();

    // Frame rendering lifecycle
    bool beginFrame();      // Acquire swapchain image, start ImGui frame
    void renderImGui();     // Record ImGui draw commands to command buffer
    void endFrame();        // Submit command buffer and present

    // Handle window resize
    void onWindowResize(int width, int height);

    // Getters for debugging/info
    VkDevice getDevice() const { return device_; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice_; }

private:
    // Initialization helpers
    bool createInstance();
    bool createSurface(GLFWwindow* window);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool allocateCommandBuffers();
    bool createSyncObjects();
    bool initImGui(GLFWwindow* window);

    // Swapchain recreation
    void cleanupSwapchain();
    void recreateSwapchain();

    // Queue family helpers
    struct QueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        bool isComplete() const {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    // Swapchain support helpers
    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    // Vulkan Core
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = 0;
    uint32_t presentFamily_ = 0;

    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};

    // Render Pass (no depth, color only)
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // Command Buffers
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Sync Objects (double buffering)
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    std::vector<VkFence> imagesInFlight_;
    size_t currentFrame_ = 0;
    uint32_t currentImageIndex_ = 0;

    // ImGui
    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    struct ImGuiContext* savedMainContext_ = nullptr;  // Main app's context to restore on cleanup
    struct ImGuiContext* overlayContext_ = nullptr;    // Our overlay's context

    // State
    GLFWwindow* window_ = nullptr;
    bool framebufferResized_ = false;
    bool initialized_ = false;
};
