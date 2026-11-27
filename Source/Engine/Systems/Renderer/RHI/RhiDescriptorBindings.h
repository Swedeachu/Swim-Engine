#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Swim::Rhi
{

	// Which descriptor tables a command list has bound at one bind point (graphics or compute),
	// with one contract for every backend, after the Vulkan rules for pipeline layout
	// compatibility:
	//  - Two layouts are compatible for space N when spaces 0..N are defined identically and
	//    their push constant ranges are identical.
	//  - Binding a pipeline unbinds nothing. A draw or dispatch needs, for every space its
	//    layout reflects, a table bound with a layout compatible with it for that space.
	//  - Binding a table to space N with layout L unbinds lower spaces M bound with a layout
	//    not compatible with L for M, and higher spaces bound with a layout not compatible
	//    with L for N.
	//  - A table only binds to the space it was made for, with the same definition in the
	//    active layout.
	// Each backend keeps one per bind point and calls it on the same commands, so a sequence
	// succeeds or fails the same way everywhere.
	class DescriptorBindingState
	{

	  public:
		static bool SameBinding(const DescriptorBindingDesc& a, const DescriptorBindingDesc& b)
		{
			return a.Binding == b.Binding && a.Type == b.Type && a.Count == b.Count && a.Stages == b.Stages &&
				a.VariableCount == b.VariableCount && a.PartiallyBound == b.PartiallyBound &&
				a.StorageTextureFormat == b.StorageTextureFormat && a.SampledClass == b.SampledClass &&
				a.SampledDimension == b.SampledDimension && a.UpdateAfterBind == b.UpdateAfterBind && a.Multisampled == b.Multisampled;
		}

		// Space `space` defined identically (a space a layout leaves out has no bindings).
		static bool SameSpace(const ShaderProgramInterface& a, const ShaderProgramInterface& b, std::uint32_t space)
		{
			if (&a == &b)
			{
				return true;
			}

			const auto* left = Find(a, space);
			const auto* right = Find(b, space);
			const std::size_t leftCount = left ? left->Bindings.size() : 0;
			const std::size_t rightCount = right ? right->Bindings.size() : 0;
			return leftCount == rightCount &&
				(leftCount == 0 || std::equal(left->Bindings.begin(), left->Bindings.end(), right->Bindings.begin(), &SameBinding));
		}

		static bool CompatibleFor(const ShaderProgramInterface& a, const ShaderProgramInterface& b, std::uint32_t space)
		{
			if (&a == &b)
			{
				return true;
			}

			if (a.PushConstants.size() != b.PushConstants.size() ||
				!std::equal(a.PushConstants.begin(), a.PushConstants.end(), b.PushConstants.begin(),
					[](const PushConstantRange& x, const PushConstantRange& y)
					{
						return x.Offset == y.Offset && x.Size == y.Size && x.Stages == y.Stages;
					}))
			{
				return false;
			}

			for (std::uint32_t s = 0; s <= space; ++s)
			{
				if (!SameSpace(a, b, s))
				{
					return false;
				}
			}

			return true;
		}

		// `table` (any backend object) made for `tableLayout`'s `space`, bound while `active` is
		// the bind point's pipeline layout. Throws when it does not fit that layout's space.
		void Bind(std::uint32_t space, const void* table, const ShaderProgramInterface& tableLayout, const ShaderProgramInterface& active)
		{
			const auto* schema = Find(active, space);

			if (!schema || !SameSpace(tableLayout, active, space))
			{
				throw std::invalid_argument(
					"Descriptor table must match an identically defined pipeline layout space and have every required element initialized");
			}

			for (std::uint32_t other = 0; other < entries.size(); ++other)
			{
				auto& entry = entries[other];

				if (other != space && entry.Table && !CompatibleFor(*entry.Layout, active, std::min(other, space)))
				{
					entry = {};
				}
			}

			if (entries.size() <= space)
			{
				entries.resize(space + 1);
			}

			entries[space] = { table, &active };
		}

		// Throws unless every space `active` reflects has a compatible table bound.
		void Require(const ShaderProgramInterface& active) const
		{
			for (const auto& schema : active.DescriptorSchemas)
			{
				if (!schema.Bindings.empty() && !Get(schema.Space, active))
				{
					throw std::logic_error("Every reflected descriptor space must be bound before drawing or dispatching");
				}
			}
		}

		// The table `active` reads at `space`, or null.
		const void* Get(std::uint32_t space, const ShaderProgramInterface& active) const
		{
			if (space >= entries.size() || !entries[space].Table || !CompatibleFor(*entries[space].Layout, active, space))
			{
				return nullptr;
			}

			return entries[space].Table;
		}

		void Reset() { entries.clear(); }

	  private:
		struct Entry
		{
			const void* Table = nullptr;
			const ShaderProgramInterface* Layout = nullptr; // The active layout it was bound with.
		};

		static const DescriptorSchemaDesc* Find(const ShaderProgramInterface& layout, std::uint32_t space)
		{
			for (const auto& schema : layout.DescriptorSchemas)
			{
				if (schema.Space == space)
				{
					return &schema;
				}
			}

			return nullptr;
		}

		std::vector<Entry> entries;
	};

} // namespace Swim::Rhi
