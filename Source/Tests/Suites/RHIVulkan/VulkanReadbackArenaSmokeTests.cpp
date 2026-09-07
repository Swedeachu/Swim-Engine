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

	void RunReadbackArenaSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Readback arena smoke requires a working SDL desktop video driver");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		SWIM_REQUIRE(graphics->IsValidationEnabled());
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		std::array<std::unique_ptr<Rhi::ReadbackArena>, 2> arenas;
		std::array<std::unique_ptr<Rhi::Texture>, 2> textures;
		std::array<Rhi::ReadbackSlice, 2> bufferResults;
		std::array<Rhi::ReadbackSlice, 2> textureResults;
		std::array<std::array<std::byte, 64>, 2> expected{};
		for (std::size_t slot = 0; slot < arenas.size(); ++slot)
		{
			arenas[slot] = Rhi::ReadbackArena::Create(*device, { 128 });
			Rhi::TextureDesc desc;
			desc.Extent = { 4, 4, 1 };
			desc.PixelFormat = Rhi::Format::RGBA8Unorm;
			desc.Usage = Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
			textures[slot] = device->CreateTexture(desc);
			SWIM_REQUIRE(arenas[slot] && textures[slot]);
		}
		auto checkResults = [&](std::size_t slot)
		{
			std::array<std::byte, 32> bytes{};
			SWIM_REQUIRE_EQUAL(arenas[slot]->TryRead(bufferResults[slot], bytes), Rhi::ReadbackStatus::Ready);
			SWIM_CHECK(std::equal(bytes.begin(), bytes.end(), expected[slot].begin()));
			std::span<const std::byte> pixels;
			SWIM_REQUIRE_EQUAL(arenas[slot]->TryGetData(textureResults[slot], pixels), Rhi::ReadbackStatus::Ready);
			SWIM_REQUIRE_EQUAL(pixels.size(), expected[slot].size());
			SWIM_CHECK(std::equal(pixels.begin(), pixels.end(), expected[slot].begin()));
		};

		// Declared last: GPU work drains before textures/arenas unwind on failure.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2, { 128 } });
		SWIM_REQUIRE(frames);
		for (unsigned iteration = 0; iteration < 6; ++iteration)
		{
			const auto slot = frames->BeginFrame().Index;
			if (iteration >= 2)
			{
				// Beginning this frame has reused commands/uploads. The old CPU
				// results must still be available until we explicitly discard them.
				checkResults(slot);
				SWIM_REQUIRE(arenas[slot]->TryReset());
				SWIM_CHECK_THROWS(bufferResults[slot].GetBuffer(), std::invalid_argument);
			}
			for (std::size_t byte = 0; byte < expected[slot].size(); ++byte)
			{
				expected[slot][byte] = static_cast<std::byte>((iteration * 41 + byte * 13) & 255);
			}
			SWIM_REQUIRE(frames->AllocateUpload(3));
			auto uploadBuffer = frames->WriteUpload(std::span(expected[slot]).first(32), 16);
			auto uploadTexture = frames->WriteUpload(expected[slot], 64);
			SWIM_REQUIRE(uploadBuffer && uploadTexture);
			SWIM_REQUIRE(arenas[slot]->Allocate(3));
			auto buffer = arenas[slot]->Allocate(32, 16);
			auto texture = arenas[slot]->Allocate(64, 64);
			SWIM_REQUIRE(buffer && texture);
			SWIM_CHECK_EQUAL(buffer->GetOffset(), 16ull);
			SWIM_CHECK_EQUAL(texture->GetOffset(), 64ull);
			SWIM_CHECK(!arenas[slot]->Allocate(1));
			bufferResults[slot] = *buffer;
			textureResults[slot] = *texture;
			std::span<const std::byte> unavailable;
			SWIM_CHECK_EQUAL(arenas[slot]->TryGetData(*buffer, unavailable), Rhi::ReadbackStatus::NotSubmitted);
			SWIM_CHECK(unavailable.empty());

			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.BeginDebugLabel("Persistent upload and readback arena round trip");
			commands.Transition(*uploadBuffer->Resource, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			commands.Transition(buffer->GetBuffer(), iteration < 2 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead,
				Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*uploadBuffer->Resource, buffer->GetBuffer(), { uploadBuffer->Offset, buffer->GetOffset(), buffer->GetSize() });
			commands.Transition(*textures[slot], iteration < 2 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource,
				Rhi::ResourceState::CopyDestination);
			Rhi::BufferTextureCopyRegion copy;
			copy.BufferOffset = uploadTexture->Offset;
			copy.Extent = { 4, 4, 1 };
			commands.CopyBufferToTexture(*uploadTexture->Resource, *textures[slot], copy);
			commands.Transition(*textures[slot], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::CopySource);
			copy.BufferOffset = texture->GetOffset();
			commands.CopyTextureToBuffer(*textures[slot], texture->GetBuffer(), copy);
			commands.Transition(buffer->GetBuffer(), Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.EndDebugLabel();
			commands.End();
			std::array batches{ arenas[slot].get() };
			frames->SubmitCurrent(batches);
		}
		frames->Drain();
		checkResults(0);
		checkResults(1);
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ReadbackArenaBufferTextureAndRetainedResults",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunReadbackArenaSmoke); } });
		}
		return true;
	}();

} // namespace
