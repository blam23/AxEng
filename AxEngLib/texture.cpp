#include "texture.h"
#include <memory>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "spdlog/spdlog.h"
#include <imgui_impl_vulkan.h>

ax::Texture::Texture(Badge<TextureManager>, const std::string& name, const std::vector<uint8_t> imageData, std::shared_ptr<VulkanContext> context)
	: Asset{ name }, m_context{ std::move(context) }
{
	int iwidth{}, iheight{};
	std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image
	{
		stbi_load_from_memory(imageData.data(), static_cast<int>(imageData.size()), &iwidth, &iheight, nullptr, 4),
		stbi_image_free
	};
	if (!image || !m_context || m_context->device == VK_NULL_HANDLE)
	{
		spdlog::error("Failed to load texture '{}': invalid image or Vulkan device", name);
		return;
	}

	m_width = static_cast<uint32_t>(iwidth);
	m_height = static_cast<uint32_t>(iheight);
	VkBuffer stagingBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory stagingMemory{ VK_NULL_HANDLE };
	try
	{
		const VkDeviceSize imageSize = static_cast<VkDeviceSize>(m_width) * m_height * 4;
		m_context->create_buffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);
		void* mapped{};
		if (vkMapMemory(m_context->device, stagingMemory, 0, imageSize, 0, &mapped) != VK_SUCCESS)
			throw std::runtime_error("Failed to map texture staging buffer");
		std::memcpy(mapped, image.get(), static_cast<size_t>(imageSize));
		vkUnmapMemory(m_context->device, stagingMemory);

		m_context->create_image(m_width, m_height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_image, m_imageMemory);
		auto commandBuffer = m_context->begin_single_time_commands();
		VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.image = m_image;
		toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
		VkBufferImageCopy copy{};
		copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageExtent = { m_width, m_height, 1 };
		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		VkImageMemoryBarrier toShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader.image = m_image;
		toShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
		m_context->end_single_time_commands(commandBuffer);

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(m_context->device, &viewInfo, nullptr, &m_view) != VK_SUCCESS)
			throw std::runtime_error("Failed to create texture image view");
		m_stbiPtr = image.release();
		m_loaded = true;
	}
	catch (const std::exception& e)
	{
		spdlog::error("Failed to create texture '{}': {}", name, e.what());
		if (m_view != VK_NULL_HANDLE)
			vkDestroyImageView(m_context->device, m_view, nullptr);
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(m_context->device, m_image, nullptr);
		if (m_imageMemory != VK_NULL_HANDLE)
			vkFreeMemory(m_context->device, m_imageMemory, nullptr);
		m_view = VK_NULL_HANDLE;
		m_image = VK_NULL_HANDLE;
		m_imageMemory = VK_NULL_HANDLE;
	}
	if (stagingBuffer != VK_NULL_HANDLE)
		vkDestroyBuffer(m_context->device, stagingBuffer, nullptr);
	if (stagingMemory != VK_NULL_HANDLE)
		vkFreeMemory(m_context->device, stagingMemory, nullptr);
}

ax::Texture::~Texture()
{
	if (m_imguiDescriptor != VK_NULL_HANDLE && ImGui::GetCurrentContext())
		ImGui_ImplVulkan_RemoveTexture(m_imguiDescriptor);
	if (m_context && m_context->device != VK_NULL_HANDLE)
	{
		if (m_view != VK_NULL_HANDLE)
			vkDestroyImageView(m_context->device, m_view, nullptr);
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(m_context->device, m_image, nullptr);
		if (m_imageMemory != VK_NULL_HANDLE)
			vkFreeMemory(m_context->device, m_imageMemory, nullptr);
	}
	if(m_stbiPtr)
		stbi_image_free(m_stbiPtr);
}

ImTextureID ax::Texture::imgui_view() const
{
	if (m_imguiDescriptor == VK_NULL_HANDLE && m_view != VK_NULL_HANDLE)
		m_imguiDescriptor = ImGui_ImplVulkan_AddTexture(m_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	return (ImTextureID)(intptr_t)m_imguiDescriptor;
}

GLFWimage ax::Texture::create_glfw_image() const
{
	return 
	{
		.width = static_cast<int>(m_width),
		.height = static_cast<int>(m_height),
		.pixels = (unsigned char*)m_stbiPtr,
	};
}

ax::TextureManager::TextureManager(Badge<Application> badge, ResourceLoader& loader)
	: AssetManager<Texture, TextureManager>{ badge, loader }
{
}

std::unique_ptr<ax::Texture> ax::TextureManager::load_from_raw_impl(const std::string& name, const std::vector<uint8_t>& data)
{
	if (!m_context)
	{
		spdlog::error("TextureManager Vulkan context not set.");
		return nullptr;
	}

	return std::make_unique<Texture>(Badge<TextureManager>{}, name, data, m_context);
}

std::unique_ptr<ax::Texture> ax::TextureManager::load_impl(const std::string& name, const Texture::Descriptor& description)
{
	if (!m_context)
	{
		spdlog::error("TextureManager Vulkan context not set.");
		return nullptr;
	}

	auto res{ Resource::load(m_loader, description) };

	if (res.has_value())
		return std::make_unique<Texture>(Badge<TextureManager>{}, name, res.value(), m_context);
	else
		return nullptr;
}

void ax::TextureManager::set_context(std::shared_ptr<VulkanContext> context)
{
	m_context = std::move(context);
}
