#pragma once

#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace Swim::RhiVulkan
{

	// One thread that makes every queue submission and presentation of a device, in the order
	// they were issued (Device::SetSubmissionThread). A driver whose present blocks (until its
	// frame's GPU work is done, or while the image crosses adapters) and holds back submissions
	// meanwhile then stalls this thread instead of the one recording frames. Binary semaphores
	// keep their signal-before-wait order because everything goes through one ordered queue.
	class VulkanSubmissionWorker
	{

	  public:
		VulkanSubmissionWorker()
			: thread(
				  [this]
				  {
					  Run();
				  })
		{
		}

		~VulkanSubmissionWorker()
		{
			{
				std::scoped_lock lock(mutex);
				stop = true;
			}

			signal.notify_all();
			thread.join();
		}

		VulkanSubmissionWorker(const VulkanSubmissionWorker&) = delete;

		VulkanSubmissionWorker& operator=(const VulkanSubmissionWorker&) = delete;

		// Runs work later, in order. An exception it throws is rethrown by the next Post,
		// Call or Drain.
		void Post(std::function<void()> work)
		{
			std::unique_lock lock(mutex);
			RethrowLocked(lock);
			jobs.push_back(std::move(work));
			signal.notify_all();
		}

		// Runs work after everything posted before it and waits for it (rethrowing its error).
		void Call(const std::function<void()>& work)
		{
			std::exception_ptr own;
			bool done = false;
			Post(
				[&]
				{
					try
					{
						work();
					}
					catch (...)
					{
						own = std::current_exception();
					}

					std::scoped_lock lock(mutex);
					done = true;
				});
			std::unique_lock lock(mutex);
			signal.wait(lock,
				[&]
				{
					return done;
				});

			if (own)
			{
				std::rethrow_exception(own);
			}
		}

		// Waits until everything posted has run.
		void Drain()
		{
			std::unique_lock lock(mutex);
			signal.wait(lock,
				[this]
				{
					return jobs.empty() && !busy;
				});
			RethrowLocked(lock);
		}

		bool IsWorkerThread() const { return std::this_thread::get_id() == thread.get_id(); }

	  private:
		void RethrowLocked(std::unique_lock<std::mutex>&)
		{
			if (error)
			{
				auto pending = error;
				error = nullptr;
				std::rethrow_exception(pending);
			}
		}

		void Run()
		{
			std::unique_lock lock(mutex);

			while (true)
			{
				signal.wait(lock,
					[this]
					{
						return stop || !jobs.empty();
					});

				if (jobs.empty())
				{
					return; // Stopped with nothing left.
				}

				auto work = std::move(jobs.front());
				jobs.pop_front();
				busy = true;
				lock.unlock();

				try
				{
					work();
				}
				catch (...)
				{
					std::scoped_lock errorLock(mutex);
					error = std::current_exception();
				}

				lock.lock();
				busy = false;
				signal.notify_all();
			}
		}

		std::mutex mutex;
		std::condition_variable signal;
		std::deque<std::function<void()>> jobs;
		std::exception_ptr error;
		bool busy = false;
		bool stop = false;
		std::thread thread;
	};

} // namespace Swim::RhiVulkan
