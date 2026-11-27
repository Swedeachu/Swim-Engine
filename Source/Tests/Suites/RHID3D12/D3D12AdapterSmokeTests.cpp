#include "Tests/Suites/RHID3D12/D3D12Smoke.h"

#include <iostream>

namespace
{

	// Reports what the adapter's in-box runtime offers (shader model, barrier, mesh and
	// raytracing tiers) and creates and drains a device under the debug layer.
	void RunAdapterSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		using namespace Swim;
		auto graphics = Testing::CreateValidatedD3D12System(graphicsDesc);

		for (std::uint32_t index = 0; index < graphics->GetAdapterCount(); ++index)
		{
			const auto& info = graphics->GetAdapter(index).GetInfo();
			std::cerr << "[D3D12 adapter " << index << "] " << info.Name << " | driver " << info.DriverInfo << " | " << info.ApiVersion
					  << '\n';
		}

		auto& adapter = graphics->GetAdapter(0);
		SWIM_CHECK(adapter.GetInfo().Api == Rhi::GraphicsApi::D3D12);
		auto device = adapter.CreateDevice();
		SWIM_REQUIRE(device);
		SWIM_CHECK(device->GetAdapterInfo().Api == Rhi::GraphicsApi::D3D12);
		device->GetQueue(Rhi::QueueType::Graphics).WaitIdle();
	}

	[[maybe_unused]] const bool registered = []
	{
		if (Swim::Testing::D3D12SmokeEnabled())
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.D3D12.Smoke", "AdapterFeatureReport", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedD3D12Smoke(&RunAdapterSmoke);
				} });
		}

		return true;
	}();

} // namespace
