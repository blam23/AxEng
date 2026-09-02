#pragma once

#include <memory>
#include <map>
#include <functional>
#include <string>
#include <mutex>

#include "helpers.h"
#include "asset.h"
#include "resource_loader.h"

namespace ax
{
	/// <summary>
	/// For managing loadable assets such as shaders, scripts and textures
	/// </summary>
	/// <typeparam name="TAsset">The type of asset to be managed, such as Texture or Script</typeparam>
	template <typename TAsset>
		requires ValidAsset<TAsset>
	class AssetManager
	{
	public:
		AssetManager(IResourceLoader& loader)
			: m_loader{ loader }
		{
		}
		
		TAsset* load(const std::string& name, const TAsset::Descriptor& description);
		TAsset* get(const std::string& name);
		void mark_for_removal(const std::string& name);
		void for_each(std::function<void(const std::string& name, TAsset const*)> func);
		void for_each_name(std::function<void(const std::string& name)> func);
		void unload_all();

	protected:
		virtual std::unique_ptr<TAsset> inner_load(const std::string& name, const TAsset::Descriptor& description) = 0;
		std::map<std::string, std::pair<typename TAsset::Descriptor, std::unique_ptr<TAsset>>> m_store{};
		std::recursive_mutex m_loadMutex{};
		IResourceLoader& m_loader;
	};

	template<typename TAsset>
		requires ValidAsset<TAsset>
	TAsset* AssetManager<TAsset>::load(
		const std::string& name,
		const TAsset::Descriptor& description
	)
	{
		std::lock_guard lock{ m_loadMutex };

		// If the asset already is loaded, return it
		const auto itr{ m_store.find(name) };
		if (itr != m_store.end())
			return itr->second.second.get();

		// Try and load the asset
		auto asset{ inner_load(name, description) };

		// Only emplace if the asset was loaded successfully
		if (asset && asset->is_loaded())
		{
			const auto [idx, success] = m_store.try_emplace(name, std::pair(description, std::move(asset)));

			if (success)
				return idx->second.second.get();
		}

		return nullptr;
	}

	template<typename TAsset>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset>::for_each(std::function<void(const std::string& name, TAsset const*)> func)
	{
		std::lock_guard lock{ m_loadMutex };

		for (auto& [name, entry] : m_store)
			func(name, entry.second.get());
	}

	template<typename TAsset>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset>::unload_all()
	{
		std::lock_guard lock{ m_loadMutex };
		m_store.clear();
	}

	template<typename TAsset>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset>::for_each_name(std::function<void(const std::string& name)> func)
	{
		std::lock_guard lock{ m_loadMutex };

		for (auto& [name, _] : m_store)
			func(name);
	}

	template<typename TAsset>
		requires ValidAsset<TAsset>
	TAsset* AssetManager<TAsset>::get(const std::string& name)
	{
		std::lock_guard lock{ m_loadMutex };

		const auto& idx = m_store.find(name);
		return idx != m_store.end() ? idx->second.second.get() : nullptr;
	}

	template<typename TAsset>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset>::mark_for_removal(const std::string& name)
	{
		std::lock_guard lock{ m_loadMutex };

		const auto& idx = m_store.find(name);

		if (idx != m_store.end())
		{
			const auto ptr = idx->second.second.get();
			if (ptr)
			{
				ptr->tag_outdated();
				m_store.erase(idx);
			}
		}
	}
}