#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <functional>
#include <stdexcept>

namespace Swim::UI
{

	void UiDocument::Impl::QueueEvent(UiEvent event)
	{
		Events.push_back(event);
		const auto found = Nodes.find(event.Node.Value);

		if (found == Nodes.end())
		{
			return;
		}

		// Handlers and value bindings both run at dispatch.
		const auto& node = found->second;

		if (node.Callbacks.contains(event.Kind) || (event.Kind == UiEventKind::ValueChanged && node.Binding))
		{
			CallbackEvents.push_back(event);
		}
	}

	void UiDocument::On(UiNodeId node, UiEventKind kind, std::function<void(const UiEvent&)> handler)
	{
		if (kind > UiEventKind::ContextMenu)
		{
			throw std::invalid_argument("Invalid UI callback event kind");
		}

		auto& callbacks = impl->Get(node).Callbacks;

		if (handler)
		{
			callbacks[kind] = std::make_shared<const std::function<void(const UiEvent&)>>(std::move(handler));
		}
		else
		{
			callbacks.erase(kind);
		}
	}

	void UiDocument::OnClick(UiNodeId node, std::function<void()> handler)
	{
		if (!handler)
		{
			On(node, UiEventKind::Click, {});
			return;
		}

		On(node, UiEventKind::Click,
			[handler = std::move(handler)](const UiEvent&)
			{
				handler();
			});
	}

	void UiDocument::OnValue(UiNodeId node, std::function<void(float)> handler)
	{
		if (!handler)
		{
			On(node, UiEventKind::ValueChanged, {});
			return;
		}

		On(node, UiEventKind::ValueChanged,
			[handler = std::move(handler)](const UiEvent& event)
			{
				handler(event.Value);
			});
	}

	void UiDocument::OnChecked(UiNodeId node, std::function<void(bool)> handler)
	{
		if (!handler)
		{
			On(node, UiEventKind::ValueChanged, {});
			return;
		}

		On(node, UiEventKind::ValueChanged,
			[handler = std::move(handler)](const UiEvent& event)
			{
				handler(event.Value == 1.0f);
			});
	}

	void UiDocument::OnText(UiNodeId node, std::function<void(const std::string&)> handler)
	{
		if (!handler)
		{
			On(node, UiEventKind::TextChanged, {});
			return;
		}

		On(node, UiEventKind::TextChanged,
			[this, handler = std::move(handler)](const UiEvent& event)
			{
				// Own the string while gameplay may change text or remove the node.
				const std::string text = GetText(event.Node);
				handler(text);
			});
	}

	void UiDocument::ClearCallbacks(UiNodeId node)
	{
		auto& target = impl->Get(node);
		target.Callbacks.clear();
		target.Binding.reset(); // Bindings capture gameplay state too.
	}

	void UiDocument::ClearCallbacks()
	{
		for (auto& [id, node] : impl->Nodes)
		{
			(void)id;
			node.Callbacks.clear();
			node.Binding.reset();
		}

		impl->CallbackEvents.clear();
		impl->BoundNodes.clear();
	}

	void UiDocument::DispatchCallbacks()
	{
		if (impl->DispatchingCallbacks)
		{
			return;
		}

		struct DispatchGuard
		{
			bool& Active;
			std::vector<UiEvent>& Scratch;

			~DispatchGuard()
			{
				Active = false;
				Scratch.clear(); // Keeps its capacity for the next dispatch.
			}
		};

		// Events created by handlers go to the (now empty) queue for the next dispatch.
		impl->DispatchingCallbacks = true;
		impl->DispatchScratch.clear();
		impl->DispatchScratch.swap(impl->CallbackEvents);
		DispatchGuard guard{ impl->DispatchingCallbacks, impl->DispatchScratch };

		for (std::size_t i = 0; i < impl->DispatchScratch.size(); ++i)
		{
			const UiEvent event = impl->DispatchScratch[i];
			auto* node = impl->Find(event.Node);

			if (!node)
			{
				continue;
			}

			// The bound value first, so a handler reading it sees the new value.
			if (event.Kind == UiEventKind::ValueChanged && node->Binding)
			{
				impl->PushBinding(event);
				node = impl->Find(event.Node);

				if (!node)
				{
					continue;
				}
			}

			const auto found = node->Callbacks.find(event.Kind);

			if (found != node->Callbacks.end())
			{
				// Hold the handler (a reference count, not a copy): it may replace itself or erase its node.
				const auto handler = found->second;
				(*handler)(event);
			}
		}
	}

} // namespace Swim::UI
