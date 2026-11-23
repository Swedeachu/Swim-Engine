#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Engine
{

	class SceneTimers;

	struct TimerHandle
	{
		std::uint32_t Index = 0;
		std::uint32_t Generation = 0;

		explicit operator bool() const { return Generation != 0; }

		bool operator==(const TimerHandle&) const = default;
	};

	// A coroutine run by SceneTimers: co_await Wait(seconds), WaitUntil(predicate) and
	// NextFrame() suspend it on scene time (paused with the scene). Start it with
	// SceneTimers::Start (or Behavior::StartCoroutine, which stops it with the behaviour).
	//
	//   Engine::SceneTask Blink(Engine::Scene& scene, entt::entity lamp)
	//   {
	//       for (int i = 0; i < 3; ++i)
	//       {
	//           co_await Engine::Wait(0.5);
	//           Toggle(scene, lamp);
	//       }
	//   }
	class SceneTask
	{

	  public:

		struct promise_type
		{
			double WakeTime = 0.0;
			std::function<bool()> Until;

			SceneTask get_return_object() { return SceneTask(std::coroutine_handle<promise_type>::from_promise(*this)); }

			std::suspend_always initial_suspend() noexcept { return {}; }

			std::suspend_always final_suspend() noexcept { return {}; }

			void return_void() {}

			void unhandled_exception() { std::terminate(); }
		};

		SceneTask() = default;

		SceneTask(const SceneTask&) = delete;

		SceneTask& operator=(const SceneTask&) = delete;

		SceneTask(SceneTask&& other) noexcept : handle(std::exchange(other.handle, {})) {}

		SceneTask& operator=(SceneTask&& other) noexcept
		{
			if (this != &other)
			{
				if (handle)
				{
					handle.destroy();
				}

				handle = std::exchange(other.handle, {});
			}

			return *this;
		}

		~SceneTask()
		{
			if (handle)
			{
				handle.destroy();
			}
		}

	  private:

		friend class SceneTimers;

		explicit SceneTask(std::coroutine_handle<promise_type> value) : handle(value) {}

		std::coroutine_handle<promise_type> handle;

	};

	struct SceneWait
	{
		double Seconds = 0.0; // 0: the next frame.

		bool await_ready() const noexcept { return false; }

		void await_suspend(std::coroutine_handle<SceneTask::promise_type> handle) const noexcept { handle.promise().WakeTime = -Seconds; }

		void await_resume() const noexcept {}
	};

	struct SceneWaitUntil
	{
		std::function<bool()> Predicate;

		bool await_ready() const { return Predicate && Predicate(); }

		void await_suspend(std::coroutine_handle<SceneTask::promise_type> handle) { handle.promise().Until = std::move(Predicate); }

		void await_resume() const noexcept {}
	};

	inline SceneWait Wait(double seconds) { return { seconds > 0.0 ? seconds : 0.0 }; }

	inline SceneWait NextFrame() { return {}; }

	inline SceneWaitUntil WaitUntil(std::function<bool()> predicate) { return { std::move(predicate) }; }

	// Delays, repeating timers, wait-until callbacks and coroutines on the scene's time
	// (scaled; nothing advances while the simulation is paused). Ticked after behaviours
	// update; callbacks may add or cancel timers. An owner token (a weak pointer) cancels
	// everything it owns once it expires, so a timer never outlives what it captured.
	class SceneTimers
	{

	  public:

		using Owner = std::weak_ptr<const void>;

		TimerHandle After(double seconds, std::function<void()> callback, Owner owner = {});

		// Every interval seconds until cancelled (the first call after one interval).
		TimerHandle Every(double interval, std::function<void()> callback, Owner owner = {});

		// Calls callback once, the first tick predicate returns true.
		TimerHandle When(std::function<bool()> predicate, std::function<void()> callback, Owner owner = {});

		TimerHandle Start(SceneTask task, Owner owner = {});

		bool Cancel(TimerHandle timer);

		bool IsActive(TimerHandle timer) const;

		void Tick(double dt);

		void Clear();

		double GetTime() const { return time; }

		std::size_t GetActiveCount() const;

	  private:

		struct Entry
		{
			std::uint32_t Generation = 0;
			bool Active = false;
			bool Owned = false;
			std::uint64_t AddedTick = 0;
			double Due = 0.0;
			double Interval = 0.0;
			std::function<void()> Callback;
			std::function<bool()> Predicate;
			SceneTask Task;
			Owner OwnerToken;
		};

		TimerHandle Add(Entry entry);

		void Release(Entry& entry);

		std::vector<Entry> entries;
		std::vector<std::uint32_t> freeList;
		double time = 0.0;
		std::uint64_t ticks = 0;

	};

} // namespace Engine
