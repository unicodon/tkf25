#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <map>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include "workqueue.h"
#include "mainwq.h"
#include "gif.h"
#include <windows.h>

unifex::task<void> main_task();

HWND g_hwnd;

class WinWorkqueue : public Workqueue {
public:
	void execute(HWND hwnd)
	{
		::KillTimer(hwnd, 0);
		auto nextSchedule = executeExpired(false);
		if (nextSchedule) {
			auto now = Workqueue::Work::Clock::now();
			if (now < *nextSchedule) {
				auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(*nextSchedule - now);
				::SetTimer(hwnd, 0, delay.count(), NULL);
			}
			else {
				::PostMessage(hwnd, WM_USER, 0, 0);
			}
		}
	}

	virtual void wakeup() override
	{
		::PostMessage(g_hwnd, WM_USER, 0, 0);
	}
};

WinWorkqueue* g_mainWQ = nullptr;

void enqueueCoroutine(std::coroutine_handle<> handle)
{
	g_mainWQ->enqueue(handle);
}

void enqueueCoroutine(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point schedule)
{
	g_mainWQ->enqueue(handle, schedule);
}

struct Image {
	std::vector<uint32_t> image;
	int width = 0;
	int height = 0;
};
Image g_images[4 * 2];

void SetImage(const std::vector<uint32_t>& image, int width, int height, int index) {
	g_images[index].image = image;
	g_images[index].width = width;
	g_images[index].height = height;

	// ウィンドウを再描画
	InvalidateRect(g_hwnd, nullptr, TRUE);
}

void Paint(HWND hwnd) {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);

	for (int i = 0; i < sizeof(g_images) / sizeof(g_images[0]); ++i) {
		if (g_images[i].width > 0 && g_images[i].height > 0) {
			// 画像を描画
			int x = i % 4 * 200; // 4列に分けて配置
			int y = i / 4 * 200; // 2行に分けて配置
			HBITMAP hBitmap = CreateBitmap(g_images[i].width, g_images[i].height, 1, 32, g_images[i].image.data());
			HDC hMemDC = CreateCompatibleDC(hdc);
			SelectObject(hMemDC, hBitmap);
			StretchBlt(hdc, x, y, 200, 200,
				hMemDC, 0, 0, g_images[i].width, g_images[i].height,
				SRCCOPY);
			DeleteObject(hBitmap);
			DeleteDC(hMemDC);
		}
	}

	EndPaint(hwnd, &ps);
}

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE:
		// Workqueueを初期化
		g_mainWQ = new WinWorkqueue();
		break;

	case WM_TIMER:
		printf("WM_TIMER\n");
		g_mainWQ->execute(hwnd);
		break;

	case WM_USER:
		printf("WM_USER\n");
		g_mainWQ->execute(hwnd);
		break;

	case WM_PAINT:
		printf("WM_PAINT\n");
		// ウィンドウの再描画
		Paint(hwnd);
		break;

	case WM_DESTROY:
		delete g_mainWQ;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}

// ウィンドウを作成する関数
HWND CreateMainWindow(HINSTANCE hInstance, int width, int height) {
	const TCHAR* CLASS_NAME = TEXT("GIF Viewer");

	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(
		0,                              // 拡張スタイル
		CLASS_NAME,                     // ウィンドウクラス名
		TEXT("GIF Viewer"),             // ウィンドウタイトル
		WS_OVERLAPPEDWINDOW,            // ウィンドウスタイル
		CW_USEDEFAULT, CW_USEDEFAULT,   // 初期位置
		width, height,                  // 初期サイズ
		nullptr, nullptr, hInstance, nullptr);

	if (!hwnd) {
		return nullptr;
	}

	ShowWindow(hwnd, SW_SHOW);
	return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	if (false) {
		AllocConsole(); // コンソールウィンドウを作成

		// 標準出力・入力をコンソールにバインド
		FILE* fp;
		freopen_s(&fp, "CONOUT$", "w", stdout);  // printf 用
		freopen_s(&fp, "CONOUT$", "w", stderr);  // fprintf(stderr) 用
	}

	HWND hwnd = CreateMainWindow(hInstance, 800, 400);
	if (!hwnd) {
		return -1;
	}
	g_hwnd = hwnd;

	std::thread{ []() {
		unifex::sync_wait(main_task());
	} }.detach();

	// メッセージループ
	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
