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

		explicit SceneToolingBridge(SendCallback callback = {})
			: callback(std::move(callback))
		{}

		bool IsConnected() const { return static_cast<bool>(callback); }

		bool Send(std::string message, std::uintptr_t channel) const
		{
			return callback && callback(message, channel);
		}

	private:

		SendCallback callback;

	};

}
