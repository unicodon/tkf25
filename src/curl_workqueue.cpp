#include "curl_workqueue.h"

#include <unordered_set>
#include <curl/curl.h>

CurlWorkqueue::CurlReader::CurlReader(const char* url, CurlWorkqueue& wq)
	: m_wq(wq)
{
	m_curl = curl_easy_init();
	curl_easy_setopt(m_curl, CURLOPT_URL, url);
	curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "tkf/1.0");
	curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_multi_add_handle(m_wq.multi(), m_curl);
}

CurlWorkqueue::CurlReader::~CurlReader()
{
	curl_multi_remove_handle(m_wq.multi(), m_curl);
	curl_easy_cleanup(m_curl);
}

CurlWorkqueue::CurlWorkqueue()
{
	m_multi = curl_multi_init();
}

void CurlWorkqueue::enqueue(Work::Condition&& condition, Work::CoroutineHandle handle, CURL* curl)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_queue.emplace_back(std::move(condition), handle, curl);
	wakeup();
}

void CurlWorkqueue::enqueue(Work::CoroutineHandle handle)
{
	enqueue([](bool) { return true; }, handle, nullptr);
}

void CurlWorkqueue::wakeup()
{
	m_cond.notify_all();
}

void CurlWorkqueue::run()
{
	int running_handles;
	int numfds;
	CURLMcode mcode;

	decltype(m_queue) execQueue;
	std::unordered_set<CURL*> doneHandles;

	while (true) {
		mcode = curl_multi_perform(m_multi, &running_handles);
		if (running_handles != 0) {
			mcode = curl_multi_wait(m_multi, nullptr, 0, 1000, &numfds);
		}

		struct CURLMsg* m;
		do {
			int msgq = 0;
			m = curl_multi_info_read(m_multi, &msgq);
			if (m && (m->msg == CURLMSG_DONE)) {
				CURL* curl = m->easy_handle;
				doneHandles.insert(curl);
			}
		} while (m);

		bool wait = (running_handles == 0);
		execQueue.clear();
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if (wait) {
				if (m_queue.empty()) {
					// WorkÇ™Ç»Ç¢ÇÃÇ≈ñ≥èåèÇ≈ë“Ç¬
					m_cond.wait(lock);
				}
			}

			for (auto it = m_queue.begin(); it != m_queue.end();) {
				bool done = (doneHandles.find(it->m_curl) != doneHandles.end());
				if (it->m_condition(done)) {
					execQueue.push_back(std::move(*it));
					it = m_queue.erase(it);
				}
				else {
					it++;
				}
			}
		}

		// execQueue ÇÃÇ‡ÇÃÇé¿çs
		for (auto& work : execQueue) {
			work.m_handle.resume();
		}

		execQueue.clear();
		doneHandles.clear();
	}
}

void CurlWorkqueue::executeExpired(bool wait)
{
}
