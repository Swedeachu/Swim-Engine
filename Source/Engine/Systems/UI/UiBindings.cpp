#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

// Two-way value bindings: a control shows a value owned elsewhere and writes edits back, so
// gameplay code binds once instead of tracking which control shows what. Input edits are
// pushed at callback dispatch; values changed elsewhere are pulled every Update (from code,
// no events). Only bound nodes are visited.
namespace Swim::UI
{

	void UiDocument::BindValue(UiNodeId id, std::function<float()> get, std::function<void(float)> set)
	{
		auto& node = impl->Get(id);

		if (!node.Behavior)
		{
			throw std::invalid_argument("Only controls can bind a value");
		}

		if (!get)
		{
			throw std::invalid_argument("A UI value binding needs a getter");
		}

		node.Binding = std::make_unique<Impl::Node::ValueBinding>(Impl::Node::ValueBinding{ std::move(get), std::move(set) });

		if (std::find(impl->BoundNodes.begin(), impl->BoundNodes.end(), id) == impl->BoundNodes.end())
		{
			impl->BoundNodes.push_back(id);
		}

		impl->PullBindings(); // Shows the value now.
	}

	void UiDocument::BindValue(UiNodeId id, float& value)
	{
		float* target = &value;
		BindValue(
			id,
			[target]
			{
				return *target;
			},
			[target](float v)
			{
				*target = v;
			});
	}

	void UiDocument::BindChecked(UiNodeId id, bool& value)
	{
		bool* target = &value;
		BindValue(
			id,
			[target]
			{
				return *target ? 1.0f : 0.0f;
			},
			[target](float v)
			{
				*target = v == 1.0f; // Mixed (0.5) reads as off.
			});
	}

	void UiDocument::Unbind(UiNodeId id)
	{
		impl->Get(id).Binding.reset();
		std::erase(impl->BoundNodes, id);
	}

	void UiDocument::Impl::PushBinding(const UiEvent& event)
	{
		auto* node = Find(event.Node);

		if (node && node->Binding && node->Binding->Set)
		{
			// Hold the setter: it may unbind or remove the node.
			const auto set = node->Binding->Set;
			set(event.Value);
		}
	}

	void UiDocument::Impl::PullBindings()
	{
		std::size_t write = 0;

		for (std::size_t read = 0; read < BoundNodes.size(); ++read)
		{
			const auto id = BoundNodes[read];
			auto* node = Find(id);

			if (!node || !node->Binding || !node->Behavior)
			{
				continue; // Removed or unbound: dropped from the list.
			}

			BoundNodes[write++] = id;

			if (Dragging == id)
			{
				continue; // Never fight the pointer.
			}

			const float wanted = node->Binding->Get();
			auto context = Context(id);
			const float shown = node->Behavior->GetValue(context);

			if (std::isfinite(wanted) && std::abs(shown - wanted) > 1.0e-6f * std::max(1.0f, std::abs(wanted)))
			{
				node->Behavior->SetValue(context, wanted); // From code: no events, no feedback loop.
			}
		}

		BoundNodes.resize(write);
	}

} // namespace Swim::UI
