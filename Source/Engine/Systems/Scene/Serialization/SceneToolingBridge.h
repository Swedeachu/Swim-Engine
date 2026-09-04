// LEGACY/DORMANT: external scene-tool transport is not runtime-wired. Future editor
// features are in-process engine UI and do not use this callback transport.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace Engine
{

	class SceneToolingBridge
	{
	public:

		using SendCallback = std::function<bool(const std::string&, std::uintptr_t)>;
		using IsConnectedCallback = std::function<bool()>;

		explicit SceneToolingBridge(SendCallback callback = {}, IsConnectedCallback isConnected = {})
			: callback(std::move(callback)), isConnected(std::move(isConnected))
		{}

		bool IsConnected() const
		{
			if (!callback)
			{
				return false;
			}

			// A sender existing does not mean an editor process is actually attached.
			// Standalone engine runs always have a sender callback routed through
			// SwimEngine, so use the explicit availability provider when present.
			return isConnected ? isConnected() : true;
		}

		bool Send(std::string message, std::uintptr_t channel) const
		{
			return IsConnected() && callback(message, channel);
		}

	private:

		SendCallback callback;
		IsConnectedCallback isConnected;

	};

}
