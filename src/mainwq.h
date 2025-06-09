#pragma once

#include <chrono>
#include <coroutine>

void enqueueCoroutine(std::coroutine_handle<> handle);
void enqueueCoroutine(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point schedule);

[[nodiscard]]
auto sheduleOnMainWQ()
{
	struct Awaitable {
		bool await_ready() { return false; }
		bool await_suspend(std::coroutine_handle<> h)
		{
			enqueueCoroutine(h);
			return true;
		}
		void await_resume() {}
	};

	return Awaitable{};
}

template <class _Rep, class _Period>
[[nodiscard]]
auto sheduleOnMainWQ(const std::chrono::duration<_Rep, _Period>& timeout)
{
	struct Awaitable {
		bool await_ready() { return false; }
		bool await_suspend(std::coroutine_handle<> h)
		{
			enqueueCoroutine(h, schedule);
			return true;
		}
		void await_resume() {}

		std::chrono::steady_clock::time_point schedule;
	};

	return Awaitable{ std::chrono::steady_clock::now() + timeout };
}
