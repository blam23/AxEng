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
	class Module final
	{
	public:
		DISABLE_COPY_AND_MOVE(Module);

		static Module from_directory(std::string_view root);

		~Module();

		bool try_load();
		
		IResourceLoader& loader() noexcept { return *m_loader; }
		const IResourceLoader& loader() const noexcept { return *m_loader; }

		bool has_window() const noexcept { return m_window != nullptr; }
		Window* window() noexcept { return m_window.get(); }
		const Window* window() const noexcept { return m_window.get(); }

	private:
		Module(std::unique_ptr<IResourceLoader> loader);

		bool init_window(const sol::environment& env);
		void add_manifest_bindings(sol::environment& env);

		std::unique_ptr<Window> m_window;

		std::unique_ptr<IResourceLoader> m_loader;
		ax::lua::ScriptManager m_scripts;
		ax::TextureManager m_textures;

		std::string m_name{};
		ax::lua::Script* m_entryPoint{};

		bool m_loaded{ false };
	};
}