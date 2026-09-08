#pragma once

// stdlib
#include <memory>
#include <queue>

// AxEng
#include "helpers.h"
#include "texture.h"
#include "lua_engine.h"
#include "script.h"
#include "window.h"
#include "resource_loader.h"
#include "log_timer.h"

// GFX
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

namespace ax
{
	class Application final
	{
	public:
		DISABLE_COPY_AND_MOVE(Application);

		static Application from_directory(std::string_view root);
		static Application from_embedded(EmbeddedResourceLayout&& layout);

		~Application();

		bool try_load();
		
		ResourceLoader& loader() noexcept { return m_loader; }
		const ResourceLoader& loader() const noexcept { return m_loader; }

		bool has_window() const noexcept { return m_window != nullptr; }
		Window* window() noexcept { return m_window.get(); }
		const Window* window() const noexcept { return m_window.get(); }

		const TextureManager& textures() const noexcept { return m_textures; }
		TextureManager& textures() noexcept { return m_textures; }

	private:
		Application(ResourceLoader&& loader);

		bool init_window(const sol::environment& env);
		void add_manifest_bindings(sol::environment& env);

		std::unique_ptr<Window> m_window;

		ResourceLoader m_loader;
		lua::ScriptManager m_scripts;
		TextureManager m_textures;

		std::string m_name{};
		lua::Script* m_entryPoint{};

		bool m_loaded{ false };
	};
}