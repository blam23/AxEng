#pragma once

#include "axeng/core/forward.h"
#include "axeng/core/application.h"
#include "axeng/core/native_application_module.h"
#include "axeng/core/resource_loader.h"
#include "axeng/core/log_timer.h"
#include "axeng/core/lua/lua_engine.h"
#include "axeng/core/lua/bindings/bind_all.h"
#include "axeng/core/lua/script.h"
#include "axeng/core/camera.h"
#include "axeng/core/window.h"
#include "axeng/core/error.h"
#include "axeng/core/input/keyboard.h"

namespace ax
{
	void init();
	void teardown();

	ax::Error run
	(
		const std::vector<std::string>& args,
		Application&&,
		wgpu::BackendType backendType,
		const std::filesystem::path& userDirectory = {},
		const std::optional<audio::AudioSystem::Config>& audioConfig = std::nullopt,
		bool backgroundWindow = false
	);
	ax::Error run_from_directory
	(
		flag_set<lua::Permission> permissions,
		const std::vector<std::string>& args,
		std::string_view rootDirectory,
		wgpu::BackendType backendType = wgpu::BackendType::Undefined,
		std::string_view sourceDirectory = {},
		std::unique_ptr<NativeApplicationModule> nativeModule = {},
		std::filesystem::path userDirectory = {},
		std::optional<audio::AudioSystem::Config> audioConfig = std::nullopt,
			bool backgroundWindow = false
	);
	ax::Error run_from_zip
	(
		flag_set<lua::Permission> permissions,
		const std::vector<std::string>& args,
		std::string_view zipFile,
		wgpu::BackendType backendType = wgpu::BackendType::Undefined,
		std::unique_ptr<NativeApplicationModule> nativeModule = {},
		std::filesystem::path userDirectory = {},
		std::optional<audio::AudioSystem::Config> audioConfig = std::nullopt,
			bool backgroundWindow = false
	);
}
