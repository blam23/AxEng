#pragma once

#include <memory>
#include <map>
#include <functional>
#include <string>
#include <mutex>

#include "helpers.h"
#include "asset.h"
#include "resource_loader.h"
#include "forward.h"

namespace ax
{
	/// <summary>
	/// For managing loadable assets such as shaders, scripts and textures
	/// </summary>
	/// <typeparam name="TAsset">The type of asset to be managed, such as Texture or Script</typeparam>
	template <typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	class AssetManager
	{
	public:
		AssetManager(Badge<Application>, ResourceLoader& loader)
			: m_loader{ loader }
		{
		}
		~AssetManager() = default;
		
		TAsset* load(const std::string& name, const TAsset::Descriptor& description);
		TAsset* get(const std::string& name);
		const TAsset* get(const std::string& name) const;
		void for_each(std::function<void(const std::string& name, TAsset&)> func);
		void for_each_name(std::function<void(const std::string& name)> func);
		void unload_all(Badge<Application>);

	private:
		struct AssetStore
		{
			typename TAsset::Descriptor desc;
			std::unique_ptr<TAsset> asset;
		};

		std::unique_ptr<TAsset> load_impl(const std::string& name, const TAsset::Descriptor& description)
		{
			return static_cast<TDerived&>(*this).load_impl(name, description);
		}

		std::map<std::string, AssetStore> m_store{};
		std::recursive_mutex m_loadMutex{};
		ResourceLoader& m_loader;

		friend TDerived;
	};

	template<typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	TAsset* AssetManager<TAsset, TDerived>::load(
		const std::string& name,
		const TAsset::Descriptor& description
	)
	{
		std::lock_guard lock{ m_loadMutex };

		// If the asset already is loaded, return it
		const auto itr{ m_store.find(name) };
		if (itr != m_store.end())
			return itr->second.asset.get();

		// Try and load the asset
		auto asset{ load_impl(name, description) };

		// Only emplace if the asset was loaded successfully
		if (asset && asset->is_loaded())
		{
			const auto [idx, success] = m_store.try_emplace(name, AssetStore{ description, std::move(asset) });

			if (success)
				return idx->second.asset.get();
		}

		return nullptr;
	}

	template<typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset, TDerived>::for_each(std::function<void(const std::string& name, TAsset&)> func)
	{
		std::lock_guard lock{ m_loadMutex };

		for (auto& [name, entry] : m_store)
			func(name, *entry.asset);
	}

	template<typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset, TDerived>::unload_all(Badge<Application>)
	{
		std::lock_guard lock{ m_loadMutex };
		m_store.clear();
	}

	template<typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	void AssetManager<TAsset, TDerived>::for_each_name(std::function<void(const std::string& name)> func)
	{
		std::lock_guard lock{ m_loadMutex };

		for (auto& [name, _] : m_store)
			func(name);
	}

	template<typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	TAsset* AssetManager<TAsset, TDerived>::get(const std::string& name)
	{
		std::lock_guard lock{ m_loadMutex };

		const auto& idx = m_store.find(name);
		return idx != m_store.end() ? idx->second.asset.get() : nullptr;
	}

	template<typename TAsset, typename TDerived>
		requires ValidAsset<TAsset>
	const TAsset* AssetManager<TAsset, TDerived>::get(const std::string& name) const
	{
		std::lock_guard lock{ m_loadMutex };

		const auto& idx = m_store.find(name);
		return idx != m_store.end() ? idx->second.asset.get() : nullptr;
	}
}