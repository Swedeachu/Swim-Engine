#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{
	// A reflected descriptor of a runtime program, by its Slang parameter name.
	struct RuntimeBinding
	{
		std::string Name;
		std::uint32_t Space = 0;
		std::uint32_t Binding = 0;
		Swim::Rhi::DescriptorType Type = Swim::Rhi::DescriptorType::SampledTexture;
		Swim::Rhi::Format StorageFormat = Swim::Rhi::Format::Undefined; // StorageTexture only.
	};

	// A compiled compute program and the objects it owns.
	struct RuntimeComputeProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
		std::unique_ptr<Swim::Rhi::ComputePipeline> Pipeline;
		std::uint32_t Space = 0;			  // The first reflected descriptor space.
		std::vector<RuntimeBinding> Bindings; // Every descriptor, named (render features bind by name).
		std::array<std::uint32_t, 3> ThreadGroupSize{ 1, 1, 1 };
	};

	// A compiled vertex + fragment program and its layout; pipelines are created by the
	// caller (formats and state belong to the subsystem that draws with it).
	struct RuntimeGraphicsProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
	};

	// Loads the engine's precompiled Slang programs at runtime (Phase 23): each program is
	// <root>/<Name>.spv plus its <Name>.reflection.json sidecar, produced at build time by
	// swim_add_slang_program and deployed next to the executable (Shaders/Runtime). The
	// reflection is converted to the RHI program interface with the ShaderCompiler tool
	// library; entry points are the Slang conventions vertexMain / fragmentMain /
	// computeMain. Failures throw std::runtime_error naming the program and file.
	class ShaderLibrary
	{
	  public:
		ShaderLibrary(Swim::Rhi::Device& device, std::filesystem::path root);

		const std::filesystem::path& GetRoot() const { return root; }

		bool Contains(std::string_view name) const;

		RuntimeComputeProgram LoadCompute(std::string_view name) const;
		// explicitSpaces replaces reflected spaces (the shared bindless space).
		RuntimeGraphicsProgram LoadGraphics(
			std::string_view name, std::span<const Swim::Rhi::DescriptorSchemaDesc> explicitSpaces = {}) const;

		// The directory holding the runtime programs: SWIM_SHADER_DIR when set, else the
		// first of <exe dir>/Shaders/Runtime and the given fallbacks that holds
		// ForwardOpaque.spv. Empty when none does.
		static std::filesystem::path FindRoot(
			const std::filesystem::path& executableDirectory, std::span<const std::filesystem::path> fallbacks = {});

		// Every program the FrameRenderer needs (for diagnostics and tests).
		static std::span<const std::string_view> RequiredPrograms();

	  private:
		struct Loaded;
		Loaded Load(std::string_view name) const;

		Swim::Rhi::Device& device;
		std::filesystem::path root;
	};
} // namespace Engine
