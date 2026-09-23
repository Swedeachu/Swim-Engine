#pragma once

#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <memory>

namespace Swim::Testing
{

	// A GpuScene on the mock device (host-backed buffers whose copies execute)
	// plus a graph executor, so uploads land in inspectable bytes.
	struct GpuSceneFixture
	{
		explicit GpuSceneFixture(std::uint32_t maxObjects = 64)
		{
			Render::GpuSceneDesc desc;
			desc.MaxObjects = maxObjects;
			desc.DebugName = "Test scene";
			scene = std::make_unique<Render::GpuScene>(device, desc);
			executor = std::make_unique<Render::RenderGraphExecutor>(device);
		}

		// Imports, executes, commits and waits. Returns what the import declared.
		Render::GpuSceneGraphResources Upload()
		{
			Render::RenderGraph graph;
			auto resources = scene->Import(graph);
			executor->Execute(graph.Compile());
			scene->CommitUploads();
			executor->Wait();
			return resources;
		}

		Render::GpuInstanceRecord GpuInstance(std::uint32_t row) const
		{
			Render::GpuInstanceRecord record;
			std::memcpy(&record, Bytes(scene->GetInstanceBuffer()).data() + std::size_t(row) * sizeof(record), sizeof(record));
			return record;
		}

		Render::GpuTransformRecord GpuTransform(std::uint32_t row) const
		{
			Render::GpuTransformRecord record;
			std::memcpy(&record, Bytes(scene->GetTransformBuffer()).data() + std::size_t(row) * sizeof(record), sizeof(record));
			return record;
		}

		static const std::vector<std::byte>& Bytes(Rhi::Buffer& buffer) { return static_cast<MockMappedBuffer&>(buffer).Bytes; }

		// Every row below RowCount on the "GPU" equals the CPU mirror.
		bool GpuMatchesMirror() const
		{
			const auto rows = scene->GetStats().RowCount;
			for (std::uint32_t row = 0; row < rows; ++row)
			{
				const auto instance = GpuInstance(row);
				const auto transform = GpuTransform(row);
				if (std::memcmp(&instance, &scene->GetInstanceRow(row), sizeof(instance)) != 0 ||
					std::memcmp(&transform, &scene->GetTransformRow(row), sizeof(transform)) != 0)
				{
					return false;
				}
			}
			return true;
		}

		std::size_t CopyCount() const
		{
			std::size_t copies = 0;
			for (const auto& command : *device.Commands)
			{
				copies += command.Kind == "CopyBuffer";
			}
			return copies;
		}

		MockDevice device;
		std::unique_ptr<Render::GpuScene> scene;
		std::unique_ptr<Render::RenderGraphExecutor> executor;
	};

} // namespace Swim::Testing
