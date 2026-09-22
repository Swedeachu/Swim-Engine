#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include <algorithm>
#include <sstream>

namespace Swim::Render
{
	std::string CompiledRenderGraph::Dump() const
	{
		if (!definition)
		{
			throw std::logic_error("Uninitialized compiled RenderGraph");
		}
		std::ostringstream out;
		out << "RenderGraph: serialized graphics queue; " << schedule.size() << '/' << definition->Passes.size() << " live passes\n";
		const auto barrier = [&](const GraphBarrier& b)
		{
			out << "  barrier r" << b.Resource << " " << static_cast<std::uint32_t>(b.Before) << " -> "
				<< static_cast<std::uint32_t>(b.After) << " mip=" << b.Range.BaseMipLevel << " layer=" << b.Range.BaseArrayLayer << '\n';
		};
		for (const auto& s : schedule)
		{
			const auto& pass = definition->Passes[s.Pass];
			out << "pass " << s.Pass << " \"" << pass.Name << "\" type=" << static_cast<unsigned>(pass.Type) << " dependencies=";
			for (auto p : dependencies[s.Pass])
			{
				out << p << ',';
			}
			out << '\n';
			for (const auto& b : s.Barriers)
			{
				barrier(b);
			}
		}
		for (std::uint32_t p = 0; p < definition->Passes.size(); ++p)
		{
			if (std::none_of(schedule.begin(), schedule.end(),
					[&](const auto& s)
					{
						return s.Pass == p;
					}))
			{
				out << "culled " << p << " \"" << definition->Passes[p].Name << "\"\n";
			}
		}
		for (std::uint32_t r = 0; r < lifetimes.size(); ++r)
		{
			const auto& life = lifetimes[r];
			const auto& resource = definition->Resources[r];
			out << "resource " << r << " \"" << resource.Name << "\" imported=" << bool(resource.Imported)
				<< " exported=" << resource.Exported;
			if (resource.Staging != Internal::GraphStaging::None)
			{
				out << " staging=" << (resource.Staging == Internal::GraphStaging::Upload ? "upload" : "readback");
			}
			if (life.First == GraphResourceLifetime::Unused)
			{
				out << " unused\n";
			}
			else
			{
				out << " lifetime=[" << life.First << ',' << life.Last << "] allocation=" << life.Allocation << '\n';
			}
		}
		out << "exports\n";
		for (const auto& b : finalBarriers)
		{
			barrier(b);
		}
		return out.str();
	}
} // namespace Swim::Render
