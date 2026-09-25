#include "vulkan_context.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <stdexcept>

#include "spdlog/spdlog.h"

namespace
{
	void check_vk(VkResult result, const char* operation)
	{
		if (result != VK_SUCCESS)
			throw std::runtime_error(std::string{ operation } + " failed with VkResult " + std::to_string(result));
	}

	struct QueueFamilyIndices
	{
		uint32_t graphics{ UINT32_MAX };
		uint32_t present{ UINT32_MAX };
	};

	QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
	{
		uint32_t count{};
		vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

		QueueFamilyIndices result;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				result.graphics = i;

			VkBool32 presentSupported{};
			if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported) == VK_SUCCESS && presentSupported)
				result.present = i;
		}
		return result;
	}

	bool has_swapchain_extension(VkPhysicalDevice device)
	{
		uint32_t count{};
		vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> extensions(count);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
		return std::ranges::any_of(extensions, [](const auto& extension)
		{
			return std::string_view{ extension.extensionName } == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		});
	}

	bool supports_surface(VkPhysicalDevice device, VkSurfaceKHR surface)
	{
		uint32_t formatCount{};
		uint32_t presentModeCount{};
		return vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr) == VK_SUCCESS && formatCount > 0 &&
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr) == VK_SUCCESS && presentModeCount > 0;
	}
}

ax::VulkanContext::~VulkanContext()
{
	cleanup();
}

bool ax::VulkanContext::initialize(GLFWwindow* window)
{
	uint32_t extensionCount{};
	const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
	if (extensions == nullptr || extensionCount == 0)
	{
		spdlog::error("GLFW did not provide Vulkan instance extensions");
		return false;
	}

	VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.pApplicationName = "AxEng";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "AxEng";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = extensionCount;
	instanceInfo.ppEnabledExtensionNames = extensions;
	if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
	{
		spdlog::error("Failed to create Vulkan instance");
		return false;
	}

	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
	{
		spdlog::error("Failed to create Vulkan surface");
		cleanup();
		return false;
	}

	uint32_t deviceCount{};
	if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0)
	{
		spdlog::error("No Vulkan physical devices found");
		cleanup();
		return false;
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
	for (const auto candidate : devices)
	{
		const auto queues = find_queue_families(candidate, surface);
		if (queues.graphics == UINT32_MAX || queues.present == UINT32_MAX || !has_swapchain_extension(candidate) || !supports_surface(candidate, surface))
			continue;

		physicalDevice = candidate;
		graphicsQueueFamily = queues.graphics;
		presentQueueFamily = queues.present;
		break;
	}
	if (physicalDevice == VK_NULL_HANDLE)
	{
		spdlog::error("No Vulkan device supports graphics and swapchain presentation");
		cleanup();
		return false;
	}

	const std::array queuePriorities{ 1.0f };
	std::set<uint32_t> queueFamilies{ graphicsQueueFamily, presentQueueFamily };
	std::vector<VkDeviceQueueCreateInfo> queueInfos;
	for (const auto family : queueFamilies)
	{
		VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = family;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = queuePriorities.data();
		queueInfos.push_back(queueInfo);
	}

	const char* deviceExtensions[]{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkPhysicalDeviceFeatures features{};
	VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
	deviceInfo.pQueueCreateInfos = queueInfos.data();
	deviceInfo.enabledExtensionCount = 1;
	deviceInfo.ppEnabledExtensionNames = deviceExtensions;
	deviceInfo.pEnabledFeatures = &features;
	if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
	{
		spdlog::error("Failed to create Vulkan logical device");
		cleanup();
		return false;
	}

	vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);

	VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.queueFamilyIndex = graphicsQueueFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
	{
		spdlog::error("Failed to create Vulkan command pool");
		cleanup();
		return false;
	}

	return true;
}

void ax::VulkanContext::cleanup()
{
	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);
	if (commandPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, commandPool, nullptr);
	if (device != VK_NULL_HANDLE)
		vkDestroyDevice(device, nullptr);
	if (surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(instance, surface, nullptr);
	if (instance != VK_NULL_HANDLE)
		vkDestroyInstance(instance, nullptr);
	commandPool = VK_NULL_HANDLE;
	device = VK_NULL_HANDLE;
	surface = VK_NULL_HANDLE;
	instance = VK_NULL_HANDLE;
	physicalDevice = VK_NULL_HANDLE;
}

uint32_t ax::VulkanContext::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
	VkPhysicalDeviceMemoryProperties memoryProperties{};
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		if ((typeFilter & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	throw std::runtime_error("No compatible Vulkan memory type found");
}

void ax::VulkanContext::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) const
{
	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check_vk(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(device, buffer, &requirements);
	VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocationInfo.allocationSize = requirements.size;
	allocationInfo.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, properties);
	check_vk(vkAllocateMemory(device, &allocationInfo, nullptr, &memory), "vkAllocateMemory");
	check_vk(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
}

void ax::VulkanContext::create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory) const
{
	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = { width, height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check_vk(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage");

	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(device, image, &requirements);
	VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocationInfo.allocationSize = requirements.size;
	allocationInfo.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, properties);
	check_vk(vkAllocateMemory(device, &allocationInfo, nullptr, &memory), "vkAllocateMemory");
	check_vk(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");
}

VkCommandBuffer ax::VulkanContext::begin_single_time_commands() const
{
	VkCommandBufferAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocationInfo.commandPool = commandPool;
	allocationInfo.commandBufferCount = 1;
	VkCommandBuffer commandBuffer{};
	check_vk(vkAllocateCommandBuffers(device, &allocationInfo, &commandBuffer), "vkAllocateCommandBuffers");
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check_vk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
	return commandBuffer;
}

void ax::VulkanContext::end_single_time_commands(VkCommandBuffer commandBuffer) const
{
	check_vk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	check_vk(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
	check_vk(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle");
	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}
