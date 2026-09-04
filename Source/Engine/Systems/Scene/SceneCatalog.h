#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Engine
{

	class Scene;

	class SceneCatalog
	{

	public:

		using Factory = std::function<std::shared_ptr<Scene>(const std::string&)>;

		struct Descriptor
		{
			std::string Name;
			Factory Create;
		};

		void Register(std::string name, Factory factory)
		{
			if (name.empty())
			{
				throw std::invalid_argument("Scene type name cannot be empty.");
			}
			if (!factory)
			{
				throw std::invalid_argument("Scene type '" + name + "' has no factory.");
			}
			if (Contains(name))
			{
				throw std::runtime_error("Scene type '" + name + "' is already registered.");
			}

			descriptors.push_back({ std::move(name), std::move(factory) });
		}

		bool Contains(std::string_view name) const
		{
			for (const Descriptor& descriptor : descriptors)
			{
				if (descriptor.Name == name)
				{
					return true;
				}
			}

			return false;
		}

		const std::vector<Descriptor>& GetDescriptors() const { return descriptors; }

	private:

		std::vector<Descriptor> descriptors;

	};

}
