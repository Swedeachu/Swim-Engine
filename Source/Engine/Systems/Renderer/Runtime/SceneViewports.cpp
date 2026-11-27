#include "Engine/Systems/Renderer/Runtime/SceneViewports.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine
{

	namespace R = Swim::Render;
	namespace S = Swim::Rhi;

	namespace
	{

		constexpr std::uint32_t DrawsBinding = 0;
		constexpr std::uint32_t VerticesBinding = 1;
		constexpr std::uint32_t BindlessSpace = 1;
		constexpr S::Format ColorFormat = S::Format::RGBA8UnormSrgb;

		struct GpuDraw
		{
			std::array<float, 16> ClipFromLocal;
			std::array<float, 12> NormalFromLocal;
			std::array<float, 4> Tint;
			// Local position of the eye (w 1) or, for orthographic views, the direction towards
			// it (w 0): translucent draws keep only the faces turned towards it.
			std::array<float, 4> LocalEye;
			std::uint32_t Texture;
			std::uint32_t Sampler;
			float AlphaCutoff;
			std::uint32_t Flags;
		};

		static_assert(sizeof(GpuDraw) == 160);

		constexpr std::uint32_t DrawLit = 1u;
		constexpr std::uint32_t DrawTranslucent = 2u;

		// clipFromLocal^-1 (0, 0, 1, 0): with reverse-Z, the point (or direction) every view ray
		// comes from, with the sign that points at the viewer.
		std::array<float, 4> LocalEye(const std::array<float, 16>& clipFromLocal)
		{
			const auto inverse = R::ScreenSpace::Inverse(clipFromLocal);

			if (!inverse)
			{
				return { 0.0f, 0.0f, 0.0f, 0.0f };
			}

			std::array<float, 4> eye{ (*inverse)[2], (*inverse)[6], (*inverse)[10], (*inverse)[14] };

			if (std::abs(eye[3]) > 1.0e-6f)
			{
				for (auto& value : eye)
				{
					value /= std::abs(eye[3]);
				}
			}

			return eye;
		}

		struct GpuConstants
		{
			std::array<float, 4> LightDirection;
			std::array<float, 4> LightColor;
			std::array<float, 4> Ambient;
			std::array<float, 4> ClearColor;
			std::uint32_t Mode;
			std::uint32_t Reserved[3];
		};

		// Rows of the inverse transpose of the upper 3x3 (cofactors; scale goes in the normalize).
		std::array<float, 12> NormalMatrix(const std::array<float, 16>& m)
		{
			const auto row = [&](int r)
			{
				return std::array<float, 3>{ m[r * 4], m[r * 4 + 1], m[r * 4 + 2] };
			};
			const auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b)
			{
				return std::array<float, 3>{ a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
			};
			const auto r0 = row(0), r1 = row(1), r2 = row(2);
			// Columns of the cofactor matrix are the row cross products; its transpose's rows are these.
			const auto c0 = cross(r1, r2), c1 = cross(r2, r0), c2 = cross(r0, r1);
			const float det = r0[0] * c0[0] + r0[1] * c0[1] + r0[2] * c0[2];
			const float s = det < 0.0f ? -1.0f : 1.0f;
			return { s * c0[0], s * c1[0], s * c2[0], 0, s * c0[1], s * c1[1], s * c2[1], 0, s * c0[2], s * c1[2], s * c2[2], 0 };
		}

		std::array<float, 4> Direction(const std::array<float, 3>& d)
		{
			const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			return length > 0.0f ? std::array<float, 4>{ d[0] / length, d[1] / length, d[2] / length, 0.0f }
								 : std::array<float, 4>{ 0.0f, 1.0f, 0.0f, 0.0f };
		}

		S::DescriptorWrite BufferWrite(R::RenderCommandContext& c, std::uint32_t binding, R::GraphBuffer buffer)
		{
			const auto range = c.GetRange(buffer);
			S::DescriptorWrite write{};
			write.Binding = binding;
			write.BufferResource = range.Buffer;
			write.BufferOffset = range.Offset;
			write.BufferRange = range.Size;
			return write;
		}

	} // namespace

	struct SceneViewports::Viewport
	{
		SceneViewportDesc Desc;
		std::uint32_t Generation = 0;
		std::unique_ptr<S::Texture> Texture;
		std::unique_ptr<S::TextureView> View;
		R::BindlessTextureHandle Handle;
		bool Initialized = false;
		std::vector<SceneViewportScene> Queue;
		std::size_t Recorded = 0; // Queued scenes drawn this frame (dropped on CommitFrame).
		bool Written = false;	  // Drawn or cleared this frame.
	};

	struct SceneViewports::Retired
	{
		std::unique_ptr<S::Texture> Texture;
		std::unique_ptr<S::TextureView> View;
		S::TimelinePoint LastUse;
		R::BindlessTextureHandle Handle; // Released at RetireAfter while Pending.
		bool Pending = false;			 // Destroyed without a lastUse: waits for a frame completion.
	};

	S::GraphicsPipelineDesc SceneViewports::PipelineDesc(S::ShaderProgram& program, S::PipelineLayout& layout)
	{
		static constexpr std::array<S::Format, 1> Formats{ ColorFormat };
		static constexpr std::array<S::BlendAttachmentState, 1> Opaque{ S::BlendAttachmentState{ false, S::BlendFactor::One,
			S::BlendFactor::Zero, S::BlendOp::Add, S::BlendFactor::One, S::BlendFactor::Zero, S::BlendOp::Add, S::ColorWriteMask::All } };
		S::GraphicsPipelineDesc pipeline{};
		pipeline.Program = &program;
		pipeline.Layout = &layout;
		pipeline.ColorFormats = Formats;
		pipeline.BlendAttachments = Opaque;
		pipeline.DepthStencilFormat = S::Format::D32Float;
		pipeline.DepthStencil.DepthTest = true;
		pipeline.DepthStencil.DepthWrite = true;
		pipeline.DepthStencil.DepthCompare = S::CompareOp::GreaterEqual; // Reverse-Z; the clear triangle sits at 0.
		pipeline.Raster.Cull = S::CullMode::None;
		pipeline.DebugName = "Scene viewport";
		return pipeline;
	}

	S::GraphicsPipelineDesc SceneViewports::TranslucentPipelineDesc(S::ShaderProgram& program, S::PipelineLayout& layout)
	{
		static constexpr std::array<S::BlendAttachmentState, 1> Over{ S::BlendAttachmentState{ true, S::BlendFactor::One,
			S::BlendFactor::OneMinusSourceAlpha, S::BlendOp::Add, S::BlendFactor::One, S::BlendFactor::OneMinusSourceAlpha, S::BlendOp::Add,
			S::ColorWriteMask::All } };
		S::GraphicsPipelineDesc pipeline = PipelineDesc(program, layout);
		pipeline.BlendAttachments = Over;
		pipeline.DepthStencil.DepthWrite = false;
		pipeline.DebugName = "Scene viewport (translucent)";
		return pipeline;
	}

	SceneViewports::SceneViewports(S::Device& deviceInput, R::BindlessResourceTable& table) : device(deviceInput), bindless(table)
	{
		for (const bool point : { true, false })
		{
			S::SamplerDesc desc{};
			desc.MinFilter = desc.MagFilter = point ? S::Filter::Nearest : S::Filter::Linear;
			desc.MipFilter = S::Filter::Nearest;
			desc.AddressU = desc.AddressV = desc.AddressW = S::SamplerAddressMode::ClampToEdge;
			desc.DebugName = point ? "Scene viewport point" : "Scene viewport linear";
			auto sampler = device.CreateSampler(desc);

			if (!sampler)
			{
				throw std::runtime_error("Scene viewport sampler could not be created");
			}

			(point ? pointHandle : linearHandle) = bindless.RegisterSampler(*sampler);
			(point ? pointSampler : linearSampler) = std::move(sampler);
		}
	}

	SceneViewports::~SceneViewports()
	{
		for (auto& viewport : viewports)
		{
			if (viewport->Texture)
			{
				bindless.Release(viewport->Handle);
			}
		}

		bindless.Release(pointHandle);
		bindless.Release(linearHandle);
	}

	SceneViewports::Viewport* SceneViewports::Find(SceneViewportHandle handle)
	{
		if (!handle || handle.Index >= viewports.size())
		{
			return nullptr;
		}

		auto& viewport = *viewports[handle.Index];
		return viewport.Texture && viewport.Generation == handle.Generation ? &viewport : nullptr;
	}

	const SceneViewports::Viewport* SceneViewports::Find(SceneViewportHandle handle) const
	{
		return const_cast<SceneViewports*>(this)->Find(handle);
	}

	SceneViewportHandle SceneViewports::Create(const SceneViewportDesc& desc)
	{
		if (desc.Width == 0 || desc.Height == 0 || desc.Width > 8192 || desc.Height > 8192)
		{
			throw std::invalid_argument(desc.DebugName + ": a scene viewport needs 1 .. 8192 pixels a side");
		}

		auto viewport = std::make_unique<Viewport>();
		viewport->Desc = desc;
		S::TextureDesc textureDesc;
		textureDesc.Extent = { desc.Width, desc.Height, 1 };
		textureDesc.PixelFormat = ColorFormat;
		textureDesc.Usage = S::TextureUsage::ColorAttachment | S::TextureUsage::Sampled | S::TextureUsage::TransferSource;
		textureDesc.DebugName = viewport->Desc.DebugName;
		viewport->Texture = device.CreateTexture(textureDesc);

		if (!viewport->Texture)
		{
			throw std::runtime_error(desc.DebugName + " could not be created");
		}

		S::TextureViewDesc viewDesc;
		viewDesc.PixelFormat = ColorFormat;
		viewDesc.DebugName = viewport->Desc.DebugName;
		viewport->View = device.CreateTextureView(*viewport->Texture, viewDesc);

		if (!viewport->View)
		{
			throw std::runtime_error(desc.DebugName + " view could not be created");
		}

		const auto handle = bindless.TryRegisterTexture(*viewport->View);

		if (!handle)
		{
			throw std::length_error(desc.DebugName + ": the bindless table is full");
		}

		viewport->Handle = *handle;

		for (std::uint32_t index = 0; index < viewports.size(); ++index)
		{
			if (!viewports[index]->Texture)
			{
				viewport->Generation = viewports[index]->Generation + 1;
				viewports[index] = std::move(viewport);
				return { index, viewports[index]->Generation };
			}
		}

		viewport->Generation = 1;
		viewports.push_back(std::move(viewport));
		return { static_cast<std::uint32_t>(viewports.size() - 1), 1 };
	}

	bool SceneViewports::Destroy(SceneViewportHandle handle, S::TimelinePoint lastUse)
	{
		auto* viewport = Find(handle);

		if (!viewport)
		{
			return false;
		}

		const bool pending = !lastUse.Semaphore;

		if (!pending)
		{
			bindless.Release(viewport->Handle, lastUse);
		}

		retired.push_back(std::make_unique<Retired>(
			Retired{ std::move(viewport->Texture), std::move(viewport->View), lastUse, viewport->Handle, pending }));
		viewport->Queue.clear();
		viewport->Recorded = 0;
		viewport->Initialized = false;
		return true;
	}

	bool SceneViewports::IsValid(SceneViewportHandle handle) const
	{
		return Find(handle) != nullptr;
	}

	void SceneViewports::Draw(SceneViewportHandle handle, SceneViewportScene scene)
	{
		auto* viewport = Find(handle);

		if (!viewport)
		{
			throw std::invalid_argument("Scene viewport: invalid handle");
		}

		auto& region = scene.Region;

		if (region[2] == 0 || region[3] == 0)
		{
			region = { 0, 0, viewport->Desc.Width, viewport->Desc.Height };
		}
		else if (std::uint64_t(region[0]) + region[2] > viewport->Desc.Width || std::uint64_t(region[1]) + region[3] > viewport->Desc.Height)
		{
			throw std::invalid_argument(viewport->Desc.DebugName + ": the region lies outside the texture");
		}

		viewport->Queue.push_back(std::move(scene));
	}

	SceneViewportImage SceneViewports::GetImage(SceneViewportHandle handle) const
	{
		const auto* viewport = Find(handle);

		if (!viewport)
		{
			return {};
		}

		return { bindless.GetIndex(viewport->Handle), bindless.GetIndex(viewport->Desc.PointFilter ? pointHandle : linearHandle),
			viewport->Desc.Width, viewport->Desc.Height };
	}

	bool SceneViewports::IsIdle(SceneViewportHandle handle) const
	{
		const auto* viewport = Find(handle);
		return viewport && viewport->Queue.empty();
	}

	SceneViewportStats SceneViewports::GetStats() const
	{
		SceneViewportStats stats;

		for (const auto& viewport : viewports)
		{
			if (viewport->Texture)
			{
				++stats.Viewports;
				stats.Queued += static_cast<std::uint32_t>(viewport->Queue.size());
			}
		}

		stats.Drawn = drawn;
		stats.LastItems = lastItems;
		return stats;
	}

	namespace
	{

		struct Call
		{
			std::uint32_t IndexCount = 0;
			std::uint32_t FirstIndex = 0;
			std::int32_t VertexOffset = 0;
			std::uint32_t Draw = 0;
			std::uint32_t VertexPage = 0;
			std::uint32_t IndexPage = 0;
		};

		struct Region
		{
			std::array<std::uint32_t, 4> Rect;
			GpuConstants Constants;
			std::size_t FirstCall = 0;
			std::size_t CallCount = 0;
			std::size_t OpaqueCalls = 0; // The first calls; translucent ones follow, back to front.
		};

		// One viewport's ready scenes this frame.
		struct ViewportBatch
		{
			std::vector<GpuDraw> Draws;
			std::vector<Call> Calls;
			std::vector<Region> Regions;
		};

		// Where scene items' meshes and textures come from.
		struct SceneItemSource
		{
			const R::GeometryHeap& Geometry;
			const R::GeometryGraphResources& Pages;
			const R::AssetResidencyService& Residency;
			const SceneViewports::AssetResolver& Resolver;
			std::uint32_t MaterialLinear = 0;
			std::uint32_t MaterialPoint = 0;

			auto MeshOf(const SceneViewportItem& item) const { return Resolver.Mesh ? Resolver.Mesh(item.Mesh) : item.Mesh; }

			auto TextureOf(const SceneViewportItem& item) const
			{
				return Resolver.Texture && item.Texture ? Resolver.Texture(item.Texture) : item.Texture;
			}

			bool Resident(const SceneViewportItem& item) const
			{
				const auto texture = TextureOf(item);
				return Residency.GetState(MeshOf(item)) == R::AssetResidencyState::Resident &&
					(!texture || Residency.GetState(texture) == R::AssetResidencyState::Resident);
			}
		};

		// Adds a scene's region, draws and calls; the number of items drawn. Opaque items come
		// first in order, then translucent ones (when blended) from the farthest origin.
		std::uint32_t AddScene(ViewportBatch& batch, const SceneViewportScene& scene, const SceneItemSource& source, bool blended)
		{
			auto& calls = batch.Calls;
			std::uint32_t items = 0;
			Region region{ scene.Region, {}, calls.size(), 0, 0 };
			region.Constants.LightDirection = Direction(scene.LightDirection);
			region.Constants.LightColor = { scene.LightColor[0], scene.LightColor[1], scene.LightColor[2], 0.0f };
			region.Constants.Ambient = { scene.Ambient[0], scene.Ambient[1], scene.Ambient[2], 0.0f };
			region.Constants.ClearColor = scene.ClearColor;
			const auto clipFromWorld = R::MultiplyRowMajor(scene.Projection, scene.View);
			// Reverse-Z depth of each translucent item's origin: the smallest is the farthest.
			std::vector<std::pair<float, const SceneViewportItem*>> order;
			order.reserve(scene.Items.size());

			for (const auto& item : scene.Items)
			{
				if (!(blended && item.Translucent))
				{
					order.emplace_back(0.0f, &item);
				}
			}

			const auto opaqueItems = order.size();

			for (const auto& item : scene.Items)
			{
				if (blended && item.Translucent)
				{
					const auto m = R::MultiplyRowMajor(clipFromWorld, item.Transform);
					const float w = m[15];
					order.emplace_back(std::abs(w) > 1.0e-12f ? m[11] / w : 0.0f, &item);
				}
			}

			std::stable_sort(order.begin() + std::ptrdiff_t(opaqueItems), order.end(),
				[](const auto& a, const auto& b)
				{
					return a.first < b.first;
				});

			for (std::size_t at = 0; at < order.size(); ++at)
			{
				if (at == opaqueItems)
				{
					region.OpaqueCalls = calls.size() - region.FirstCall;
				}

				const auto& item = *order[at].second;
				const auto mesh = source.Residency.GetGpuMesh(source.MeshOf(item));
				const auto* metadata = source.Geometry.GetMetadata(mesh);

				if (!metadata || metadata->VertexPage >= source.Pages.Pages.size() || metadata->IndexPage >= source.Pages.Pages.size() ||
					metadata->IndexCount == 0)
				{
					continue;
				}

				GpuDraw draw{};
				draw.ClipFromLocal = R::MultiplyRowMajor(clipFromWorld, item.Transform);
				draw.NormalFromLocal = NormalMatrix(item.Transform);
				const bool translucent = blended && item.Translucent;
				draw.LocalEye = translucent ? LocalEye(draw.ClipFromLocal) : std::array<float, 4>{};
				draw.Tint = item.Tint;
				draw.Texture = item.Texture ? source.Residency.GetBindlessIndex(source.TextureOf(item)) : 0u;
				draw.Sampler = item.PointSampled ? source.MaterialPoint : source.MaterialLinear;
				draw.AlphaCutoff = item.AlphaCutoff;
				draw.Flags = (item.Lit ? DrawLit : 0u) | (translucent ? DrawTranslucent : 0u);
				const auto drawIndex = static_cast<std::uint32_t>(batch.Draws.size());
				batch.Draws.push_back(draw);
				const auto submeshes = source.Geometry.GetSubmeshes(mesh);

				if (submeshes.empty())
				{
					calls.push_back({ metadata->IndexCount, metadata->FirstIndex, std::int32_t(metadata->VertexOffset), drawIndex,
						metadata->VertexPage, metadata->IndexPage });
				}

				for (const auto& submesh : submeshes)
				{
					calls.push_back({ submesh.IndexCount, submesh.FirstIndex, submesh.VertexOffset, drawIndex, metadata->VertexPage,
						metadata->IndexPage });
				}

				++items;
			}

			region.CallCount = calls.size() - region.FirstCall;

			if (opaqueItems == order.size())
			{
				region.OpaqueCalls = region.CallCount;
			}

			batch.Regions.push_back(region);
			return items;
		}

		// The geometry pages a viewport's calls read.
		struct PageLookup
		{
			std::vector<std::pair<std::uint32_t, R::GraphBuffer>> Map;
			R::GraphBuffer Fallback; // Clear-only scenes read no vertices.

			R::GraphBuffer operator()(std::uint32_t page) const
			{
				for (const auto& [index, buffer] : Map)
				{
					if (index == page)
					{
						return buffer;
					}
				}

				return Fallback;
			}
		};

		PageLookup CollectPages(const std::vector<Call>& calls, const R::GeometryGraphResources& pages, R::GraphBuffer fallback)
		{
			PageLookup lookup{ {}, fallback };

			for (const auto& call : calls)
			{
				for (const auto page : { call.VertexPage, call.IndexPage })
				{
					if (std::find_if(lookup.Map.begin(), lookup.Map.end(),
							[&](const auto& entry)
							{
								return entry.first == page;
							}) == lookup.Map.end())
					{
						lookup.Map.emplace_back(page, pages.Pages[page]);
					}
				}
			}

			return lookup;
		}

		struct ViewportPass
		{
			SceneViewports::Program Program;
			std::string Name;
			R::GraphTexture Texture;
			R::GraphTexture Depth;
			R::GraphBuffer Draws;
			std::vector<Region> Regions;
			std::vector<Call> Calls;
			PageLookup Pages;
			bool Load = false;
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			S::DescriptorTable* Bindless = nullptr;
		};

		void DrawRegions(R::RenderCommandContext& c, const ViewportPass& pass)
		{
			const auto& program = pass.Program;
			const auto& calls = pass.Calls;
			S::TextureViewDesc colorView;
			colorView.PixelFormat = ColorFormat;
			std::array<S::RenderingAttachmentDesc, 1> colors{};
			colors[0].View = &c.CreateView(pass.Texture, colorView);
			colors[0].Load = pass.Load ? S::LoadOp::Load : S::LoadOp::Clear;
			colors[0].Clear.Value = { 0.0f, 0.0f, 0.0f, 0.0f };
			S::TextureViewDesc depthView;
			depthView.PixelFormat = S::Format::D32Float;
			const S::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(pass.Depth, depthView), S::LoadOp::Clear,
				S::StoreOp::Discard, 0.0f, 0 };
			auto& list = c.Commands();
			list.BeginRendering({ colors, &depthAttachment, { pass.Width, pass.Height } });
			list.BindGraphicsPipeline(*program.Pipeline);
			list.BindDescriptorTable(BindlessSpace, *pass.Bindless);
			std::uint32_t boundPage = ~0u, boundIndexPage = ~0u;
			S::DescriptorTable* vertexTable = nullptr;

			const auto bindVertices = [&](std::uint32_t page)
			{
				auto table = c.CreateDescriptorTable({ program.Layout, 0, 0, pass.Name });

				if (!table)
				{
					throw std::runtime_error(pass.Name + " descriptor table could not be created");
				}

				const std::array<S::DescriptorWrite, 2> writes{ BufferWrite(c, DrawsBinding, pass.Draws),
					BufferWrite(c, VerticesBinding, pass.Pages(page)) };
				table->Write(writes);
				vertexTable = &static_cast<S::DescriptorTable&>(c.Retain(std::move(table)));
				list.BindDescriptorTable(0, *vertexTable);
				boundPage = page;
			};

			bindVertices(calls.empty() ? ~0u : calls.front().VertexPage);
			bool translucentBound = false;
			// Binding a pipeline unbinds the descriptor tables: bind them again for the new one.
			const auto bindPipeline = [&](S::GraphicsPipeline& pipeline)
			{
				list.BindGraphicsPipeline(pipeline);
				list.BindDescriptorTable(BindlessSpace, *pass.Bindless);
				list.BindDescriptorTable(0, *vertexTable);
			};

			for (const auto& region : pass.Regions)
			{
				if (translucentBound)
				{
					bindPipeline(*program.Pipeline);
					translucentBound = false;
				}

				const auto& r = region.Rect;
				list.SetViewport({ float(r[0]), float(r[1]), float(r[2]), float(r[3]), 0.0f, 1.0f });
				list.SetScissor({ std::int32_t(r[0]), std::int32_t(r[1]), r[2], r[3] });
				auto constants = region.Constants;
				constants.Mode = 1;

				list.PushConstants(S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));
				list.Draw(3);
				constants.Mode = 0;
				list.PushConstants(S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));

				for (std::size_t i = region.FirstCall; i < region.FirstCall + region.CallCount; ++i)
				{
					const auto& call = calls[i];

					if (!translucentBound && i >= region.FirstCall + region.OpaqueCalls)
					{
						bindPipeline(*program.TranslucentPipeline);
						list.PushConstants(
							S::ShaderStageMask::Vertex | S::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&constants, 1)));
						translucentBound = true;
					}

					if (call.VertexPage != boundPage)
					{
						bindVertices(call.VertexPage);
					}

					if (call.IndexPage != boundIndexPage)
					{
						list.BindIndexBuffer(c.Get(pass.Pages(call.IndexPage)), 0, S::IndexType::Uint32);
						boundIndexPage = call.IndexPage;
					}

					list.DrawIndexed(call.IndexCount, 1, call.FirstIndex, call.VertexOffset, call.Draw);
				}
			}

			list.EndRendering();
		}

		// The viewport's pass: its draw buffer, a transient depth and the regions drawn into texture.
		void AddViewportPass(R::RenderGraph& graph, const SceneViewports::Program& program, S::DescriptorTable& bindlessTable,
			const SceneViewportDesc& desc, bool initialized, R::GraphTexture texture, const R::GeometryGraphResources& pages,
			ViewportBatch batch)
		{
			// A dummy record keeps the draw buffer non-empty for clear-only scenes.
			if (batch.Draws.empty())
			{
				batch.Draws.push_back({});
			}

			const auto name = desc.DebugName;
			const auto drawBuffer = graph.CreateUpload(std::as_bytes(std::span(batch.Draws)), name + " draws", S::BufferUsage::Storage, 16);
			S::TextureDesc depthDesc;
			depthDesc.Extent = { desc.Width, desc.Height, 1 };
			depthDesc.PixelFormat = S::Format::D32Float;
			depthDesc.Usage = S::TextureUsage::DepthStencilAttachment;
			depthDesc.DebugName = "Scene viewport depth";
			const auto depth = graph.CreateTexture(depthDesc);
			const bool whole = batch.Regions.front().Rect == std::array<std::uint32_t, 4>{ 0, 0, desc.Width, desc.Height };
			const bool load = initialized && !whole;
			auto lookup = CollectPages(batch.Calls, pages, drawBuffer);
			std::vector<R::GraphBuffer> pageBuffers;

			for (const auto& [index, buffer] : lookup.Map)
			{
				pageBuffers.push_back(buffer);
			}

			ViewportPass pass{ program, name, texture, depth, drawBuffer, std::move(batch.Regions), std::move(batch.Calls),
				std::move(lookup), load, desc.Width, desc.Height, &bindlessTable };
			graph.AddPass(
				name, S::QueueType::Graphics,
				[&](R::RenderGraphBuilder& b)
				{
					if (load)
					{
						b.ReadWrite(texture, S::ResourceState::ColorAttachment);
					}
					else
					{
						b.Write(texture, S::ResourceState::ColorAttachment);
					}

					b.Write(depth, S::ResourceState::DepthStencilWrite);
					b.Read(drawBuffer, S::ResourceState::ShaderRead);

					for (const auto buffer : pageBuffers)
					{
						b.Read(buffer, S::ResourceState::ShaderRead);
					}
				},
				[pass = std::move(pass)](R::RenderCommandContext& c)
				{
					DrawRegions(c, pass);
				});
		}

	} // namespace

	std::vector<R::GraphTexture> SceneViewports::Record(R::RenderGraph& graph, const R::GeometryHeap& geometry,
		const R::GeometryGraphResources& pages, const R::AssetResidencyService& residency, const Program& program,
		S::DescriptorTable& bindlessTable, std::uint32_t materialLinear, std::uint32_t materialPoint)
	{
		std::vector<R::GraphTexture> textures;
		lastItems = 0;

		if (!program.Pipeline || !program.Layout)
		{
			return textures;
		}

		const SceneItemSource source{ geometry, pages, residency, resolver, materialLinear, materialPoint };
		const auto resident = [&](const SceneViewportItem& item)
		{
			return source.Resident(item);
		};

		for (auto& slot : viewports)
		{
			auto& viewport = *slot;
			viewport.Recorded = 0;
			viewport.Written = false;

			if (!viewport.Texture)
			{
				continue;
			}

			const auto texture =
				graph.ImportTexture(*viewport.Texture, viewport.Initialized ? S::ResourceState::ShaderRead : S::ResourceState::Undefined);
			graph.Export(texture, S::ResourceState::ShaderRead);
			textures.push_back(texture);
			// Scenes are drawn in order up to the first one still waiting for residency.
			ViewportBatch batch;

			for (const auto& scene : viewport.Queue)
			{
				if (!std::all_of(scene.Items.begin(), scene.Items.end(), resident))
				{
					break;
				}

				lastItems += AddScene(batch, scene, source, program.TranslucentPipeline != nullptr);
				++viewport.Recorded;
			}

			if (batch.Regions.empty())
			{
				if (viewport.Initialized)
				{
					continue;
				}

				// Never drawn: clear it so the UI samples transparent black, not undefined texels.
				batch.Regions.push_back({ { 0, 0, viewport.Desc.Width, viewport.Desc.Height }, {}, 0, 0, 0 });
			}

			viewport.Written = true;
			AddViewportPass(graph, program, bindlessTable, viewport.Desc, viewport.Initialized, texture, pages, std::move(batch));
		}

		return textures;
	}

	void SceneViewports::CommitFrame()
	{
		for (auto& viewport : viewports)
		{
			drawn += viewport->Recorded;
			viewport->Queue.erase(viewport->Queue.begin(), viewport->Queue.begin() + std::ptrdiff_t(viewport->Recorded));
			viewport->Initialized = viewport->Initialized || viewport->Written;
			viewport->Recorded = 0;
			viewport->Written = false;
		}
	}

	void SceneViewports::RetireAfter(const S::TimelinePoint& completion)
	{
		for (auto& entry : retired)
		{
			if (entry->Pending)
			{
				entry->LastUse = completion;
				entry->Pending = false;
				bindless.Release(entry->Handle, completion);
			}
		}
	}

	void SceneViewports::AbortFrame()
	{
		for (auto& viewport : viewports)
		{
			viewport->Recorded = 0;
			viewport->Written = false;
		}
	}

	void SceneViewports::Collect()
	{
		std::erase_if(retired,
			[](const std::unique_ptr<Retired>& entry)
			{
				return !entry->Pending &&
					(!entry->LastUse.Semaphore || entry->LastUse.Semaphore->GetCompletedValue() >= entry->LastUse.Value);
			});
	}

	void SceneViewports::Drain()
	{
		for (const auto& entry : retired)
		{
			if (entry->Pending)
			{
				// Draining follows a device idle: nothing in flight uses it any more.
				entry->Pending = false;
				bindless.Release(entry->Handle);
			}
			else if (entry->LastUse.Semaphore)
			{
				entry->LastUse.Semaphore->Wait(entry->LastUse.Value);
			}
		}

		Collect();
	}

} // namespace Engine
