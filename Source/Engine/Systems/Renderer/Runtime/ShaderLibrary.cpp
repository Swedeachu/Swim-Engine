#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace Engine
{
	namespace
	{
		constexpr std::array<std::string_view, 36> RequiredProgramNames{ "Present", "SkyBackground", "GpuVisibility", "ClusterLightCull",
			"ClusterBounds", "ClusterAssign", "ClusterScan", "ForwardOpaque", "ForwardTransparent", "ForwardTransparentSort", "ShadowDepth",
			"ShadowMasked", "EnvironmentSky", "EnvironmentDownsample", "EnvironmentPrefilter", "EnvironmentIrradiance",
			"EnvironmentBrdfLut", "PostHistogram", "PostExposure", "PostBloomDownsample", "PostBloomUpsample", "PostComposite",
			"PostCompositeHdr", "TemporalResolve", "ScreenSpaceAo", "ScreenSpaceBlur", "ScreenSpaceComposite", "ScreenSpaceReflection",
			"ParticleSimulate", "ParticleEmit", "ParticleCompact", "ParticleFinalize", "ParticleRender", "Skinning", "UiQuad",
			"HzbReduce" };

		std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
			{
				throw std::runtime_error("ShaderLibrary: cannot open " + path.string());
			}
			const auto size = file.tellg();
			if (size <= 0)
			{
				throw std::runtime_error("ShaderLibrary: empty shader " + path.string());
			}
			std::vector<std::byte> bytes(static_cast<std::size_t>(size));
			file.seekg(0);
			file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!file)
			{
				throw std::runtime_error("ShaderLibrary: cannot read " + path.string());
			}
			return bytes;
		}
	} // namespace

	struct ShaderLibrary::Loaded
	{
		std::vector<std::byte> Bytes;
		Swim::Rhi::ShaderProgramInterface Interface;
	};

	ShaderLibrary::ShaderLibrary(Swim::Rhi::Device& deviceValue, std::filesystem::path rootValue)
		: device(deviceValue), root(std::move(rootValue))
	{
		if (root.empty() || !std::filesystem::is_directory(root))
		{
			throw std::runtime_error("ShaderLibrary: shader directory '" + root.string() + "' does not exist");
		}
	}

	bool ShaderLibrary::Contains(std::string_view name) const
	{
		std::error_code error;
		return std::filesystem::is_regular_file(root / (std::string(name) + ".spv"), error) &&
			std::filesystem::is_regular_file(root / (std::string(name) + ".reflection.json"), error);
	}

	ShaderLibrary::Loaded ShaderLibrary::Load(std::string_view name) const
	{
		const auto spirv = root / (std::string(name) + ".spv");
		const auto reflectionPath = root / (std::string(name) + ".reflection.json");
		Loaded loaded;
		loaded.Bytes = ReadBytes(spirv);
		const auto reflection = Swim::ShaderCompiler::LoadSlangReflectionJson(reflectionPath);
		if (!reflection)
		{
			throw std::runtime_error("ShaderLibrary: " + std::string(name) + " reflection: " + reflection.Error);
		}
		auto converted = Swim::ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		if (!converted)
		{
			throw std::runtime_error("ShaderLibrary: " + std::string(name) + " interface: " + converted.Error);
		}
		loaded.Interface = std::move(converted.Interface);
		return loaded;
	}

	RuntimeComputeProgram ShaderLibrary::LoadCompute(std::string_view name) const
	{
		const auto loaded = Load(name);
		const std::string label(name);
		const Swim::Rhi::ShaderStageArtifact stage{ Swim::Rhi::ShaderStageMask::Compute, "computeMain", loaded.Bytes };
		RuntimeComputeProgram program;
		program.Program = device.CreateShaderProgram({ { &stage, 1 },
			{ loaded.Interface.DescriptorSchemas, loaded.Interface.PushConstants, loaded.Interface.ComputeThreadGroupSize }, label });
		if (!program.Program)
		{
			throw std::runtime_error("ShaderLibrary: cannot create compute program " + label);
		}
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), label });
		if (!program.Layout)
		{
			throw std::runtime_error("ShaderLibrary: cannot create the layout of " + label);
		}
		program.Pipeline = device.CreateComputePipeline({ program.Program.get(), program.Layout.get(), {}, label });
		if (!program.Pipeline)
		{
			throw std::runtime_error("ShaderLibrary: cannot create the pipeline of " + label);
		}
		program.Space = loaded.Interface.DescriptorSchemas.empty() ? 0u : loaded.Interface.DescriptorSchemas.front().Space;
		return program;
	}

	RuntimeGraphicsProgram ShaderLibrary::LoadGraphics(
		std::string_view name, std::span<const Swim::Rhi::DescriptorSchemaDesc> explicitSpaces) const
	{
		const auto loaded = Load(name);
		const std::string label(name);
		const std::array<Swim::Rhi::ShaderStageArtifact, 2> stages{ { { Swim::Rhi::ShaderStageMask::Vertex, "vertexMain", loaded.Bytes },
			{ Swim::Rhi::ShaderStageMask::Fragment, "fragmentMain", loaded.Bytes } } };
		RuntimeGraphicsProgram program;
		program.Program =
			device.CreateShaderProgram({ stages, { loaded.Interface.DescriptorSchemas, loaded.Interface.PushConstants }, label });
		if (!program.Program)
		{
			throw std::runtime_error("ShaderLibrary: cannot create graphics program " + label);
		}
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), label, explicitSpaces });
		if (!program.Layout)
		{
			throw std::runtime_error("ShaderLibrary: cannot create the layout of " + label);
		}
		return program;
	}

	std::filesystem::path ShaderLibrary::FindRoot(
		const std::filesystem::path& executableDirectory, std::span<const std::filesystem::path> fallbacks)
	{
		std::error_code error;
		const auto holds = [&](const std::filesystem::path& directory)
		{
			return !directory.empty() && std::filesystem::is_regular_file(directory / "ForwardOpaque.spv", error);
		};
		if (const char* overridden = std::getenv("SWIM_SHADER_DIR"); overridden && *overridden)
		{
			return holds(overridden) ? std::filesystem::path(overridden) : std::filesystem::path{};
		}
		if (const auto deployed = executableDirectory / "Shaders" / "Runtime"; holds(deployed))
		{
			return deployed;
		}
		for (const auto& fallback : fallbacks)
		{
			if (holds(fallback))
			{
				return fallback;
			}
		}
		return {};
	}

	std::span<const std::string_view> ShaderLibrary::RequiredPrograms()
	{
		return RequiredProgramNames;
	}
} // namespace Engine
