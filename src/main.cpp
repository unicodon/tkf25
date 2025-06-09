#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <coroutine>
#include <memory>
#include "workqueue.h"
#include "curl_workqueue.h"
#include "gif.h"

Workqueue* g_mainWQ;

void platform_init();

void enqueueCoroutine(std::coroutine_handle<> handle)
{
	g_mainWQ->enqueue(handle);
}

void enqueueCoroutine(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point schedule)
{
	g_mainWQ->enqueue(handle, schedule);
}

unifex::task<void> main_task();

void SetImage(const std::vector<uint32_t>& image, int width, int height, int index)
{
	// Do nothing
}

int main()
{
	platform_init();

	g_mainWQ = new Workqueue();

	std::thread{ []() {
		unifex::sync_wait(main_task());
	} }.detach();

	g_mainWQ->run();
	return 0;
}