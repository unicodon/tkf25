#pragma once

#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <mutex>
#include <queue>

typedef void CURLM;
typedef void CURL;

class CurlWorkqueue {
public:
	friend class CurlReader;
	class CurlReader {
	public:
		using Buffer = std::vector<std::byte>;

		CurlReader(const char* url, CurlWorkqueue& wq);
		~CurlReader();

		bool eof() const
		{
			return m_done && m_buffers.empty();
		}

		friend struct ReadAwaiter;
		struct ReadAwaiter {
			bool await_ready()
			{
				return tryRead();
			}

			bool await_suspend(std::coroutine_handle<> h)
			{
				if (tryRead()) {
					return false;
				}

				m_reader.m_wq.enqueue([this](bool done) -> bool {
					m_reader.m_done = done;
					if (done) {
						return true;
					}
					return tryRead();
					}, h, m_reader.m_curl);

				return true;
			}

			size_t await_resume()
			{
				return m_read;
			}

			bool tryRead()
			{
				if (m_reader.eof()) {
					return true;
				}
				size_t read = m_reader.read(m_buf, m_size);
				m_read += read;
				m_size -= read;
				m_buf += read;
				return m_size == 0;
			}

			explicit ReadAwaiter(CurlReader& reader, std::byte* buf, size_t size)
				: m_reader(reader)
				, m_buf(buf)
				, m_size(size)
			{
			}

			CurlReader& m_reader;
			std::byte* m_buf;
			size_t m_size;
			size_t m_read = 0;
		};

		auto read(void* buf, size_t size)
		{
			//printf("read:%zu\n", size);
			return ReadAwaiter{ *this, static_cast<std::byte*>(buf), size };
		}

	private:
		size_t write(char* ptr, size_t size, size_t nmemb)
		{
			size_t realSize = size * nmemb;
			//printf("write:%zd\n", realSize);
			m_buffers.emplace_back(realSize);
			auto it = m_buffers.rbegin();
			memcpy(it->data(), ptr, it->size());
			return realSize;
		}

		static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
		{
			return static_cast<CurlReader*>(userdata)->write(ptr, size, nmemb);
		}

		size_t read(std::byte* buf, size_t size)
		{
			if (eof()) {
				return 0;
			}
			size_t read = 0;
			while (size > 0 && !m_buffers.empty()) {
				auto it = m_buffers.begin();
				if (size <= it->size()) {
					size_t remaining = it->size() - size;
					memcpy(buf, it->data(), size);
					memmove(it->data(), it->data() + size, remaining);
					it->resize(remaining);
					read += size;
					break;
				}
				else {
					memcpy(buf, it->data(), it->size());
					read += it->size();
					buf += it->size();
					size -= it->size();
					m_buffers.pop_front();
				}
			}
			return read;
		}

		CurlWorkqueue& m_wq;
		CURL* m_curl;
		std::list<Buffer> m_buffers;
		bool m_done = false;
	};

	struct Work {
		using Condition = std::function<bool(bool done)>;
		using CoroutineHandle = std::coroutine_handle<>;
		Work(Condition&& condition, CoroutineHandle handle, CURL* curl)
			: m_condition(std::move(condition))
			, m_handle(handle)
			, m_curl(curl)
		{
		}
		Work(const Work&& rhs)
			: m_condition(std::move(rhs.m_condition))
			, m_handle(rhs.m_handle)
			, m_curl(rhs.m_curl)
		{
		}
		Work& operator=(const Work&& rhs)
		{
			m_condition = std::move(rhs.m_condition);
			m_handle = rhs.m_handle;
			m_curl = rhs.m_curl;
			return *this;
		}

		Work() = delete;
		Work(const Work&) = delete;
		Work& operator=(const Work&) = delete;

		Condition m_condition;
		CoroutineHandle m_handle;
		CURL* m_curl;
	};
	using Queue = std::list<Work>;

	CurlWorkqueue();
	~CurlWorkqueue() = default;

	void enqueue(Work::Condition&& condition, Work::CoroutineHandle handle, CURL* curl);
	void enqueue(Work::CoroutineHandle handle);

	virtual void run();


protected:
	void wakeup();
	void executeExpired(bool wait);

	CURLM* multi() { return m_multi; }

	Queue m_queue;

	std::mutex m_mutex;
	std::condition_variable m_cond;
	CURLM* m_multi;
};

[[nodiscard]]
inline auto shedule(CurlWorkqueue& wq)
{
	struct Awaitable {
	public:
		bool await_ready() { return false; }
		bool await_suspend(std::coroutine_handle<> h)
		{
			wq.enqueue(h);
			return true;
		}
		void await_resume() {}

		explicit Awaitable(CurlWorkqueue& wq) : wq(wq) {}
	private:
		CurlWorkqueue& wq;
	};

	return Awaitable{ wq };
}
