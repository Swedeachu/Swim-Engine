#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::ShaderCompiler
{

	enum class ShaderStage : std::uint8_t
	{
		Unknown,
		Vertex,
		Fragment,
		Compute,
		Geometry,
		Hull,
		Domain,
		Mesh,
		Amplification,
		RayGeneration,
		Intersection,
		AnyHit,
		ClosestHit,
		Miss,
		Callable
	};

	struct ShaderBindingReflection
	{
		std::string Name;
		std::string BindingKind;
		std::string TypeKind;
		std::string ResourceShape;
		std::string ResourceAccess;
		std::string ResourceScalarType;
		bool ResourceArray = false;
		bool ResourceMultisample = false;
		std::uint32_t Index = 0;
		std::uint32_t Space = 0;
		std::uint32_t Count = 1;
		std::uint32_t Offset = 0;
		std::uint32_t Size = 0;
		bool HasIndex = false;
		bool HasSpace = false;
		bool HasOffset = false;
		bool HasSize = false;
	};

	struct ShaderEntryPointReflection
	{
		std::string Name;
		ShaderStage Stage = ShaderStage::Unknown;
		std::array<std::uint32_t, 3> ThreadGroupSize{ 0, 0, 0 };
		std::vector<ShaderBindingReflection> Parameters;
	};

	struct ShaderReflection
	{
		std::string SchemaVersion;
		std::string GlobalScopeKind;
		std::vector<ShaderBindingReflection> GlobalParameters;
		std::vector<ShaderEntryPointReflection> EntryPoints;
	};

	struct ShaderReflectionResult
	{
		ShaderReflection Reflection;
		std::string Error;

		explicit operator bool() const
		{
			return Error.empty();
		}
	};

	ShaderStage ShaderStageFromSlangName(std::string_view stageName);
	std::string_view ShaderStageName(ShaderStage stage);

	ShaderReflectionResult ParseSlangReflectionJson(std::string_view jsonText);
	ShaderReflectionResult LoadSlangReflectionJson(const std::filesystem::path& path);

}
