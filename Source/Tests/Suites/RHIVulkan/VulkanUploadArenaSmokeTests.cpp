#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cstdlib>
#include <string_view>

namespace
{

	void RunUploadArenaSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Upload arena smoke requires a working SDL desktop video driver");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		constexpr std::size_t iterations = 6;
		std::array<std::unique_ptr<Rhi::Buffer>, iterations> readbacks;
		std::array<std::unique_ptr<Rhi::Texture>, iterations> textures;
		std::array<std::array<std::byte, 64>, iterations> expected{};
		for (std::size_t index = 0; index < iterations; ++index)
		{
			readbacks[index] = device->CreateBuffer({ 128, Rhi::BufferUsage::TransferDestination,
				Rhi::MemoryPreference::GpuToCpu, "Upload smoke readback" });
			Rhi::TextureDesc desc{};
			desc.Extent = { 4, 4, 1 };
			desc.PixelFormat = Rhi::Format::RGBA8Unorm;
			desc.Usage = Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
			textures[index] = device->CreateTexture(desc);
			SWIM_REQUIRE(readbacks[index] && textures[index]);
			for (std::size_t byte = 0; byte < expected[index].size(); ++byte)
			{
				expected[index][byte] = static_cast<std::byte>((index * 43 + byte * 7) & 255);
			}
		}

		// Last owner drains GPU work before destinations unwind, including failure.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2, { 128 } });
		SWIM_REQUIRE(frames);
		std::array<Rhi::Buffer*, 2> slotBuffers{};
		frames->BeginFrame();
		frames->CancelFrame();
		SWIM_CHECK_EQUAL(frames->GetLastSubmittedValue(), 0ull);
		for (std::size_t index = 0; index < iterations; ++index)
		{
			const auto slot = frames->BeginFrame().Index;
			SWIM_REQUIRE(frames->AllocateUpload(3));
			auto bufferSlice = frames->WriteUpload(std::span(expected[index]).first(32), 16);
			auto textureSlice = frames->AllocateUpload(64, 64);
			SWIM_REQUIRE(bufferSlice && textureSlice);
			std::copy(expected[index].begin(), expected[index].end(), textureSlice->Bytes.begin());
			SWIM_CHECK_EQUAL(bufferSlice->Offset, 16ull);
			SWIM_CHECK_EQUAL(textureSlice->Offset, 64ull);
			SWIM_CHECK(!frames->AllocateUpload(1));
			if (slotBuffers[slot] != nullptr)
			{
				SWIM_CHECK(slotBuffers[slot] == bufferSlice->Resource);
			}
			slotBuffers[slot] = bufferSlice->Resource;

			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.BeginDebugLabel("Upload arena buffer and texture copies");
			commands.Transition(*bufferSlice->Resource, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			commands.Transition(*readbacks[index], Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*bufferSlice->Resource, *readbacks[index], { bufferSlice->Offset, 0, 32 });
			commands.Transition(*textures[index], Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination);
			Rhi::BufferTextureCopyRegion copy{};
			copy.BufferOffset = textureSlice->Offset;
			copy.Extent = { 4, 4, 1 };
			commands.CopyBufferToTexture(*textureSlice->Resource, *textures[index], copy);
			commands.Transition(*textures[index], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::CopySource);
			copy.BufferOffset = 64;
			commands.CopyTextureToBuffer(*textures[index], *readbacks[index], copy);
			commands.Transition(*readbacks[index], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.EndDebugLabel();
			commands.End();
			frames->SubmitCurrent();
		}
		frames->Drain();
		for (std::size_t index = 0; index < iterations; ++index)
		{
			std::array<std::byte, 128> actual{};
			readbacks[index]->Read(0, actual);
			SWIM_CHECK(std::equal(expected[index].begin(), expected[index].begin() + 32, actual.begin()));
			SWIM_CHECK(std::equal(expected[index].begin(), expected[index].end(), actual.begin() + 64));
		}
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "UploadArenaBufferTextureAndSlotReuse",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunUploadArenaSmoke); } });
		}
		return true;
	}();

} // namespace
