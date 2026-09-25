#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#undef APIENTRY

#include <vulkan/vulkan.h>

namespace ax
{
	class VulkanContext final
	{
	public:
		VulkanContext() = default;
		~VulkanContext();

		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

		bool initialize(GLFWwindow* window);
		void cleanup();

		uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
		void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const;
		void create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory) const;
		VkCommandBuffer begin_single_time_commands() const;
		void end_single_time_commands(VkCommandBuffer commandBuffer) const;

		VkInstance instance{ VK_NULL_HANDLE };
		VkSurfaceKHR surface{ VK_NULL_HANDLE };
		VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
		VkDevice device{ VK_NULL_HANDLE };
		VkQueue graphicsQueue{ VK_NULL_HANDLE };
		VkQueue presentQueue{ VK_NULL_HANDLE };
		uint32_t graphicsQueueFamily{ 0 };
		uint32_t presentQueueFamily{ 0 };
		VkCommandPool commandPool{ VK_NULL_HANDLE };
	};
}
