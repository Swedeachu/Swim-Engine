#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Rhi
{

	enum class DiagnosticSeverity : std::uint8_t
	{
		Info,
		Warning,
		Error
	};

	struct DiagnosticMessage
	{
		DiagnosticSeverity Severity = DiagnosticSeverity::Info;
		std::string Id;
		std::string Text;
	};

	struct DiagnosticSnapshot
	{
		std::vector<DiagnosticMessage> Messages;
		std::uint64_t Warnings = 0;
		std::uint64_t Errors = 0;
		std::uint64_t Dropped = 0;

		bool IsClean() const
		{
			return Warnings == 0 && Errors == 0 && Dropped == 0;
		}
	};

	// One owned log per graphics-system lifetime. Native callbacks can arrive on
	// multiple threads. Copies are bounded, and failures never unwind into a driver.
	// Retain this object past graphics-system destruction to inspect teardown too.
	class DiagnosticLog
	{
	public:
		explicit DiagnosticLog(std::size_t capacity = 256) : capacity(std::min(capacity, std::size_t{ 4096 }))
		{
		}

		void Record(DiagnosticSeverity severity, std::string_view id, std::string_view text) noexcept
		{
			if (severity == DiagnosticSeverity::Warning)
			{
				++warnings;
			}
			else if (severity == DiagnosticSeverity::Error)
			{
				++errors;
			}
			try
			{
				std::scoped_lock lock(mutex);
				if (messages.size() >= capacity)
				{
					++dropped;
					return;
				}
				messages.push_back({ severity, std::string(id.substr(0, 256)), std::string(text.substr(0, 8192)) });
			}
			catch (...)
			{
				++dropped;
			}
		}

		DiagnosticSnapshot Snapshot() const
		{
			std::scoped_lock lock(mutex);
			return { messages, warnings.load(), errors.load(), dropped.load() };
		}

	private:
		const std::size_t capacity;
		mutable std::mutex mutex;
		std::vector<DiagnosticMessage> messages;
		std::atomic<std::uint64_t> warnings{ 0 };
		std::atomic<std::uint64_t> errors{ 0 };
		std::atomic<std::uint64_t> dropped{ 0 };
	};

	enum class ValidationMode : std::uint8_t
	{
		Default, // Request in Debug; disable in other configurations.
		Disabled,
		IfAvailable,
		Required // Creation fails unless validation and diagnostic capture are active.
	};

	struct GraphicsSystemDesc
	{
		ValidationMode Validation = ValidationMode::Default;
		std::shared_ptr<DiagnosticLog> Diagnostics;
		bool EchoDiagnostics = true;
		bool DeviceFaultDiagnostics = true; // Optional native details, never a device requirement.
	};

} // namespace Swim::Rhi
