#pragma once

// stdlib
#include <memory>
#include <queue>

#include "vulkan_context.h"
#include "window.h"

// AxEng
#include "helpers.h"
#include "texture.h"
#include "script.h"
#include "resource_loader.h"
#include "log_timer.h"
#include "custom_type.h"

namespace ax
{
	class Application final
	{
	public:
		DISABLE_COPY_AND_MOVE(Application);

		static Application from_directory(std::string_view root);
		static Application from_embedded(EmbeddedResourceLayout&& layout);
		static Application from_zip(std::string_view zipFile);

		~Application();

		bool try_load(const std::vector<std::string>& args);
		void cleanup();
		
		ResourceLoader& loader() noexcept { return m_loader; }
		const ResourceLoader& loader() const noexcept { return m_loader; }

		bool has_window() const noexcept { return m_window != nullptr; }
		Window* window() noexcept { return m_window.get(); }
		const Window* window() const noexcept { return m_window.get(); }

		const TextureManager& textures() const noexcept { return m_textures; }
		TextureManager& textures() noexcept { return m_textures; }

		const lua::ScriptManager& scripts() const noexcept { return m_scripts; }
		lua::ScriptManager& scripts() noexcept { return m_scripts; }

		const sol::environment& debug_get_env(Badge<debug::View>) const noexcept { return m_env; }
		sol::environment& debug_get_env(Badge<debug::View>) noexcept { return m_env; }

		void set_headless() noexcept { m_create_window = false; }

	private:
		Application(ResourceLoader&& loader);

		const std::vector<std::string> m_args;

		bool init_window();
		void add_manifest_bindings(sol::state&);
		void add_application_bindings(sol::state&);
		std::unique_ptr<Window> m_window;

		ResourceLoader m_loader;
		lua::ScriptManager m_scripts;
		TextureManager m_textures;
		type::TypeGenerator m_typeGen{};
		sol::environment m_env;
		bool m_create_window{ true };

		std::string m_name{};
		lua::Script* m_entryPoint{};

		bool m_loaded{ false };
	};
}