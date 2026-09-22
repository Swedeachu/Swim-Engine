#pragma once

#include "Engine/Assets/AssetSystem.h"
#include "Engine/IO/AsyncIoService.h"
#include "Engine/Jobs/JobSystem.h"
#include "Engine/Platform/FileSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Framework/Test.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

namespace Swim::Testing
{

	// Full CPU stack for asset-residency tests: filesystem, jobs, async IO, the
	// AssetSystem (owned by this thread), a mock device with host-backed buffers
	// and textures, a GeometryHeap, TextureResidency and a RenderGraph executor.
	// Cooked objects written through WriteObject are resolved by AssetId.
	class ResidencyServiceFixture
	{
	  public:
		explicit ResidencyServiceFixture(const std::string& name, bool useJobs = true)
			: root(std::filesystem::temp_directory_path() / ("SwimResidency_" + name))
		{
			device.CreateTextures = true;
			fileSystemDesc.OrganizationName = "Swim Tests";
			fileSystemDesc.ApplicationName = "Residency";
			fileSystemDesc.UserDataRootOverride = root / "user";
			SWIM_REQUIRE(fileSystem.Initialize(fileSystemDesc));
			Jobs::JobSystemDesc jobDesc{};
			jobDesc.WorkerThreads = 2;
			jobDesc.BlockingThreads = 1;
			SWIM_REQUIRE(jobs.Initialize(jobDesc));
			SWIM_REQUIRE(io.Initialize(fileSystem, jobs));
			SWIM_REQUIRE(assets.Initialize());
			std::filesystem::create_directories(root);

			Render::GeometryHeapDesc heapDesc;
			heapDesc.VertexPageSize = 64 * 1024;
			heapDesc.IndexPageSize = 32 * 1024;
			heapDesc.MeshletPageSize = 16 * 1024;
			heapDesc.MaxMeshes = 64;
			heapDesc.MaxSubmeshes = 256;
			heapDesc.MaxPages = 16;
			geometry = std::make_unique<Render::GeometryHeap>(device, heapDesc);
			textures = std::make_unique<Render::TextureResidency>(device, Render::TextureResidencyDesc{ 16, "Test textures" });
			executor = std::make_unique<Render::RenderGraphExecutor>(device);
			useJobSystem = useJobs;
		}

		~ResidencyServiceFixture()
		{
			service.reset();
			executor.reset();
			textures.reset();
			geometry.reset();
			assets.Shutdown();
			io.Shutdown();
			jobs.Shutdown();
			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
		}

		ResidencyServiceFixture(const ResidencyServiceFixture&) = delete;
		ResidencyServiceFixture& operator=(const ResidencyServiceFixture&) = delete;

		Render::AssetResidencyService& Service(Render::AssetResidencyDesc desc = {})
		{
			if (!desc.ResolveCookedPath)
			{
				desc.ResolveCookedPath = [this](Assets::AssetId id)
				{
					const auto found = objects.find(id);
					return found == objects.end() ? std::filesystem::path{} : found->second;
				};
			}
			service = std::make_unique<Render::AssetResidencyService>(
				assets, io, useJobSystem ? &jobs : nullptr, *geometry, *textures, std::move(desc));
			return *service;
		}

		void WriteObject(Assets::AssetId id, const std::string& fileName, const std::vector<std::byte>& bytes)
		{
			const auto path = root / fileName;
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			SWIM_REQUIRE(static_cast<bool>(file));
			objects[id] = path;
		}

		void MapObject(Assets::AssetId id, std::filesystem::path path) { objects[id] = std::move(path); }

		// Pumps IO completions and service updates until the predicate holds.
		bool UpdateUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds(10))
		{
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			while (std::chrono::steady_clock::now() < deadline)
			{
				io.PumpCompletions();
				service->Update();
				if (done())
				{
					return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return false;
		}

		// Records pending uploads into one graph, executes and commits them, then
		// waits for completion and lets the service observe residency.
		Render::GpuResidencyGraphResources UploadFrame()
		{
			Render::RenderGraph graph;
			auto resources = service->Import(graph);
			const auto completion = executor->Execute(graph.Compile());
			service->CommitUploads(completion);
			executor->Wait();
			service->Update();
			return resources;
		}

		MockDevice device;
		Platform::FileSystem fileSystem;
		Platform::FileSystemDesc fileSystemDesc;
		Jobs::JobSystem jobs;
		IO::AsyncIoService io;
		Assets::AssetSystem assets;
		std::unique_ptr<Render::GeometryHeap> geometry;
		std::unique_ptr<Render::TextureResidency> textures;
		std::unique_ptr<Render::RenderGraphExecutor> executor;
		std::unique_ptr<Render::AssetResidencyService> service;
		std::unordered_map<Assets::AssetId, std::filesystem::path> objects;
		std::filesystem::path root;
		bool useJobSystem = true;
	};

} // namespace Swim::Testing
