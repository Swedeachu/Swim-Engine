#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <stdexcept>

namespace Swim::UI
{

	void UiDocument::Impl::QueueEvent(UiEvent event)
	{
		Events.push_back(event);
		const auto found = Nodes.find(event.Node.Value);

		if (found != Nodes.end() && found->second.Callbacks.contains(event.Kind))
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
			callbacks[kind] = std::move(handler);
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
		impl->Get(node).Callbacks.clear();
	}

	void UiDocument::ClearCallbacks()
	{
		for (auto& [id, node] : impl->Nodes)
		{
			(void)id;
			node.Callbacks.clear();
		}

		impl->CallbackEvents.clear();
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

			~DispatchGuard() { Active = false; }
		};

		impl->DispatchingCallbacks = true;
		DispatchGuard guard{ impl->DispatchingCallbacks };
		std::vector<UiEvent> events;
		events.swap(impl->CallbackEvents);

		for (const auto& event : events)
		{
			const auto node = impl->Nodes.find(event.Node.Value);

			if (node == impl->Nodes.end())
			{
				continue;
			}

			const auto found = node->second.Callbacks.find(event.Kind);

			if (found != node->second.Callbacks.end())
			{
				// Copy before invoking: the handler can replace itself or erase its node.
				const auto handler = found->second;
				handler(event);
			}
		}
	}

} // namespace Swim::UI
