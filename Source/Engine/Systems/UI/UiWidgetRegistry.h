#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace Swim::UI
{

	class UiWidgetRegistry
	{

	  public:

		UiWidgetRegistry(); // Registers the engine widgets through Register, like custom widgets.

		template <class Options, class Result = UiNodeId, class Factory> void Register(std::string name, Factory factoryValue)
		{
			std::function<Result(UiDocument&, UiNodeId, const Options&)> factory(std::move(factoryValue));

			if (name.empty() || !factory || factories.contains(name))
			{
				throw std::invalid_argument("UI widget needs a unique name and factory");
			}

			factories.emplace(std::move(name),
				Entry{ typeid(Options), typeid(Result),
					[factory = std::move(factory)](UiDocument& document, UiNodeId parent, const void* options) -> std::any
					{
						return factory(document, parent, *static_cast<const Options*>(options));
					} });
		}

		template <class Result = UiNodeId, class Options>
		Result Create(std::string_view name, UiDocument& document, UiNodeId parent, const Options& options) const
		{
			const auto found = factories.find(std::string(name));

			if (found == factories.end() || found->second.OptionsType != typeid(Options) || found->second.ResultType != typeid(Result))
			{
				throw std::invalid_argument("Unknown UI widget or incompatible options/result: " + std::string(name));
			}

			if (!document.Contains(parent))
			{
				throw std::invalid_argument("UI widget parent is not in the document");
			}

			// A custom factory can register other factories without invalidating this call.
			const auto factory = found->second.Factory;
			return std::any_cast<Result>(factory(document, parent, &options));
		}

		bool Contains(std::string_view name) const { return factories.contains(std::string(name)); }

	  private:

		struct Entry
		{
			std::type_index OptionsType;
			std::type_index ResultType;
			std::function<std::any(UiDocument&, UiNodeId, const void*)> Factory;
		};

		std::unordered_map<std::string, Entry> factories;

	};

} // namespace Swim::UI
