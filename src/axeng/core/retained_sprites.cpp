#include "axeng/core/retained_sprites.h"

wgpu::Buffer ax::upload_static_sprites(const wgpu::Device& device, const SpriteBuffer& data, glm::vec2 atlasOffset)
{
	if (!data.sealed() || data.size() * SpriteGpuData::gpuDataSize > 65536)
		throw sol::error("Static sprite upload requires 1-1024 sealed records");
	wgpu::BufferDescriptor descriptor{};
	descriptor.size = data.size() * SpriteGpuData::gpuDataSize;
	descriptor.usage = wgpu::BufferUsage::Storage;
	descriptor.mappedAtCreation = true;
	descriptor.label = "RetainedSpriteBatch";
	auto buffer{ device.CreateBuffer(&descriptor) };
	auto mapped{ static_cast<SpriteGpuData*>(buffer.GetMappedRange()) };
	if (!mapped)
		throw std::runtime_error("Failed to map retained sprite batch");
	const auto& records{ data.data() };
	std::copy(records.begin(), records.end(), mapped);
	for (std::size_t i{ 0 }; i < records.size(); ++i)
	{
		mapped[i].region.x += atlasOffset.x;
		mapped[i].region.y += atlasOffset.y;
	}
	buffer.Unmap();
	return buffer;
}

void ax::SpriteGroup::set_visible(bool visible)
{
	check_thread();
	if (m_released)
		throw sol::error("Sprite group has been released");
	m_visible = visible;
}

void ax::SpriteGroup::check_thread() const
{
	if (m_thread != std::this_thread::get_id())
		throw sol::error("Sprite groups belong to the renderer thread");
}

void ax::StaticSpriteBatch::check_thread() const
{
	if (m_thread != std::this_thread::get_id())
		throw sol::error("Static batches belong to the renderer thread");
}

void ax::StaticSpriteBatch::set_visible(bool visible)
{
	check_thread();
	if (m_released)
		throw sol::error("Sprite batch has been released");
	m_visible = visible;
}

void ax::StaticSpriteBatch::set_staging(bool eligible, std::int64_t priority)
{
	check_thread();
	if (m_released)
		throw sol::error("Sprite batch has been released");
	m_stagingEligible = eligible;
	m_stagingPriority = priority;
}

std::shared_ptr<ax::StaticSpriteBatch> ax::next_static_sprite_batch(
	std::span<const std::shared_ptr<StaticSpriteBatch>> batches)
{
	std::shared_ptr<StaticSpriteBatch> next;
	for (const auto& batch : batches)
	{
		if (!batch->staging_eligible() || batch->ready() || !batch->error().empty())
			continue;
		if (!next || batch->staging_priority() < next->staging_priority())
			next = batch;
	}
	return next;
}

void ax::StaticSpriteBatch::release()
{
	check_thread();
	m_released = true;
	m_visible = false;
	m_stagingEligible = false;
	m_bindGroup = nullptr;
	m_buffer = nullptr;
	m_data.reset();
}
