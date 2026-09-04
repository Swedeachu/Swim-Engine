#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine
{

	template<typename Context>
	class DeferredCommandBuffer
	{
	private:

		class Command
		{
		public:

			template<typename Func>
			explicit Command(Func&& func)
				: function(std::make_unique<CommandModel<std::decay_t<Func>>>(std::forward<Func>(func)))
			{}

			Command(Command&&) noexcept = default;
			Command& operator=(Command&&) noexcept = default;

			Command(const Command&) = delete;
			Command& operator=(const Command&) = delete;

			void Invoke(Context& context)
			{
				function->Invoke(context);
			}

		private:

			struct CommandConcept
			{
				virtual ~CommandConcept() = default;
				virtual void Invoke(Context& context) = 0;
			};

			template<typename Func>
			struct CommandModel final : CommandConcept
			{
				template<typename Incoming>
				explicit CommandModel(Incoming&& func)
					: FuncValue(std::forward<Incoming>(func))
				{}

				void Invoke(Context& context) override
				{
					FuncValue(context);
				}

				Func FuncValue;
			};

			std::unique_ptr<CommandConcept> function;

		};

	public:

		DeferredCommandBuffer() = default;

		DeferredCommandBuffer(const DeferredCommandBuffer&) = delete;
		DeferredCommandBuffer& operator=(const DeferredCommandBuffer&) = delete;
		DeferredCommandBuffer(DeferredCommandBuffer&&) = delete;
		DeferredCommandBuffer& operator=(DeferredCommandBuffer&&) = delete;

		template<typename Func>
		void Enqueue(Func&& command)
		{
			static_assert(std::is_invocable_r_v<void, std::decay_t<Func>&, Context&>,
				"DeferredCommandBuffer commands must be invocable as void(Context&)");

			std::lock_guard<std::mutex> lock(mutex);
			pendingCommands.emplace_back(std::forward<Func>(command));
		}

		std::size_t Flush(Context& context)
		{
			std::vector<Command> commands;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (flushing)
				{
					throw std::logic_error("DeferredCommandBuffer cannot be flushed recursively.");
				}

				flushing = true;
				commands.swap(pendingCommands);
			}

			try
			{
				for (Command& command : commands)
				{
					command.Invoke(context);
				}
			}
			catch (...)
			{
				std::lock_guard<std::mutex> lock(mutex);
				flushing = false;
				throw;
			}

			{
				std::lock_guard<std::mutex> lock(mutex);
				flushing = false;
			}

			return commands.size();
		}

		void Clear()
		{
			std::lock_guard<std::mutex> lock(mutex);
			pendingCommands.clear();
		}

		std::size_t GetPendingCount() const
		{
			std::lock_guard<std::mutex> lock(mutex);
			return pendingCommands.size();
		}

		bool IsFlushing() const
		{
			std::lock_guard<std::mutex> lock(mutex);
			return flushing;
		}

	private:

		mutable std::mutex mutex;
		std::vector<Command> pendingCommands;
		bool flushing = false;

	};

}
