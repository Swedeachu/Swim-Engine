#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"

#include "Tools/ShaderCompiler/ShaderReflection.h"
#include "Engine/RuntimeShaderCatalog.h"
#include "Tools/ShaderCompiler/ShaderNativeBindings.h"
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#include "Tools/ShaderCompiler/ShaderTweaks.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
		std::vector<RuntimeTweak> Tweaks;
	};

	ShaderLibrary::ShaderLibrary(Swim::Rhi::Device& deviceValue, std::filesystem::path rootValue)
		: device(deviceValue), root(std::move(rootValue)),
		  bytecodeExtension(Swim::Rhi::ShaderBytecodeExtension(deviceValue.GetAdapterInfo().Api))
	{
		if (root.empty() || !std::filesystem::is_directory(root))
		{
			throw std::runtime_error("ShaderLibrary: shader directory '" + root.string() + "' does not exist");
		}

		for (const auto name : Internal::RuntimeProgramNames)
		{
			const std::string key(name);
			Register({ key, key + bytecodeExtension, key + ".reflection.json" });
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

	std::optional<RuntimeShaderDesc> ShaderLibrary::Find(std::string_view name) const
	{
		const auto* desc = Describe(name);
		return desc ? std::optional<RuntimeShaderDesc>(*desc) : std::nullopt;
	}

	void ShaderLibrary::Replace(RuntimeShaderDesc desc)
	{
		const std::string name = desc.Name;
		registrations.erase(name);
		discovered.erase(name);
		Invalidate(name);
		Register(std::move(desc));
	}

	bool ShaderLibrary::IsLoaded(std::string_view name) const
	{
		return loadedPrograms.contains(std::string(name));
	}

	const RuntimeShaderDesc* ShaderLibrary::Describe(std::string_view name) const
	{
		const std::string key(name);

		if (const auto found = registrations.find(key); found != registrations.end())
		{
			return &found->second;
		}

		if (const auto found = discovered.find(key); found != discovered.end())
		{
			return &found->second;
		}

		if (key.empty() || key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
		{
			return nullptr;
		}

		std::error_code error;
		RuntimeShaderDesc desc{ key, root / (key + bytecodeExtension), root / (key + ".reflection.json") };

		if (!std::filesystem::is_regular_file(desc.Bytecode, error) || !std::filesystem::is_regular_file(desc.Reflection, error))
		{
			return nullptr;
		}

		return &discovered.emplace(key, std::move(desc)).first->second;
	}

	bool ShaderLibrary::Contains(std::string_view name) const
	{
		const auto* desc = Describe(name);

		if (!desc)
		{
			return false;
		}

		std::error_code error;
		return std::filesystem::is_regular_file(desc->Bytecode, error) && std::filesystem::is_regular_file(desc->Reflection, error);
	}

	const ShaderLibrary::Loaded& ShaderLibrary::Load(std::string_view name) const
	{
		const std::string key(name);

		if (const auto cached = loadedPrograms.find(key); cached != loadedPrograms.end())
		{
			return *cached->second;
		}

		const auto* desc = Describe(key);

		if (!desc)
		{
			throw std::runtime_error("ShaderLibrary: unregistered program " + key);
		}

		const auto& bytecode = desc->Bytecode;
		const auto& reflectionPath = desc->Reflection;
		Loaded loaded;
		loaded.Bytes = ReadBytes(bytecode);
		std::ifstream reflectionFile(reflectionPath, std::ios::binary);

		if (!reflectionFile)
		{
			throw std::runtime_error("ShaderLibrary: cannot open " + reflectionPath.string());
		}

		std::ostringstream reflectionText;
		reflectionText << reflectionFile.rdbuf();
		const auto json = reflectionText.str();
		const auto reflection = Swim::ShaderCompiler::ParseSlangReflectionJson(json);

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

		// DXIL binds by register: <Name>.dxil.reflection.json places each reflected binding.
		if (bytecode.extension() == ".dxil")
		{
			auto nativePath = bytecode;
			nativePath += ".reflection.json";
			const auto registers = Swim::ShaderCompiler::LoadSlangReflectionJson(nativePath);

			if (!registers)
			{
				throw std::runtime_error("ShaderLibrary: " + std::string(name) + " register reflection: " + registers.Error);
			}

			auto natives = Swim::ShaderCompiler::BuildNativeBindings(reflection.Reflection, registers.Reflection);

			if (!natives)
			{
				throw std::runtime_error("ShaderLibrary: " + std::string(name) + " registers: " + natives.Error);
			}

			if (const auto error = Swim::ShaderCompiler::LoadNativeSizeQueries(nativePath, natives.Bindings); !error.empty())
			{
				throw std::runtime_error("ShaderLibrary: " + std::string(name) + " size queries: " + error);
			}

			loaded.Interface.NativeBindings = std::move(natives.Bindings);
			auto inputs = Swim::ShaderCompiler::LoadNativeVertexInputs(nativePath);

			if (!inputs)
			{
				throw std::runtime_error("ShaderLibrary: " + std::string(name) + " vertex inputs: " + inputs.Error);
			}

			loaded.Interface.NativeVertexInputs = std::move(inputs.Inputs);
		}

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

		for (auto& tweak : Swim::ShaderCompiler::FindSlangTweaks(json))
		{
			const auto type = tweak.ScalarType == "float32" ? RuntimeTweak::Kind::Float
				: tweak.ScalarType == "int32"				? RuntimeTweak::Kind::Int
															: RuntimeTweak::Kind::Uint;
			loaded.Tweaks.push_back({ std::move(tweak.Name), tweak.Offset, type, tweak.Min, tweak.Max });
		}

		const auto cached = std::make_shared<const Loaded>(std::move(loaded));
		loadedPrograms.emplace(key, cached);
		return *cached;
	}

	RuntimeComputeProgram ShaderLibrary::LoadCompute(std::string_view name) const
	{
		const auto& loaded = Load(name);
		const std::string label(name);
		const Swim::Rhi::ShaderStageArtifact stage{ Swim::Rhi::ShaderStageMask::Compute, Describe(name)->ComputeEntry,
			loaded.Bytes };
		RuntimeComputeProgram program;
		program.Program = device.CreateShaderProgram({ { &stage, 1 },
			{ loaded.Interface.DescriptorSchemas, loaded.Interface.PushConstants, loaded.Interface.ComputeThreadGroupSize,
				loaded.Interface.NativeBindings },
			label });

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
		program.Tweaks = loaded.Tweaks;
		return program;
	}

	RuntimeGraphicsProgram ShaderLibrary::LoadGraphics(
		std::string_view name, std::span<const Swim::Rhi::DescriptorSchemaDesc> explicitSpaces) const
	{
		const auto& loaded = Load(name);
		const std::string label(name);
		const std::array<Swim::Rhi::ShaderStageArtifact, 2> stages{
			{ { Swim::Rhi::ShaderStageMask::Vertex, Describe(name)->VertexEntry, loaded.Bytes },
				{ Swim::Rhi::ShaderStageMask::Fragment, Describe(name)->FragmentEntry, loaded.Bytes } }
		};
		RuntimeGraphicsProgram program;
		program.Program = device.CreateShaderProgram({ stages,
			{ loaded.Interface.DescriptorSchemas, loaded.Interface.PushConstants, {}, loaded.Interface.NativeBindings,
				loaded.Interface.NativeVertexInputs },
			label });

		if (!program.Program)
		{
			throw std::runtime_error("ShaderLibrary: cannot create graphics program " + label);
		}

		program.Layout = device.CreatePipelineLayout({ program.Program.get(), label, explicitSpaces });

		if (!program.Layout)
		{
			throw std::runtime_error("ShaderLibrary: cannot create the layout of " + label);
		}

		program.Bindings = loaded.Names;
		return program;
	}

	std::filesystem::path ShaderLibrary::FindRoot(
		const std::filesystem::path& executableDirectory, std::span<const std::filesystem::path> fallbacks)
	{
		std::error_code error;
		const auto holds = [&](const std::filesystem::path& directory)
		{
			return !directory.empty() && std::filesystem::is_regular_file(directory / "ForwardOpaque.reflection.json", error);
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
