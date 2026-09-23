#pragma once
#include "Engine/Systems/Renderer/Materials/MaterialTemplate.h"

#include <array>
#include <memory>

namespace Swim::Render
{
	// One material's parameter values in its template's record layout (item 58).
	// Starts from the template defaults; every change bumps the version so a GPU
	// material buffer (item 59) can upload only changed records.
	class MaterialInstance
	{
	  public:
		explicit MaterialInstance(std::shared_ptr<const MaterialTemplate> materialTemplate);

		const MaterialTemplate& GetTemplate() const { return *materialTemplate; }

		std::span<const std::byte> GetRecord() const { return record; }

		std::uint64_t GetVersion() const { return version; }

		// Float/FloatN parameters; the value count must match the type.
		void SetFloat(std::string_view parameter, float value);
		void SetVector(std::string_view parameter, std::span<const float> values);
		void SetUint(std::string_view parameter, std::uint32_t value);
		void SetInt(std::string_view parameter, std::int32_t value);
		// Bindless indices (BindlessResourceTable); only for TextureIndex/SamplerIndex parameters.
		void SetTexture(std::string_view parameter, std::uint32_t bindlessIndex);
		void SetSampler(std::string_view parameter, std::uint32_t bindlessIndex);

		float GetFloat(std::string_view parameter) const;
		std::array<float, 4> GetVector(std::string_view parameter) const; // Unused components are zero.
		std::uint32_t GetUint(std::string_view parameter) const;		  // Uint, TextureIndex or SamplerIndex.
		std::int32_t GetInt(std::string_view parameter) const;

	  private:
		const MaterialParameterDesc& Require(std::string_view parameter, bool (*accepts)(MaterialParameterType)) const;
		void Changed(bool changed);

		std::shared_ptr<const MaterialTemplate> materialTemplate;
		std::vector<std::byte> record;
		std::uint64_t version = 1;
	};
} // namespace Swim::Render
