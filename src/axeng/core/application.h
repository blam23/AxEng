#pragma once

// stdlib
#include <memory>
#include <queue>
#include <map>
#include <mutex>

// AxEng
#include "axeng/core/background_worker.h"
#include "axeng/core/custom_type.h"
#include "axeng/core/helpers.h"
#include "axeng/core/log_timer.h"
#include "axeng/core/lua/lua_engine.h"
#include "axeng/core/resource_loader.h"
#include "axeng/core/lua/script.h"
#include "axeng/core/lua/shared.h"
#include "axeng/core/texture.h"
#include "axeng/core/user_files.h"
#include "axeng/core/window.h"

// GFX
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

namespace ax
{
	class Application final
	{
	public:
		DISABLE_COPY_AND_MOVE(Application);

		static Application from_directory(flag_set<lua::Permission> permissions, std::string_view root);
		static Application from_embedded(flag_set<lua::Permission> permissions, EmbeddedResourceLayout&& layout);
		static Application from_zip(flag_set<lua::Permission> permissions, std::string_view zipFile);

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

		void call_deferred(std::function<void()>);

		std::string safe_directory_name() const noexcept { return m_directory_name; }

	private:
		Application(flag_set<lua::Permission> permissions, ResourceLoader&& loader);

		const std::vector<std::string> m_args;

		bool init_window();
		void add_manifest_bindings(sol::state&);
		void add_application_bindings(sol::state&);
		void add_thread_bindings(sol::state&, const std::vector<std::string>& args);
		std::unique_ptr<Window> m_window;

		ResourceLoader m_loader;
		lua::ScriptManager m_scripts;
		TextureManager m_textures;
		type::TypeGenerator m_typeGen{};
		sol::environment m_env;
		bool m_create_window{ true };

		std::string m_name{};
		std::string m_directory_name{};
		lua::Script* m_entryPoint{};
		Texture* m_atlasTexture{};

		bool m_loaded{ false };
		flag_set<lua::Permission> m_allowedPermissions;

		void initialise_background_worker(const std::vector<std::string>& args);
		BackgroundWorker m_backgroundWorker;
		std::mutex m_shared_mutex{};
		std::map<std::string, ax::lua::SharedObject> m_shared{};
		
		UserFileManager m_userFileManager;
	};
}