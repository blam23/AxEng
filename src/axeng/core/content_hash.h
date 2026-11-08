#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace ax
{
	// A stable 64-bit hash of generated content, for keying caches on disk. The value only depends on what is added
	// and in what order, never on the platform or the run, so it can be compared across launches. Not cryptographic.
	class ContentHash
	{
	public:
		ContentHash& bytes(const void* data, std::size_t size) noexcept
		{
			const auto* values{ static_cast<const unsigned char*>(data) };
			for (std::size_t i{}; i < size; ++i)
			{
				m_state ^= values[i];
				m_state *= 0x100000001b3ull;
			}
			return *this;
		}

		// Integers, enums and floats by value. Floats hash their bit pattern, so equal values from the same code hash
		// equally.
		template<typename T>
			requires std::is_arithmetic_v<T> || std::is_enum_v<T>
		ContentHash& add(T value) noexcept
		{
			if constexpr (std::is_same_v<T, bool>)
				return add(static_cast<std::uint8_t>(value ? 1 : 0));
			else if constexpr (std::is_enum_v<T>)
				return add(static_cast<std::underlying_type_t<T>>(value));
			else if constexpr (std::is_floating_point_v<T>)
			{
				if (value == T{}) value = T{}; // -0 and 0 hash alike
				return bytes(&value, sizeof(value));
			}
			else
			{
				const auto widened{ static_cast<std::uint64_t>(value) };
				return bytes(&widened, sizeof(widened));
			}
		}

		// Length-prefixed, so consecutive strings cannot run together.
		ContentHash& add(std::string_view text) noexcept
		{
			add(static_cast<std::uint64_t>(text.size()));
			return bytes(text.data(), text.size());
		}

		ContentHash& add(const char* text) noexcept { return add(std::string_view{ text }); }

		template<typename T>
			requires std::is_arithmetic_v<T>
		ContentHash& add_span(std::span<const T> values) noexcept
		{
			add(static_cast<std::uint64_t>(values.size()));
			return bytes(values.data(), values.size_bytes());
		}

		std::uint64_t value() const noexcept
		{
			auto value{ m_state };
			value ^= value >> 30u; value *= 0xbf58476d1ce4e5b9ull;
			value ^= value >> 27u; value *= 0x94d049bb133111ebull;
			value ^= value >> 31u;
			return value;
		}

	private:
		std::uint64_t m_state{ 0xcbf29ce484222325ull };
	};
}
