#pragma once
#include "Engine/Systems/Renderer/GpuScene/GpuRecordUpload.h"
#include "Engine/Systems/Renderer/GpuScene/Internal/GpuDirtyRowSet.h"

#include <cstring>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace Swim::Render
{
	// A persistent device-local buffer of fixed-size records with a CPU mirror
	// and dirty-row tracking. Import uploads only rows edited since the last
	// import, batched into contiguous runs, as one staged transfer pass; the
	// buffer rests in ShaderRead. Commit after a successful Execute; Abort puts
	// the rows back so the next import records them again.
	//
	// Externally synchronized. Rows are overwritten in queue order, so later
	// submissions see new contents and earlier ones finished with the old.
	template <typename Record> class GpuRecordBuffer
	{
		static_assert(std::is_trivially_copyable_v<Record>);
		static_assert(sizeof(Record) % 16 == 0, "GPU records are std430 rows padded to 16 bytes");

	  public:
		struct ImportResult
		{
			GraphBuffer Buffer;
			std::optional<GraphPass> Pass;
			std::uint32_t Rows = 0;
			std::uint32_t Runs = 0;
			std::uint64_t Bytes = 0;
		};

		GpuRecordBuffer(Rhi::Device& device, std::uint32_t capacity, std::string name)
			: mirror(capacity), dirty(capacity), name(std::move(name))
		{
			if (capacity == 0)
			{
				throw std::invalid_argument(this->name + " needs at least one row");
			}
			buffer = device.CreateBuffer({ std::uint64_t(capacity) * sizeof(Record),
				Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::DeviceLocal, this->name });
			if (!buffer)
			{
				throw std::runtime_error(this->name + " buffer could not be created");
			}
		}

		std::uint32_t GetCapacity() const { return static_cast<std::uint32_t>(mirror.size()); }

		const Record& Get(std::uint32_t row) const { return mirror.at(row); }

		// Returns the mirror row for editing and schedules it for upload.
		Record& Edit(std::uint32_t row)
		{
			auto& record = mirror.at(row);
			dirty.Mark(row);
			return record;
		}

		std::uint32_t GetDirtyRows() const { return dirty.Size(); }

		Rhi::Buffer& GetBuffer() const { return *buffer; }

		ImportResult Import(RenderGraph& graph)
		{
			if (pending)
			{
				throw std::logic_error(name + " has an import awaiting CommitUploads/AbortUploads");
			}
			ImportResult result;
			result.Buffer = graph.ImportBuffer(*buffer, Rhi::ResourceState::ShaderRead);
			auto rows = dirty.Take();
			if (rows.empty())
			{
				return result;
			}
			try
			{
				auto snapshot = std::make_shared<std::vector<std::byte>>(rows.size() * sizeof(Record));
				for (std::size_t i = 0; i < rows.size(); ++i)
				{
					std::memcpy(snapshot->data() + i * sizeof(Record), &mirror[rows[i]], sizeof(Record));
				}
				auto runs = BuildRecordRuns(rows);
				result.Runs = static_cast<std::uint32_t>(runs.size());
				result.Rows = static_cast<std::uint32_t>(rows.size());
				result.Bytes = snapshot->size();
				result.Pass =
					RecordRowRunsUpload(graph, name + " upload", result.Buffer, sizeof(Record), std::move(runs), std::move(snapshot));
			}
			catch (...)
			{
				for (auto row : rows)
				{
					dirty.Mark(row);
				}
				throw;
			}
			recorded = std::move(rows);
			pending = true;
			return result;
		}

		void Commit()
		{
			recorded.clear();
			pending = false;
		}

		void Abort()
		{
			for (auto row : recorded)
			{
				dirty.Mark(row);
			}
			Commit();
		}

	  private:
		std::vector<Record> mirror;
		Internal::GpuDirtyRowSet dirty;
		std::vector<std::uint32_t> recorded;
		std::unique_ptr<Rhi::Buffer> buffer;
		std::string name;
		bool pending = false;
	};
} // namespace Swim::Render
