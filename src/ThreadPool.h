#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace bsbar
{

	class ThreadPool
	{
	public:
		ThreadPool(uint64_t thread_count) :
			m_running(true)
		{
			m_threads.resize(thread_count);
			for (auto& thread : m_threads)
				thread = std::thread(&ThreadPool::worker_function, this);
		}
		~ThreadPool()
		{
			kill();
			for (auto& thread : m_threads)
				thread.join();
		}

		void kill()
		{
			m_running = false;
			m_cv.notify_all();
		}

		void push_task(std::function<void()> func)
		{
			std::scoped_lock lock(m_mutex);
			m_task_queue.push(std::move(func));
			m_task_count++;
			m_cv.notify_one();
		}

		void wait()
		{
			std::unique_lock lock(m_mutex);
			m_wait_cv.wait(lock, [this]() { return !m_running || m_task_count == 0; });
		}

		template<typename Rep, typename Period>
		void wait_for(const std::chrono::duration<Rep, Period>& duration)
		{
			std::unique_lock lock(m_mutex);
			m_wait_cv.wait_for(lock, duration, [this]() { return !m_running || m_task_count == 0; });
		}

	private:
		void worker_function()
		{
			while (m_running)
			{
				std::unique_lock lock(m_mutex);
				m_cv.wait(lock, [this]() { return !m_running || !m_task_queue.empty(); });
				if (!m_running)
					break;

				auto func = std::move(m_task_queue.front());
				m_task_queue.pop();

				lock.unlock();

				func();

				m_task_count--;
				m_wait_cv.notify_all();
			}
		}

	private:
		std::vector<std::thread>			m_threads;
		std::queue<std::function<void()>>	m_task_queue;

		std::atomic<bool>					m_running;
		std::condition_variable				m_cv;
		std::condition_variable				m_wait_cv;
		std::mutex							m_mutex;

		std::atomic<uint64_t>				m_task_count = 0;
	};

}