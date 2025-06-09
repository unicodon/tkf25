#include "workqueue.h"

void Workqueue::enqueue(Work::CoroutineHandle handle)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_queue.emplace(std::move(handle), Work::Clock::time_point::min());
	wakeup();
}

void Workqueue::enqueue(Work::CoroutineHandle handle, Work::Clock::time_point schedule)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	bool needsReschedule = true;
	if (!m_queue.empty()) {
		needsReschedule = schedule < m_queue.top().m_schedule;
	}
	m_queue.emplace(std::move(handle), schedule);
	if (needsReschedule) {
		wakeup();
	}
}

void Workqueue::wakeup()
{
	m_cond.notify_all();
}

void Workqueue::run()
{
	while (true) {
		const bool wait = true;
		executeExpired(wait);
	}
}

std::optional<Workqueue::Work::Clock::time_point> Workqueue::executeExpired(bool wait)
{
	std::optional<Workqueue::Work::Clock::time_point> nextSchedule;
	std::vector<Work> execQueue; // priority queue でない
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (wait) {
			if (m_queue.empty()) {
				// Workがないので無条件で待つ
				m_cond.wait(lock);
			}
			else {
				// 直近にスケジュールされた時間まで待つ
				auto schedule = m_queue.top().m_schedule;
				if (schedule > Work::Clock::now()) {
					m_cond.wait_until(lock, schedule);
				}
			}
		}

		// スケジュールが現在時刻を過ぎているものを execQueue に移す。
		auto now = Work::Clock::now();
		while (!m_queue.empty() && m_queue.top().m_schedule <= now) {
			printf("shed %lld\n", m_queue.top().m_schedule.time_since_epoch().count());
			execQueue.emplace_back(std::move(m_queue.top()));
			m_queue.pop();
		}

		if (!m_queue.empty()) {
			nextSchedule = m_queue.top().m_schedule;

			auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(*nextSchedule - now);
			printf("m_queue.size() = %zu, delay = %lld\n", m_queue.size(), delay.count());
		}
	}

	// execQueue のものを実行
	for (auto& work : execQueue) {
		work.m_handle.resume();
	}

	return nextSchedule;
}

Schedule shedule(Workqueue& wq)
{
	return Schedule{ wq };
}