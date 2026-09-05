#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/RhiFactory.h"

#include <cstdint>
#include <exception>
#include <memory>

namespace
{

	class TestGraphicsSystem final : public Swim::Rhi::GraphicsSystem
	{
	public:
		std::uint32_t GetAdapterCount() const override
		{
			return 0;
		}

		Swim::Rhi::Adapter& GetAdapter(std::uint32_t) override
		{
			std::terminate();
		}
	};

	std::unique_ptr<Swim::Rhi::GraphicsSystem> CreateTestGraphicsSystem()
	{
		return std::make_unique<TestGraphicsSystem>();
	}

}

SWIM_TEST("RHI.Factory", "RegistersCreatesAndRemovesBackendsExplicitly")
{
	Swim::Rhi::GraphicsFactory factory;

	SWIM_CHECK(!factory.IsAvailable(Swim::Rhi::GraphicsApi::Vulkan));
	SWIM_CHECK(factory.Register(Swim::Rhi::GraphicsApi::Vulkan, &CreateTestGraphicsSystem));
	SWIM_CHECK(factory.IsAvailable(Swim::Rhi::GraphicsApi::Vulkan));
	SWIM_CHECK(!factory.Register(Swim::Rhi::GraphicsApi::Vulkan, &CreateTestGraphicsSystem));

	auto graphics = factory.Create(Swim::Rhi::GraphicsApi::Vulkan);
	SWIM_REQUIRE(graphics != nullptr);
	SWIM_CHECK_EQUAL(graphics->GetAdapterCount(), std::uint32_t(0));

	SWIM_CHECK(factory.Unregister(Swim::Rhi::GraphicsApi::Vulkan));
	SWIM_CHECK(!factory.IsAvailable(Swim::Rhi::GraphicsApi::Vulkan));
	SWIM_CHECK(factory.Create(Swim::Rhi::GraphicsApi::Vulkan) == nullptr);
}

SWIM_TEST("RHI.Factory", "RejectsInvalidRegistration")
{
	Swim::Rhi::GraphicsFactory factory;
	SWIM_CHECK(!factory.Register(Swim::Rhi::GraphicsApi::Count, &CreateTestGraphicsSystem));
	SWIM_CHECK(!factory.Register(Swim::Rhi::GraphicsApi::D3D12, nullptr));
	SWIM_CHECK(!factory.Unregister(Swim::Rhi::GraphicsApi::Metal));
}
