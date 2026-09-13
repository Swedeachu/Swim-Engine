#pragma once
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphDefinition.h"

namespace Swim::Render
{
	class RenderGraphBuilder
	{
	  public:
		void Read(GraphBuffer resource, Rhi::ResourceState state);
		void Write(GraphBuffer resource, Rhi::ResourceState state);
		void ReadWrite(GraphBuffer resource, Rhi::ResourceState state);
		void Read(GraphTexture resource, Rhi::ResourceState state, Rhi::TextureSubresourceRange range = {});
		void Write(GraphTexture resource, Rhi::ResourceState state, Rhi::TextureSubresourceRange range = {});
		void ReadWrite(GraphTexture resource, Rhi::ResourceState state, Rhi::TextureSubresourceRange range = {});
		void DependsOn(GraphPass pass);
		void SideEffect();

	  private:
		friend class RenderGraph;

		RenderGraphBuilder(Internal::GraphDefinition& definition, Internal::GraphPassDefinition& pass) : definition(definition), pass(pass)
		{
		}

		void Use(std::uint64_t graph, std::uint32_t index, GraphKind kind, GraphAccess access, Rhi::ResourceState state,
			Rhi::TextureSubresourceRange range);
		Internal::GraphDefinition& definition;
		Internal::GraphPassDefinition& pass;
	};
} // namespace Swim::Render
