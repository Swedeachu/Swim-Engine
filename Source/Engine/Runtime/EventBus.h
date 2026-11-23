#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine
{

	class EventBus;

	// Keeps a subscription alive; destroying or resetting it unsubscribes. Move-only.
	// Release() keeps the handler for the bus's lifetime instead.
	class EventSubscription
	{

	  public:

		EventSubscription() = default;

		EventSubscription(const EventSubscription&) = delete;

		EventSubscription& operator=(const EventSubscription&) = delete;

		EventSubscription(EventSubscription&& other) noexcept { *this = std::move(other); }

		EventSubscription& operator=(EventSubscription&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				state = std::move(other.state);
				id = std::exchange(other.id, 0);
			}

			return *this;
		}

		~EventSubscription() { Reset(); }

		void Reset();

		void Release()
		{
			state.reset();
			id = 0;
		}

		explicit operator bool() const { return id != 0 && !state.expired(); }

	  private:

		friend class EventBus;
		struct State;

		EventSubscription(std::weak_ptr<State> stateValue, std::uint64_t idValue) : state(std::move(stateValue)), id(idValue) {}

		std::weak_ptr<State> state;
		std::uint64_t id = 0;

	};

	struct EventSubscription::State
	{
		struct Handler
		{
			std::uint64_t Id = 0; // 0: removed, compacted after the publish.
			std::function<void(const void*)> Invoke;
		};

		std::unordered_map<std::type_index, std::vector<Handler>> Lists;
		std::unordered_map<std::uint64_t, std::type_index> Owners;
		std::uint64_t NextId = 0;
		std::uint32_t Publishing = 0;
		bool Dirty = false;

		void Remove(std::uint64_t id)
		{
			const auto owner = Owners.find(id);

			if (owner == Owners.end())
			{
				return;
			}

			for (auto& handler : Lists[owner->second])
			{
				if (handler.Id == id)
				{
					handler.Id = 0;
					handler.Invoke = {};
				}
			}

			Owners.erase(owner);
			Dirty = true;

			if (Publishing == 0)
			{
				Compact();
			}
		}

		void Compact()
		{
			for (auto& [type, list] : Lists)
			{
				(void)type;
				std::erase_if(list, [](const Handler& handler) { return handler.Id == 0; });
			}

			Dirty = false;
		}
	};

	// Typed publish/subscribe for scene, UI and game events: any copyable struct is an event
	// type. Handlers run synchronously in subscription order on the publishing thread
	// (main thread). Subscribing or unsubscribing from a handler is safe; a handler added
	// during a publish first runs on the next one.
	//
	//   struct DoorOpened { entt::entity Door; };
	//   auto token = scene.GetEvents().Subscribe<DoorOpened>([](const DoorOpened& e) { ... });
	//   scene.GetEvents().Publish(DoorOpened{ door });
	class EventBus
	{

	  public:

		EventBus() : state(std::make_shared<EventSubscription::State>()) {}

		EventBus(const EventBus&) = delete;

		EventBus& operator=(const EventBus&) = delete;

		template <typename T> [[nodiscard]] EventSubscription Subscribe(std::function<void(const T&)> handler)
		{
			const auto id = ++state->NextId;
			state->Lists[std::type_index(typeid(T))].push_back(
				{ id, [handler = std::move(handler)](const void* event) { handler(*static_cast<const T*>(event)); } });
			state->Owners.insert_or_assign(id, std::type_index(typeid(T)));
			return EventSubscription(state, id);
		}

		template <typename T> void Publish(const T& event)
		{
			const auto found = state->Lists.find(std::type_index(typeid(T)));

			if (found == state->Lists.end())
			{
				return;
			}

			// A reference stays valid when a handler subscribes a new type (rehash); the
			// iterator would not.
			auto& list = found->second;
			++state->Publishing;
			const auto count = list.size();

			for (std::size_t i = 0; i < count && i < list.size(); ++i)
			{
				if (list[i].Id != 0)
				{
					// Copy: the handler may subscribe and grow the list.
					const auto invoke = list[i].Invoke;
					invoke(&event);
				}
			}

			if (--state->Publishing == 0 && state->Dirty)
			{
				state->Compact();
			}
		}

		// Subscriptions of every type.
		std::size_t GetSubscriberCount() const { return state->Owners.size(); }

	  private:

		std::shared_ptr<EventSubscription::State> state;

	};

	inline void EventSubscription::Reset()
	{
		if (const auto locked = state.lock())
		{
			locked->Remove(id);
		}

		state.reset();
		id = 0;
	}

} // namespace Engine
