#include "imgui_helper.h"
#include "imgui_style.h"
#include "log_timer.h"
#include "perf_profiler.h"
#include "window.h"
#include "texture.h"

#include "spdlog/spdlog.h"

#include "backends/imgui_impl_glfw.h"
#include <imgui_impl_vulkan.h>
#include "IconsFontAwesome6.h"
#include "ImGuiNotify.hpp"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>

std::map<GLFWwindow*, ax::Window*> s_windows{};
std::mutex s_windows_mutex{};

static void glfw_error_callback(int error, const char* description)
{
	spdlog::error("glfw error {}: {}", error, description);
}

ax::Window::Window(const WindowDefinition& def)
	: m_width{ def.width },
	m_height{ def.height },
	m_vsync{ def.vsync }
{
	LogTimer _timer{ "create window" };
	m_window = glfwCreateWindow(m_width, m_height, def.title.data(), NULL, NULL);

	std::lock_guard lock{ s_windows_mutex };
	s_windows.emplace(m_window, this);
}

ax::Window::~Window()
{
	if (m_context && m_context->device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(m_context->device);

	if (m_window)
	{
		{
			std::lock_guard lock{ s_windows_mutex };
			s_windows.erase(m_window);

			if (s_windows.size() == 0)
			{
				ImGui_ImplGlfw_Shutdown();
				ImGui_ImplVulkan_Shutdown();
			}
		}
	}

	if (m_context && m_context->device != VK_NULL_HANDLE)
	{
		if (m_spriteMapped)
			vkUnmapMemory(m_context->device, m_spriteMemory);

		if (m_pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(m_context->device, m_pipeline, nullptr);
		if (m_pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_context->device, m_pipelineLayout, nullptr);
		if (m_spriteDescriptorPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(m_context->device, m_spriteDescriptorPool, nullptr);
		if (m_spriteSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_context->device, m_spriteSetLayout, nullptr);
		if (m_viewportSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_context->device, m_viewportSetLayout, nullptr);
		if (m_sampler != VK_NULL_HANDLE)
			vkDestroySampler(m_context->device, m_sampler, nullptr);
		if (m_spriteBuffer != VK_NULL_HANDLE)
			vkDestroyBuffer(m_context->device, m_spriteBuffer, nullptr);
		if (m_spriteMemory != VK_NULL_HANDLE)
			vkFreeMemory(m_context->device, m_spriteMemory, nullptr);
		if (m_viewportBuffer != VK_NULL_HANDLE)
			vkDestroyBuffer(m_context->device, m_viewportBuffer, nullptr);
		if (m_viewportMemory != VK_NULL_HANDLE)
			vkFreeMemory(m_context->device, m_viewportMemory, nullptr);
		destroy_swapchain();
		if (m_renderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(m_context->device, m_renderPass, nullptr);
		if (m_depthView != VK_NULL_HANDLE)
			vkDestroyImageView(m_context->device, m_depthView, nullptr);
		if (m_depthImage != VK_NULL_HANDLE)
			vkDestroyImage(m_context->device, m_depthImage, nullptr);
		if (m_depthMemory != VK_NULL_HANDLE)
			vkFreeMemory(m_context->device, m_depthMemory, nullptr);
		if (m_imageAvailable != VK_NULL_HANDLE)
			vkDestroySemaphore(m_context->device, m_imageAvailable, nullptr);
		if (m_renderFinished != VK_NULL_HANDLE)
			vkDestroySemaphore(m_context->device, m_renderFinished, nullptr);
		if (m_inFlight != VK_NULL_HANDLE)
			vkDestroyFence(m_context->device, m_inFlight, nullptr);
	}

	if (m_window)
		glfwDestroyWindow(m_window);
}

bool ax::setup_glfw()
{
	LogTimer _timer{ "glfw setup" };

	glfwSetErrorCallback(glfw_error_callback);

	if (!glfwInit())
	{
		spdlog::error("glfw init failed");
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	
	return true;
}

void ax::teardown_glfw()
{
	glfwTerminate();
}

bool ax::Window::init_vulkan()
{
	LogTimer _timer{ "Vulkan initial setup" };
	try
	{
		m_context = std::make_shared<VulkanContext>();
		if (!m_context->initialize(m_window) || !create_swapchain())
			return false;
		create_depth_resources();
		if (!create_render_pass() || !create_framebuffers())
			return false;
		create_descriptor_resources();
		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		if (vkCreateSampler(m_context->device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
			throw std::runtime_error("Failed to create Vulkan sampler");
		ensure_uniform_capacity(m_uniformsCapacity);
		if (!create_sprite_pipeline())
			return false;
		return create_sync_objects();
	}
	catch (const std::exception& e)
	{
		spdlog::error("Vulkan initialization failed: {}", e.what());
		return false;
	}
}

void ax::Window::reload_pipeline()
{
	if (m_context && m_context->device != VK_NULL_HANDLE)
		create_sprite_pipeline();
}

bool ax::Window::create_swapchain()
{
	VkSurfaceCapabilitiesKHR capabilities{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->physicalDevice, m_context->surface, &capabilities) != VK_SUCCESS)
		return false;

	uint32_t formatCount{};
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->physicalDevice, m_context->surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->physicalDevice, m_context->surface, &formatCount, formats.data());
	if (formats.empty())
		return false;
	auto selectedFormat = formats.front();
	for (const auto preferredFormat : { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM })
	{
		const auto it = std::ranges::find_if(formats, [preferredFormat](const auto& format)
		{
			return format.format == preferredFormat && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		});
		if (it != formats.end())
		{
			selectedFormat = *it;
			break;
		}
	}
	m_surfaceFormat = selectedFormat.format;

	uint32_t modeCount{};
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_context->physicalDevice, m_context->surface, &modeCount, nullptr);
	std::vector<VkPresentModeKHR> modes(modeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_context->physicalDevice, m_context->surface, &modeCount, modes.data());
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!m_vsync && std::ranges::find(modes, VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
		presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		m_swapchainExtent = capabilities.currentExtent;
	else
	{
		int width{}, height{};
		glfwGetFramebufferSize(m_window, &width, &height);
		m_swapchainExtent.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		m_swapchainExtent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	}
	uint32_t imageCount = std::max(2u, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0)
		imageCount = std::min(imageCount, capabilities.maxImageCount);

	VkSwapchainCreateInfoKHR swapchainInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchainInfo.surface = m_context->surface;
	swapchainInfo.minImageCount = imageCount;
	swapchainInfo.imageFormat = selectedFormat.format;
	swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
	swapchainInfo.imageExtent = m_swapchainExtent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	const uint32_t queueFamilies[]{ m_context->graphicsQueueFamily, m_context->presentQueueFamily };
	if (m_context->graphicsQueueFamily != m_context->presentQueueFamily)
	{
		swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchainInfo.queueFamilyIndexCount = 2;
		swapchainInfo.pQueueFamilyIndices = queueFamilies;
	}
	else
		swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform = capabilities.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = VK_TRUE;
	if (vkCreateSwapchainKHR(m_context->device, &swapchainInfo, nullptr, &m_swapchain) != VK_SUCCESS)
		return false;

	vkGetSwapchainImagesKHR(m_context->device, m_swapchain, &imageCount, nullptr);
	m_swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(m_context->device, m_swapchain, &imageCount, m_swapchainImages.data());
	m_swapchainImageViews.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = m_swapchainImages[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_surfaceFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(m_context->device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS)
			return false;
	}
	return true;
}

bool ax::Window::create_render_pass()
{
	VkAttachmentDescription color{};
	color.format = m_surfaceFormat;
	color.samples = VK_SAMPLE_COUNT_1_BIT;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depth{};
	depth.format = VK_FORMAT_D32_SFLOAT;
	depth.samples = VK_SAMPLE_COUNT_1_BIT;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorReference;
	subpass.pDepthStencilAttachment = &depthReference;
	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstStageMask = dependency.srcStageMask;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	const VkAttachmentDescription attachments[]{ color, depth };
	VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	info.attachmentCount = 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 1;
	info.pDependencies = &dependency;
	return vkCreateRenderPass(m_context->device, &info, nullptr, &m_renderPass) == VK_SUCCESS;
}

void ax::Window::create_depth_resources()
{
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormatProperties properties{};
	vkGetPhysicalDeviceFormatProperties(m_context->physicalDevice, depthFormat, &properties);
	if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
	m_depthFormat = depthFormat;
	m_context->create_image(m_swapchainExtent.width, m_swapchainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthMemory);
	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_depthImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = depthFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	if (vkCreateImageView(m_context->device, &viewInfo, nullptr, &m_depthView) != VK_SUCCESS)
		throw std::runtime_error("Failed to create depth image view");
}

bool ax::Window::create_framebuffers()
{
	m_framebuffers.resize(m_swapchainImageViews.size());
	for (size_t i = 0; i < m_swapchainImageViews.size(); ++i)
	{
		const VkImageView attachments[]{ m_swapchainImageViews[i], m_depthView };
		VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		info.renderPass = m_renderPass;
		info.attachmentCount = 2;
		info.pAttachments = attachments;
		info.width = m_swapchainExtent.width;
		info.height = m_swapchainExtent.height;
		info.layers = 1;
		if (vkCreateFramebuffer(m_context->device, &info, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
			return false;
	}
	return true;
}

void ax::Window::create_descriptor_resources()
{
	VkDescriptorSetLayoutBinding spriteBindings[2]{};
	spriteBindings[0].binding = 0;
	spriteBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	spriteBindings[0].descriptorCount = 1;
	spriteBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	spriteBindings[1].binding = 1;
	spriteBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	spriteBindings[1].descriptorCount = 1;
	spriteBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo spriteLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	spriteLayoutInfo.bindingCount = 2;
	spriteLayoutInfo.pBindings = spriteBindings;
	if (vkCreateDescriptorSetLayout(m_context->device, &spriteLayoutInfo, nullptr, &m_spriteSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create sprite descriptor layout");

	VkDescriptorSetLayoutBinding viewportBinding{};
	viewportBinding.binding = 0;
	viewportBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	viewportBinding.descriptorCount = 1;
	viewportBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	VkDescriptorSetLayoutCreateInfo viewportLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	viewportLayoutInfo.bindingCount = 1;
	viewportLayoutInfo.pBindings = &viewportBinding;
	if (vkCreateDescriptorSetLayout(m_context->device, &viewportLayoutInfo, nullptr, &m_viewportSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create viewport descriptor layout");

	const VkDescriptorPoolSize poolSizes[]{
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
	};
	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 4097;
	poolInfo.poolSizeCount = 3;
	poolInfo.pPoolSizes = poolSizes;
	if (vkCreateDescriptorPool(m_context->device, &poolInfo, nullptr, &m_spriteDescriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create sprite descriptor pool");

	m_context->create_buffer(sizeof(float) * 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_viewportBuffer, m_viewportMemory);
	VkDescriptorSetAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocateInfo.descriptorPool = m_spriteDescriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &m_viewportSetLayout;
	if (vkAllocateDescriptorSets(m_context->device, &allocateInfo, &m_viewportDescriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate viewport descriptor set");
	VkDescriptorBufferInfo bufferInfo{ m_viewportBuffer, 0, sizeof(float) * 4 };
	VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstSet = m_viewportDescriptorSet;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.pBufferInfo = &bufferInfo;
	vkUpdateDescriptorSets(m_context->device, 1, &write, 0, nullptr);
}

VkShaderModule ax::Window::load_shader(const std::string& path) const
{
	std::ifstream file{ path, std::ios::ate | std::ios::binary };
	if (!file)
		throw std::runtime_error("Could not load shader file: " + path);
	const auto size = file.tellg();
	if (size <= 0 || size % sizeof(uint32_t) != 0)
		throw std::runtime_error("Invalid SPIR-V shader file: " + path);
	std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), size);
	VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = static_cast<size_t>(size);
	info.pCode = code.data();
	VkShaderModule module{};
	if (vkCreateShaderModule(m_context->device, &info, nullptr, &module) != VK_SUCCESS)
		throw std::runtime_error("Failed to create shader module: " + path);
	return module;
}

bool ax::Window::create_sprite_pipeline()
{
	try
	{
		if (m_pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(m_context->device, m_pipeline, nullptr);
		if (m_pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_context->device, m_pipelineLayout, nullptr);
		const auto vertexShader = load_shader("shaders/sprite.vert.spv");
		const auto fragmentShader = load_shader("shaders/sprite.frag.spv");
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertexShader;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragmentShader;
		stages[1].pName = "main";
		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(m_swapchainExtent.width), static_cast<float>(m_swapchainExtent.height), 0.0f, 1.0f };
		VkRect2D scissor{ { 0, 0 }, m_swapchainExtent };
		VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;
		VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_NONE;
		raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depth.depthTestEnable = VK_TRUE;
		depth.depthWriteEnable = VK_TRUE;
		depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		VkPipelineColorBlendAttachmentState blend{};
		blend.blendEnable = VK_TRUE;
		blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.colorBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.alphaBlendOp = VK_BLEND_OP_ADD;
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo blendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blendState.attachmentCount = 1;
		blendState.pAttachments = &blend;

		const VkDescriptorSetLayout setLayouts[]{ m_spriteSetLayout, m_viewportSetLayout };
		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = setLayouts;
		if (vkCreatePipelineLayout(m_context->device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			throw std::runtime_error("Failed to create sprite pipeline layout");
		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &assembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &raster;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depth;
		pipelineInfo.pColorBlendState = &blendState;
		pipelineInfo.layout = m_pipelineLayout;
		pipelineInfo.renderPass = m_renderPass;
		pipelineInfo.subpass = 0;
		const auto result = vkCreateGraphicsPipelines(m_context->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
		vkDestroyShaderModule(m_context->device, fragmentShader, nullptr);
		vkDestroyShaderModule(m_context->device, vertexShader, nullptr);
		return result == VK_SUCCESS;
	}
	catch (const std::exception& e)
	{
		spdlog::error("Failed to create Vulkan sprite pipeline: {}", e.what());
		return false;
	}
}

bool ax::Window::create_sync_objects()
{
	VkCommandBufferAllocateInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	commandInfo.commandPool = m_context->commandPool;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	if (vkAllocateCommandBuffers(m_context->device, &commandInfo, &m_commandBuffer) != VK_SUCCESS)
		return false;
	VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	return vkCreateSemaphore(m_context->device, &semaphoreInfo, nullptr, &m_imageAvailable) == VK_SUCCESS &&
		vkCreateSemaphore(m_context->device, &semaphoreInfo, nullptr, &m_renderFinished) == VK_SUCCESS &&
		vkCreateFence(m_context->device, &fenceInfo, nullptr, &m_inFlight) == VK_SUCCESS;
}

void ax::Window::destroy_swapchain()
{
	for (const auto framebuffer : m_framebuffers)
		vkDestroyFramebuffer(m_context->device, framebuffer, nullptr);
	m_framebuffers.clear();
	for (const auto view : m_swapchainImageViews)
		vkDestroyImageView(m_context->device, view, nullptr);
	m_swapchainImageViews.clear();
	m_swapchainImages.clear();
	if (m_swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(m_context->device, m_swapchain, nullptr);
	m_swapchain = VK_NULL_HANDLE;
}

ax::SpriteDefinition* ax::Window::allocate_sprite()
{
	SpriteDefinition* sprite;
	if (m_freeSpriteSlots.empty())
	{
		m_spriteSlots.emplace_back();
		sprite = &m_spriteSlots.back();
	}
	else
	{
		sprite = m_freeSpriteSlots.back();
		m_freeSpriteSlots.pop_back();
		*sprite = {};
	}

	m_activeSprites.push_back(sprite);
	return sprite;
}

void ax::Window::free_sprite(SpriteDefinition* sprite)
{
	const auto activeSprite = std::find(m_activeSprites.begin(), m_activeSprites.end(), sprite);
	if (activeSprite == m_activeSprites.end())
		return;

	if (sprite->groupedTexture != nullptr)
	{
		auto group = m_spriteGroups.find(sprite->groupedTexture);
		if (group != m_spriteGroups.end())
			group->second.erase(std::remove(group->second.begin(), group->second.end(), sprite), group->second.end());
	}

	*activeSprite = m_activeSprites.back();
	m_activeSprites.pop_back();
	*sprite = {};
	m_freeSpriteSlots.push_back(sprite);
}

std::size_t ax::Window::get_sprite_count() const
{
	return m_activeSprites.size();
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, float r, rectf region, float z, glm::vec4 color, glm::vec2 scale)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.rotation = r;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	sprite.gpuData.tint = color;
	sprite.gpuData.scale = scale;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, float z)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.z = z;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::render_texture(Texture* tex, glm::vec2 position, rectf region, float z)
{
	SpriteDefinition sprite{};
	sprite.tex = tex;
	sprite.gpuData.pos = position;
	sprite.gpuData.region = region;
	sprite.gpuData.useRegion = 1;
	sprite.gpuData.z = z;
	m_pendingTextures.push_back(sprite);
}

void ax::Window::ensure_uniform_capacity(uint32_t required)
{
	if (required <= m_uniformsCapacity && m_spriteBuffer != VK_NULL_HANDLE)
		return;

	while (m_uniformsCapacity < required)
		m_uniformsCapacity *= 2;
	if (m_spriteMapped)
	{
		vkUnmapMemory(m_context->device, m_spriteMemory);
		m_spriteMapped = nullptr;
	}
	for (const auto& entry : m_textureBindGroups)
		vkFreeDescriptorSets(m_context->device, m_spriteDescriptorPool, 1, &entry.second);
	m_textureBindGroups.clear();
	if (m_spriteBuffer != VK_NULL_HANDLE)
		vkDestroyBuffer(m_context->device, m_spriteBuffer, nullptr);
	if (m_spriteMemory != VK_NULL_HANDLE)
		vkFreeMemory(m_context->device, m_spriteMemory, nullptr);
	m_context->create_buffer(static_cast<VkDeviceSize>(m_uniformStride) * m_uniformsCapacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_spriteBuffer, m_spriteMemory);
	if (vkMapMemory(m_context->device, m_spriteMemory, 0, VK_WHOLE_SIZE, 0, &m_spriteMapped) != VK_SUCCESS)
		throw std::runtime_error("Failed to map Vulkan sprite buffer");
}

VkDescriptorSet ax::Window::setup_bind_groups(Texture* texture)
{
	VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocation.descriptorPool = m_spriteDescriptorPool;
	allocation.descriptorSetCount = 1;
	allocation.pSetLayouts = &m_spriteSetLayout;
	VkDescriptorSet descriptor{};
	if (vkAllocateDescriptorSets(m_context->device, &allocation, &descriptor) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate sprite descriptor set");
	VkDescriptorBufferInfo bufferInfo{ m_spriteBuffer, 0, static_cast<VkDeviceSize>(m_uniformStride) * m_uniformsCapacity };
	VkDescriptorImageInfo imageInfo{ m_sampler, texture->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet writes[2]{};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = descriptor;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[0].pBufferInfo = &bufferInfo;
	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = descriptor;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[1].pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(m_context->device, 2, writes, 0, nullptr);
	return descriptor;
}

void ax::Window::handle_tick(double delta)
{
	m_updateEventHandler.fire({ .delta = delta });
}

void ax::Window::prevent_close()
{
	m_can_close = false;
}

bool ax::Window::init_imgui()
{
	LogTimer _timer{ "imgui setup" };

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO();

	if (!ImGui_ImplGlfw_InitForVulkan(m_window, false))
		return false;
	ImGui_ImplVulkan_InitInfo info{};
	info.ApiVersion = VK_API_VERSION_1_1;
	info.Instance = m_context->instance;
	info.PhysicalDevice = m_context->physicalDevice;
	info.Device = m_context->device;
	info.QueueFamily = m_context->graphicsQueueFamily;
	info.Queue = m_context->graphicsQueue;
	info.DescriptorPoolSize = 2048;
	info.MinImageCount = 2;
	info.ImageCount = static_cast<uint32_t>(m_swapchainImages.size());
	info.PipelineInfoMain.RenderPass = m_renderPass;
	info.PipelineInfoMain.Subpass = 0;
	info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	if (!ImGui_ImplVulkan_Init(&info))
		return false;

	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ax::setup_imgui_style();

	// Default font
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig default_font_config{};
	const auto baseFontSize = 15.0f;
	default_font_config.SizePixels = baseFontSize;
	default_font_config.ExtraSizeScale = 1.0f;
	io.Fonts->AddFontFromFileTTF("../fonts/Montserrat-Medium.ttf", baseFontSize, &default_font_config);

	// Merge in icons from Font Awesome
	const auto iconFontSize = baseFontSize * 2.0f / 3.0f;
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config{};
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;
	icons_config.SizePixels = iconFontSize;
	icons_config.ExtraSizeScale = 1.0f;
	const auto merged_font{ io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_FAS, iconFontSize, &icons_config, icons_ranges) };
	ax::ImGuiHelper::add_font("default", merged_font);

	// Add a monospace font
	ImFontConfig mono_font_config{};
	mono_font_config.SizePixels = baseFontSize;
	mono_font_config.ExtraSizeScale = 1.0f;
	const auto mono_font{ io.Fonts->AddFontFromFileTTF("../fonts/SourceCodePro-Medium.ttf", baseFontSize, &mono_font_config) };
	ax::ImGuiHelper::add_font("mono", mono_font);

	return true;
}

void ax::Window::register_window_events()
{
	glfwSetWindowCloseCallback(m_window, ax::Window::window_close_handler);
}

void ax::Window::window_close_handler(GLFWwindow* window)
{
	const auto idx{ s_windows.find(window) };
	if (idx != s_windows.end())
	{
		if (idx->second != nullptr)
		{
			// Set m_can_close to false here (Window::prevent_close()) to stop window closing.
			idx->second->get_request_close_event_handler().fire({ idx->second });

			if (!idx->second->m_can_close)
			{
				glfwSetWindowShouldClose(window, false);
				idx->second->m_can_close = true;
			}
		}
	}
}

double updatePrev{ 0 };
double updateDelta{ 0 };
void ax::Window::run_loop()
{
	register_window_events();
	ax::input::KeyEventHandler::register_events(m_window);
	ImGui_ImplGlfw_InstallCallbacks(m_window);

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	while (!glfwWindowShouldClose(m_window))
	{
		glfwPollEvents();

		updateDelta = glfwGetTime() - updatePrev;
		updatePrev = glfwGetTime();

		PROFILER_TOP_LEVEL(frame);
		PROFILER_TOP_LEVEL_SEGMENT_SCOPED(frame);
		{
			PROFILER_SEGMENT_SCOPED(frame, tick);
			handle_tick(updateDelta);
		}

		{
			PROFILER_SEGMENT_SCOPED(frame, render);
			run_vulkan_render_pass(updateDelta);
		}
	}

	ax::input::KeyEventHandler::cleanup_events(m_window);
}

void ax::Window::run_vulkan_render_pass(double delta)
{
	m_preRenderEventHandler.fire({ .delta = delta });
	if (vkWaitForFences(m_context->device, 1, &m_inFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		return;
	uint32_t imageIndex{};
	const auto acquireResult = vkAcquireNextImageKHR(m_context->device, m_swapchain, UINT64_MAX, m_imageAvailable, VK_NULL_HANDLE, &imageIndex);
	if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
	{
		spdlog::error("Failed to acquire Vulkan swapchain image: {}", static_cast<int>(acquireResult));
		return;
	}
	vkResetFences(m_context->device, 1, &m_inFlight);
	vkResetCommandBuffer(m_commandBuffer, 0);
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS)
		return;
	VkClearValue clears[2]{};
	clears[0].color = { { m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a } };
	clears[1].depthStencil = { 1.0f, 0 };
	VkRenderPassBeginInfo passInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	passInfo.renderPass = m_renderPass;
	passInfo.framebuffer = m_framebuffers[imageIndex];
	passInfo.renderArea = { { 0, 0 }, m_swapchainExtent };
	passInfo.clearValueCount = 2;
	passInfo.pClearValues = clears;
	vkCmdBeginRenderPass(m_commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
	const float viewport[4]{ static_cast<float>(m_swapchainExtent.width), static_cast<float>(m_swapchainExtent.height), 0.0f, 0.0f };
	void* mapped{};
	if (vkMapMemory(m_context->device, m_viewportMemory, 0, sizeof(viewport), 0, &mapped) == VK_SUCCESS)
	{
		std::memcpy(mapped, viewport, sizeof(viewport));
		vkUnmapMemory(m_context->device, m_viewportMemory);
	}
	handle_render_pass(m_commandBuffer, delta);
	vkCmdEndRenderPass(m_commandBuffer);
	if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS)
		return;
	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &m_imageAvailable;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &m_commandBuffer;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &m_renderFinished;
	if (vkQueueSubmit(m_context->graphicsQueue, 1, &submit, m_inFlight) != VK_SUCCESS)
		return;
	VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &m_renderFinished;
	present.swapchainCount = 1;
	present.pSwapchains = &m_swapchain;
	present.pImageIndices = &imageIndex;
	const auto presentResult = vkQueuePresentKHR(m_context->presentQueue, &present);
	if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
		spdlog::error("Failed to present Vulkan frame: {}", static_cast<int>(presentResult));
}


void ax::Window::handle_render_pass(VkCommandBuffer commandBuffer, double delta)
{
	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, render_event);

		// Send out the draw event
		m_renderEventHandler.fire
		({
			.delta = delta,
			.commandBuffer = commandBuffer
		});
	}

	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, sprite_batch);

		// Group sprite records by texture so we can issue one instanced draw per texture.
		m_spriteGroupRanges.clear();
		for (auto* sprite : m_activeSprites)
		{
			if (sprite->tex == sprite->groupedTexture)
				continue;

			if (sprite->groupedTexture != nullptr)
			{
				auto& oldGroup = m_spriteGroups[sprite->groupedTexture];
				oldGroup.erase(std::remove(oldGroup.begin(), oldGroup.end(), sprite), oldGroup.end());
			}

			sprite->groupedTexture = sprite->tex;
			if (sprite->tex != nullptr)
				m_spriteGroups[sprite->tex].push_back(sprite);
		}

		for (auto& group : m_pendingSpriteGroups)
			group.second.clear();
		for (const auto& sprite : m_pendingTextures)
			if (sprite.tex != nullptr)
				m_pendingSpriteGroups[sprite.tex].push_back(&sprite);

		size_t spriteCount = 0;
		for (const auto& group : m_spriteGroups)
			spriteCount += group.second.size();
		for (const auto& group : m_pendingSpriteGroups)
			spriteCount += group.second.size();

		ensure_uniform_capacity(static_cast<uint32_t>(spriteCount));
		m_spriteGroupRanges.reserve(m_spriteGroups.size());
		m_spriteUploadData.reserve(spriteCount);

		uint32_t instanceCursor = 0;
		auto uploadGroup = [this, &instanceCursor](Texture* tex, const std::vector<const SpriteDefinition*>& activeSprites, const std::vector<const SpriteDefinition*>* pendingSprites)
		{
			m_spriteUploadData.clear();
			if (pendingSprites != nullptr)
				for (const auto* sprite : *pendingSprites)
					m_spriteUploadData.push_back(sprite->gpuData);
			for (const auto* sprite : activeSprites)
				m_spriteUploadData.push_back(sprite->gpuData);

			const uint32_t count = static_cast<uint32_t>(m_spriteUploadData.size());
			if (count == 0)
				return;

			m_spriteGroupRanges.push_back({ tex, instanceCursor, count });
			std::memcpy(static_cast<std::byte*>(m_spriteMapped) + static_cast<size_t>(instanceCursor) * m_uniformStride, m_spriteUploadData.data(), m_spriteUploadData.size() * m_uniformStride);
			instanceCursor += count;
		};

		for (const auto& group : m_spriteGroups)
		{
			auto pendingGroup = m_pendingSpriteGroups.find(group.first);
			uploadGroup(group.first, group.second, pendingGroup == m_pendingSpriteGroups.end() ? nullptr : &pendingGroup->second);
		}
		static const std::vector<const SpriteDefinition*> emptyGroup;
		for (const auto& group : m_pendingSpriteGroups)
		{
			if (m_spriteGroups.find(group.first) == m_spriteGroups.end())
				uploadGroup(group.first, emptyGroup, &group.second);
		}

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &m_viewportDescriptorSet, 0, nullptr);

		for (const auto& r : m_spriteGroupRanges)
		{
			auto it = m_textureBindGroups.find(r.tex);
			if (it == m_textureBindGroups.end())
				it = m_textureBindGroups.emplace(r.tex, setup_bind_groups(r.tex)).first;
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &it->second, 0, nullptr);
			vkCmdDraw(commandBuffer, 6, r.count, 0, r.start);
		}

		m_pendingTextures.clear();
	}

	{
		PROFILER_SEGMENT_SCOPED_SUB_LEVEL(frame, render, ui);

		render_gui(commandBuffer, delta);
	}
}

void ax::Window::render_gui(VkCommandBuffer commandBuffer, double delta)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();

	ImGui::NewFrame();
	{
		m_uiEventHandler.fire({ .delta = delta });

		// Render notifications
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f); 
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.00f));
		{
			ImGui::RenderNotifications();
		}
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(1);
	}
	ImGui::EndFrame();

	ImGui::Render();

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

