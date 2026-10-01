#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Engine/RuntimeShaderCatalog.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace Engine
{

	namespace
	{

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
		std::vector<RuntimeBinding> Names;
	};

	ShaderLibrary::ShaderLibrary(Swim::Rhi::Device& deviceValue, std::filesystem::path rootValue)
		: device(deviceValue), root(std::move(rootValue))
	{
		if (root.empty() || !std::filesystem::is_directory(root))
		{
			throw std::runtime_error("ShaderLibrary: shader directory '" + root.string() + "' does not exist");
		}

		for (const auto name : Internal::RuntimeProgramNames)
		{
			const std::string key(name);
			Register({ key, key + ".spv", key + ".reflection.json" });
		}
	}

	void ShaderLibrary::Register(RuntimeShaderDesc desc)
	{
		if (desc.Name.empty() ||
			desc.Name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
			desc.Bytecode.empty() || desc.Reflection.empty() || registrations.contains(desc.Name))
		{
			throw std::invalid_argument("ShaderLibrary: registration needs a unique identifier and artifact paths");
		}

		if (desc.Bytecode.is_relative())
		{
			desc.Bytecode = root / desc.Bytecode;
		}

		if (desc.Reflection.is_relative())
		{
			desc.Reflection = root / desc.Reflection;
		}

		const std::string name = desc.Name;
		registrations.emplace(name, std::move(desc));
	}

	void ShaderLibrary::Invalidate(std::string_view name)
	{
		loadedPrograms.erase(std::string(name));
	}

	bool ShaderLibrary::Contains(std::string_view name) const
	{
		const auto found = registrations.find(std::string(name));

		if (found == registrations.end())
		{
			return false;
		}

		std::error_code error;
		return std::filesystem::is_regular_file(found->second.Bytecode, error) &&
			std::filesystem::is_regular_file(found->second.Reflection, error);
	}

	const ShaderLibrary::Loaded& ShaderLibrary::Load(std::string_view name) const
	{
		const std::string key(name);

		if (const auto cached = loadedPrograms.find(key); cached != loadedPrograms.end())
		{
			return *cached->second;
		}

		const auto found = registrations.find(key);

		if (found == registrations.end())
		{
			throw std::runtime_error("ShaderLibrary: unregistered program " + key);
		}

		const auto& spirv = found->second.Bytecode;
		const auto& reflectionPath = found->second.Reflection;
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
		// Name every descriptor the interface declares (global and entry-point parameters).
		const auto nameBinding = [&](const Swim::ShaderCompiler::ShaderBindingReflection& parameter)
		{
			if (!parameter.HasIndex || parameter.BindingKind != "descriptorTableSlot")
			{
				return; // Push constants and uniforms are not descriptors.
			}

			for (const auto& schema : loaded.Interface.DescriptorSchemas)
			{
				if (schema.Space != parameter.Space)
				{
					continue;
				}

				for (const auto& binding : schema.Bindings)
				{
					if (binding.Binding == parameter.Index)
					{
						loaded.Names.push_back(
							{ parameter.Name, schema.Space, binding.Binding, binding.Type, binding.StorageTextureFormat });
					}
				}
			}
		};

		for (const auto& parameter : reflection.Reflection.GlobalParameters)
		{
			nameBinding(parameter);
		}

		for (const auto& entry : reflection.Reflection.EntryPoints)
		{
			for (const auto& parameter : entry.Parameters)
			{
				nameBinding(parameter);
			}
		}

		const auto cached = std::make_shared<const Loaded>(std::move(loaded));
		loadedPrograms.emplace(key, cached);
		return *cached;
	}

	RuntimeComputeProgram ShaderLibrary::LoadCompute(std::string_view name) const
	{
		const auto& loaded = Load(name);
		const std::string label(name);
		const Swim::Rhi::ShaderStageArtifact stage{ Swim::Rhi::ShaderStageMask::Compute, registrations.at(std::string(name)).ComputeEntry,
			loaded.Bytes };
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
		program.Bindings = loaded.Names;
		program.ThreadGroupSize = loaded.Interface.ComputeThreadGroupSize;
		return program;
	}

	RuntimeGraphicsProgram ShaderLibrary::LoadGraphics(
		std::string_view name, std::span<const Swim::Rhi::DescriptorSchemaDesc> explicitSpaces) const
	{
		const auto& loaded = Load(name);
		const std::string label(name);
		const std::array<Swim::Rhi::ShaderStageArtifact, 2> stages{
			{ { Swim::Rhi::ShaderStageMask::Vertex, registrations.at(std::string(name)).VertexEntry, loaded.Bytes },
				{ Swim::Rhi::ShaderStageMask::Fragment, registrations.at(std::string(name)).FragmentEntry, loaded.Bytes } }
		};
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
		return Internal::RequiredProgramNames;
	}

} // namespace Engine
