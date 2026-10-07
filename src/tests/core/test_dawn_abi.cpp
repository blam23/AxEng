#include <gtest/gtest.h>
#include <dawn/native/DawnNative.h>
#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#if defined(_DEBUG)
static_assert(_ITERATOR_DEBUG_LEVEL == 2);
#else
static_assert(_ITERATOR_DEBUG_LEVEL == 0);
#endif

TEST(DawnAbiTests, NativeInstanceAndToggleLookup)
{
    dawn::native::Instance instance;
    ASSERT_NE(instance.Get(), nullptr);

    constexpr std::string_view toggleName = "disable_robustness";
    const auto* toggle = instance.GetToggleInfo(toggleName.data());
    ASSERT_NE(toggle, nullptr);
    EXPECT_EQ(std::string_view(toggle->name), toggleName);
    EXPECT_FALSE(std::string_view(toggle->description).empty());
    EXPECT_EQ(instance.GetToggleInfo("axeng_nonexistent_toggle"), nullptr);
}

TEST(DawnAbiTests, NativeAdapterVectorCrossesLibraryBoundary)
{
    dawn::native::Instance instance;
    ASSERT_NE(instance.Get(), nullptr);

    wgpu::RequestAdapterOptions options{};
    options.backendType = wgpu::BackendType::Null;
    auto adapters = instance.EnumerateAdapters(&options);
    ASSERT_FALSE(adapters.empty());
    std::vector<dawn::native::Adapter> copiedAdapters(adapters);
    EXPECT_EQ(copiedAdapters.size(), adapters.size());
    for (const auto& adapter : copiedAdapters)
        EXPECT_NE(adapter.Get(), nullptr);
}

TEST(DawnAbiTests, D3D12ShaderTranslation)
{
    char systemDirectory[MAX_PATH]{};
    const auto length = GetSystemDirectoryA(systemDirectory, MAX_PATH);
    ASSERT_GT(length, 0u);
    ASSERT_LT(length, static_cast<UINT>(MAX_PATH));
    const std::string systemPath = std::string(systemDirectory, length) + '\\';
    const char* searchPaths[]{systemPath.c_str()};
    dawn::native::DawnInstanceDescriptor dawnDescriptor{};
    dawnDescriptor.additionalRuntimeSearchPathsCount = 1;
    dawnDescriptor.additionalRuntimeSearchPaths = searchPaths;
    wgpu::InstanceDescriptor descriptor{};
    descriptor.nextInChain = &dawnDescriptor;
    dawn::native::Instance instance(&descriptor);

    wgpu::RequestAdapterOptions options{};
    options.backendType = wgpu::BackendType::D3D12;
    auto adapters = instance.EnumerateAdapters(&options);
    if (adapters.empty())
        GTEST_SKIP() << "No D3D12 adapter is available";

    wgpu::DeviceDescriptor deviceDescriptor{};
    deviceDescriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
            ADD_FAILURE() << std::string_view(message.data, message.length);
        });
    auto device = wgpu::Device::Acquire(adapters.front().CreateDevice(&deviceDescriptor));
    ASSERT_NE(device, nullptr);

    wgpu::ShaderSourceWGSL source{};
    source.code = "@compute @workgroup_size(1) fn dawn_entry_point() {}";
    wgpu::ShaderModuleDescriptor shaderDescriptor{};
    shaderDescriptor.nextInChain = &source;
    const auto shader = device.CreateShaderModule(&shaderDescriptor);
    ASSERT_NE(shader, nullptr);

    wgpu::ComputePipelineDescriptor pipelineDescriptor{};
    pipelineDescriptor.compute.module = shader;
    pipelineDescriptor.compute.entryPoint = "dawn_entry_point";
    const auto pipeline = device.CreateComputePipeline(&pipelineDescriptor);
    EXPECT_NE(pipeline, nullptr);
}
