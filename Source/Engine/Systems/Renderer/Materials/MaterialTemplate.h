#pragma once
#include "Engine/Systems/Renderer/Materials/MaterialParameterType.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Swim::Render
{
	struct MaterialParameterDesc
	{
		std::string Name;
		MaterialParameterType Type = MaterialParameterType::Float;
		std::uint32_t Offset = 0; // Byte offset in the record.
	};

	// Normally produced from a compiled shader's reflection
	// (Tools/ShaderCompiler/ShaderMaterialLayout.h); hand-written descs are validated
	// the same way.
	struct MaterialTemplateDesc
	{
		std::string Name;
		std::uint32_t RecordSize = 0; // The shader's struct stride (multiple of 16).
		std::vector<MaterialParameterDesc> Parameters;
	};

	// The parameter layout shared by every instance of one material program
	// (critical-path item 58). Immutable once built. Records start zeroed except for
	// texture/sampler indices, which default to 0: the bindless table's fallback
	// element. Per-template defaults are applied with SetDefault* before instances
	// are created.
	class MaterialTemplate
	{
	  public:
		// Throws std::invalid_argument for an empty name, a record size that is zero or
		// not a multiple of 16, duplicate or empty parameter names, misaligned (std430)
		// offsets, or parameters that overlap or leave the record.
		explicit MaterialTemplate(MaterialTemplateDesc desc);

		const std::string& GetName() const { return name; }

		std::uint32_t GetRecordSize() const { return recordSize; }

		std::span<const MaterialParameterDesc> GetParameters() const { return parameters; }

		// Nullptr when the template has no parameter of that name.
		const MaterialParameterDesc* FindParameter(std::string_view parameter) const;

		// Template defaults copied into every new instance.
		void SetDefault(std::string_view parameter, std::span<const float> values);
		void SetDefault(std::string_view parameter, std::uint32_t value);
		void SetDefault(std::string_view parameter, std::int32_t value);

		std::span<const std::byte> GetDefaultRecord() const { return defaults; }

		// Shared typed write used by templates and instances; throws
		// std::invalid_argument for unknown names or mismatched types/component counts.
		void Write(std::span<std::byte> record, std::string_view parameter, std::span<const float> values) const;
		void Write(std::span<std::byte> record, std::string_view parameter, std::uint32_t value) const;
		void Write(std::span<std::byte> record, std::string_view parameter, std::int32_t value) const;

	  private:
		const MaterialParameterDesc& Require(std::string_view parameter) const;

		std::string name;
		std::uint32_t recordSize = 0;
		std::vector<MaterialParameterDesc> parameters;
		std::vector<std::byte> defaults;
	};
} // namespace Swim::Render
