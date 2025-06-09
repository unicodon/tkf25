#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <mutex>
#include <queue>
#include <optional>

class Workqueue {
public:
	struct Work {
		using Clock = std::chrono::steady_clock;
		using CoroutineHandle = std::coroutine_handle<>;
		Work(CoroutineHandle handle, Clock::time_point schedule)
			: m_handle(handle)
			, m_schedule(schedule)
		{
		}
		Work(const Work& rhs) noexcept
			: m_handle(rhs.m_handle)
			, m_schedule(rhs.m_schedule)
		{
		}
		Work& operator=(const Work& rhs) noexcept
		{
			m_handle = rhs.m_handle;
			m_schedule = rhs.m_schedule;
			return *this;
		}

		Work() = delete;

		bool operator > (const Work& rhs) const { return m_schedule > rhs.m_schedule; }
		CoroutineHandle m_handle;
		Clock::time_point m_schedule;
	};
	using Queue = std::priority_queue<Work, std::vector<Work>, std::greater<Work> >;

	Workqueue() = default;
	~Workqueue() = default;

	void enqueue(Work::CoroutineHandle handle);
	void enqueue(Work::CoroutineHandle handle, Work::Clock::time_point schedule);
	virtual void run();

protected:
	virtual void wakeup();
	std::optional<Work::Clock::time_point> executeExpired(bool wait);

	Queue m_queue;

	std::mutex m_mutex;
	std::condition_variable m_cond;
};

class Schedule {
public:
	bool await_ready() { return false; }
	bool await_suspend(std::coroutine_handle<> h)
	{
		wq.enqueue(h, schedule);
		return true;
	}
	void await_resume() {}

	explicit Schedule(Workqueue& wq, Workqueue::Work::Clock::time_point schedule) : wq(wq), schedule(schedule) {}
#ifdef min
#undef min
#endif
	explicit Schedule(Workqueue& wq) : Schedule(wq, Workqueue::Work::Clock::time_point::min()) {}
private:
	Workqueue& wq;
	Workqueue::Work::Clock::time_point schedule;
};

[[nodiscard]]
Schedule shedule(Workqueue& wq);

template <class _Rep, class _Period>
[[nodiscard]]
Schedule sleep(const std::chrono::duration<_Rep, _Period>& timeout, Workqueue& wq)
{
	return Schedule{ wq, Workqueue::Work::Clock::now() + timeout };
}
