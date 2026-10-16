#include "gtest/gtest.h"

#include "axeng/comp/compiler.h"
#include "axeng/core/axeng.h"

#include "tests/main/log_capture.h"

#include <dawn/native/DawnNative.h>
#include <filesystem>
#include <string>
#include <utility>
#include <windows.h>

namespace
{
	bool has_d3d12_adapter()
	{
		char systemDirectory[MAX_PATH]{};
		const auto length{ ::GetSystemDirectoryA(systemDirectory, MAX_PATH) };
		if (length == 0)
			return false;

		const std::string systemPath{ std::string(systemDirectory, length) + '\\' };
		const char* searchPaths[]{ systemPath.c_str() };
		dawn::native::DawnInstanceDescriptor dawnDesc{};
		dawnDesc.additionalRuntimeSearchPathsCount = 1;
		dawnDesc.additionalRuntimeSearchPaths = searchPaths;

		static const auto timedWaitAny{ wgpu::InstanceFeatureName::TimedWaitAny };
		wgpu::InstanceDescriptor instanceDesc{};
		instanceDesc.nextInChain = &dawnDesc;
		instanceDesc.requiredFeatureCount = 1;
		instanceDesc.requiredFeatures = &timedWaitAny;
		auto instance{ wgpu::CreateInstance(&instanceDesc) };
		if (instance == nullptr)
			return false;

		wgpu::RequestAdapterOptions options{};
		options.backendType = wgpu::BackendType::D3D12;
		wgpu::Adapter adapter;
		instance.WaitAny(instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
			[&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView)
			{
				if (status == wgpu::RequestAdapterStatus::Success)
					adapter = std::move(result);
			}), UINT64_MAX);
		return adapter != nullptr;
	}
}

TEST(LuaTests, RunLuaTestSuite)
{
    // This kinda sucks but this value needs to be updated as we add and remove tests
    testlog::LogCapture::instance().expect_total_error_count(3);

    // Clean & compile lua tests
    ASSERT_EQ(ax::Error::Success, ax::comp::clean("compiled_lua_tests"));
    ASSERT_EQ
    (
        ax::Error::Success,
        ax::comp::compile("lua_tests", "compiled_lua_tests", false)
    );

    // Run ze tests
    ASSERT_EQ
    (
        ax::Error::Success,
        ax::run_from_directory({}, {}, "compiled_lua_tests")
    );
}

TEST(LuaTests, RunLuaRenderToSuite)
{
	if (!has_d3d12_adapter())
		GTEST_SKIP() << "No D3D12 WebGPU adapter is available";

	const auto sourceProjectPath
	{
		std::filesystem::temp_directory_path()
			/ ("AxEngLuaRenderToSource_" + std::to_string(::GetCurrentProcessId()))
	};
	std::filesystem::create_directories(sourceProjectPath / "scripts" / "lib");
	std::filesystem::create_directories(sourceProjectPath / "scripts" / "tests");

	const auto copyScript{ [&sourceProjectPath](const std::filesystem::path& source, const std::filesystem::path& destination)
	{
		return std::filesystem::copy_file(source, sourceProjectPath / destination,
			std::filesystem::copy_options::overwrite_existing);
	} };
	ASSERT_TRUE(copyScript("lua_tests/render_to/project.json", "project.json"));
	ASSERT_TRUE(copyScript("lua_tests/scripts/base_test.lua", "scripts/base_test.lua"));
	ASSERT_TRUE(copyScript("lua_tests/scripts/lib/class.lua", "scripts/lib/class.lua"));
	ASSERT_TRUE(copyScript("lua_tests/scripts/runner.lua", "scripts/runner.lua"));
	ASSERT_TRUE(copyScript("lua_tests/scripts/test_suite.lua", "scripts/test_suite.lua"));
	ASSERT_TRUE(copyScript("lua_tests/scripts/tests/render_to.lua", "scripts/tests/render_to.lua"));

	ASSERT_EQ(ax::Error::Success, ax::comp::clean("compiled_lua_render_to"));
	ASSERT_EQ
	(
		ax::Error::Success,
		ax::comp::compile(sourceProjectPath.string(), "compiled_lua_render_to", false)
	);
	std::filesystem::remove_all(sourceProjectPath);

	const auto compiledPath{ std::filesystem::absolute("compiled_lua_render_to") };
	const auto previousPath{ std::filesystem::current_path() };
	auto projectPath{ previousPath };
	while (!std::filesystem::exists(projectPath / "shaders" / "basic_shader.wgsl")
		&& projectPath.has_parent_path())
		projectPath = projectPath.parent_path();
	ASSERT_TRUE(std::filesystem::exists(projectPath / "shaders" / "basic_shader.wgsl"))
		<< "Unable to locate shaders/basic_shader.wgsl from " << previousPath.string();
	std::filesystem::current_path(projectPath);

	const auto result{ ax::run_from_directory({}, {}, compiledPath.string()) };
	std::filesystem::current_path(previousPath);
	EXPECT_EQ
	(
		ax::Error::Success,
		result
	);
}