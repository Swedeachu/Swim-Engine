#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Swim::Rhi
{

	class DeviceLostError : public std::runtime_error
	{
	public:
		DeviceLostError() : std::runtime_error("RHI device lost; inspect device diagnostics and recreate the device")
		{
		}
	};

	enum class DeviceFaultStatus : std::uint8_t
	{
		None,
		Pending,
		Unsupported,
		Complete,
		Truncated,
		Failed,
	};

	struct DeviceFaultDetails
	{
		DeviceFaultStatus Status = DeviceFaultStatus::None;
		std::int32_t NativeResult = 0;
		std::string Description;
		// Owned, bounded backend descriptions, including fault addresses and
		// vendor codes. These are diagnostic evidence, not portable GPU pointers.
		std::vector<std::string> Entries;
	};

	struct DeviceLossSnapshot
	{
		bool Lost = false;
		bool FaultReportingEnabled = false;
		std::int32_t NativeResult = 0;
		std::string Operation;
		DeviceFaultDetails Fault;
		bool RetirementAttempted = false;
		std::int32_t RetirementResult = 0;
	};

	// Retain this object beyond device destruction. No native device is retained.
	// Lost is sticky; a concurrent snapshot may show Pending while the first
	// observing thread collects optional native fault data. No reset/recovery API.
	class DeviceDiagnostics
	{
	public:
		explicit DeviceDiagnostics(bool faultReportingEnabled = false)
			: faultReportingEnabled(faultReportingEnabled)
		{
		}

		bool IsLost() const noexcept
		{
			return lost.load(std::memory_order_acquire);
		}

		DeviceLossSnapshot Snapshot() const
		{
			std::scoped_lock lock(mutex);
			return { IsLost(), faultReportingEnabled, nativeResult,
				std::string(operation.data(), operationLength), fault, retirementAttempted, retirementResult };
		}

		// Backend reporting entry points. The first observer wins; recording the
		// loss itself needs no allocation, even if later fault collection fails.
		bool TryRecordLoss(std::string_view name, std::int32_t result) noexcept
		{
			std::scoped_lock lock(mutex);
			if (IsLost())
			{
				return false;
			}
			operationLength = std::min(name.size(), operation.size());
			if (operationLength != 0)
			{
				std::copy_n(name.data(), operationLength, operation.data());
			}
			nativeResult = result;
			fault.Status = DeviceFaultStatus::Pending;
			lost.store(true, std::memory_order_release);
			return true;
		}

		void CompleteFaultCapture(DeviceFaultDetails details) noexcept
		{
			std::scoped_lock lock(mutex);
			if (IsLost() && fault.Status == DeviceFaultStatus::Pending)
			{
				fault = std::move(details);
			}
		}

		void RecordRetirement(std::int32_t result) noexcept
		{
			std::scoped_lock lock(mutex);
			retirementAttempted = true;
			retirementResult = result;
		}

	private:
		bool retirementAttempted = false;
		std::int32_t retirementResult = 0;
		const bool faultReportingEnabled;
		std::atomic<bool> lost{ false };
		mutable std::mutex mutex;
		std::array<char, 192> operation{};
		std::size_t operationLength = 0;
		std::int32_t nativeResult = 0;
		DeviceFaultDetails fault;
	};

} // namespace Swim::Rhi
