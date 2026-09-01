#pragma once

#include <string>
#include "helpers.h"

namespace ax
{
	class Asset
	{
	public:
		DISABLE_COPY(Asset);

		virtual ~Asset() = default;

		inline bool is_loaded() const { return m_loaded; }
		inline bool is_outdated() const { return m_outdated; }

		void tag_outdated() { m_outdated = true; }
		const std::string& asset_name() const { return m_name; }

	protected:
		Asset(const std::string& name);
		bool m_outdated{ false };
		bool m_loaded{ false };
		const std::string m_name;
	};

	template <typename T>
	concept ValidAsset = requires
	{
		typename T::Descriptor;
		std::derived_from<T, Asset>;
	};
}