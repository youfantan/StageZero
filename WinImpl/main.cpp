#define _CRT_SECURE_NO_WARNINGS



extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <wrl.h>
#include <comdef.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <sstream>

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib,"DXGI.lib")
#pragma comment(lib,"dwrite.lib")
using Microsoft::WRL::ComPtr;

struct DecoderParam
{
	AVFormatContext* fmtCtx;
	AVCodecContext* vcodecCtx;
	const AVCodec* vcodec;
	int width;
	int height;
	int videoStreamIndex;
};
LRESULT CALLBACK MainWindProc(HWND, UINT, WPARAM, LPARAM);
AVFrame* RequestFrame();
void InitFFDecoder(const char* path);
void InitD2D(HWND hwnd, int width, int height);
void Draw(AVFrame* frame, std::vector<uint8_t>& buffer);
DWORD WINAPI FPSCounter(LPVOID lpParameter);
int GetGpus(std::vector<std::wstring>& gpus);
std::string GetCpuInfo();

HWND _workerw = nullptr; 
DecoderParam decParam = {};
ID2D1Factory* pD2DFactory = nullptr;
ID2D1HwndRenderTarget* pRenderTarget = nullptr;
IDWriteFactory* pDWriteFactory = nullptr;
IDWriteTextFormat* pDbgTextFormat = nullptr;
ID2D1SolidColorBrush* pGreenBrush = nullptr;
D2D1_RECT_F pDbgTextRect = {};
int currentFrames = 0;
int totalFrames = 0;
int fps = 0;
int renderDelay = 0;
int decodeDelay = 0;
std::vector<std::wstring> GPUs;
std::string CPUInfo;
std::wstring hwdevice;

int g_screen_width = GetSystemMetrics(SM_CXSCREEN);
int g_screen_height = GetSystemMetrics(SM_CYSCREEN);
inline BOOL CALLBACK EnumWindowsProc(_In_ HWND tophandle, _In_ LPARAM topparamhandle)
{
	HWND defview = FindWindowEx(tophandle, 0, L"SHELLDLL_DefView", nullptr);
	if (defview != nullptr)
	{
		_workerw = FindWindowEx(0, tophandle, L"WorkerW", 0);
	}
	return true;
}

HWND GetWorkerW() {
	int* result = nullptr;
	HWND windowHandle = FindWindow(L"Progman", nullptr);
	SendMessageTimeout(windowHandle, 0x052c, 0, 0, SMTO_NORMAL, 2, (PDWORD_PTR)result);
	EnumWindows(EnumWindowsProc,0);
	ShowWindow(_workerw, SW_HIDE);
	return windowHandle;
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPreInstance, LPSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
#endif
	SetProcessDPIAware();
	WNDCLASSEX wndClass;
	wndClass.cbSize = sizeof(wndClass);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = MainWindProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = hInstance;
	wndClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	wndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
	wndClass.lpszMenuName = nullptr;
	wndClass.lpszClassName = TEXT("StageZero");
	wndClass.hIconSm = nullptr;
	RegisterClassEx(&wndClass);
	HWND hwnd = CreateWindowEx(
		0,
		TEXT("StageZero"),
		TEXT("Stage 0"),
		WS_POPUP,
		0,
		0,
		g_screen_width,
		g_screen_height,
		nullptr,
		nullptr,
		hInstance,
		nullptr);
	SetParent(hwnd, GetWorkerW());
	InitFFDecoder("video0.mp4");
	InitD2D(hwnd, g_screen_width, g_screen_height);
	GetGpus(GPUs);
	CPUInfo = GetCpuInfo();
	if (hwnd == nullptr)
	{
		std::cout << "Create Window Error!" << std::endl;
		return -1;
	}
	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);
	std::vector<uint8_t> buffer(decParam.width * decParam.height * 4);
	auto wait = std::chrono::system_clock::now();
	double framerate = (double)decParam.vcodecCtx->framerate.den / decParam.vcodecCtx->framerate.num;
	HANDLE hThread = CreateThread(nullptr, 0, FPSCounter, nullptr, 0, nullptr);
	MSG msg;
	while (1) {
		bool hasMsg = PeekMessage(&msg, hwnd, 0, 0,PM_REMOVE);
		if (hasMsg)
		{
			if (msg.message == WM_QUIT) break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}else {
			auto t0 = std::chrono::system_clock::now();
			AVFrame* frame = RequestFrame();
			auto t1 = std::chrono::system_clock::now();
			if (frame != nullptr) {
				Draw(frame, buffer);
				auto t2 = std::chrono::system_clock::now();
				decodeDelay = (t1.time_since_epoch().count() - t0.time_since_epoch().count())/1000;
				renderDelay = (t2.time_since_epoch().count() - t1.time_since_epoch().count())/1000;
				av_frame_free(&frame);
			}
			wait += std::chrono::milliseconds((int)(framerate * 1000));
			std::this_thread::sleep_until(wait);	
			wait = std::chrono::system_clock::now();
		}
	}
	return 0;
}
int GetGpus(std::vector<std::wstring>& gpus) {
	IDXGIFactory* factory;
	IDXGIAdapter* adapter;
	int nAdapters = 0;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory), (void**)&factory))) {
		return -1;
	}

	while (factory->EnumAdapters(nAdapters, &adapter) != DXGI_ERROR_NOT_FOUND) {
		nAdapters++;
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);
		std::wstring wDescription(desc.Description);
		if (wDescription != L"Microsoft Basic Render Driver") {
			gpus.push_back(wDescription);
		}
	}
	return 0;
}
DWORD WINAPI FPSCounter(LPVOID lpParameter) {
	while (1) {
		int i0 = currentFrames;
		Sleep(1000);
		int i1 = currentFrames;
		currentFrames = 0;
		int frames = i1 - i0;
		fps = frames;
	}
	return 0;
}
void InitD2D(HWND hwnd,int width,int height) {
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&pD2DFactory);
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&pDWriteFactory));
	pDWriteFactory->CreateTextFormat(L"Lucida Console",nullptr,DWRITE_FONT_WEIGHT_REGULAR,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14.0f,L"en-us",&pDbgTextFormat);
	RECT rc;
	GetClientRect(hwnd, &rc);
	pDbgTextRect = D2D1::RectF(static_cast<FLOAT>(rc.right - 400), static_cast<FLOAT>(rc.top), static_cast<FLOAT>(rc.right - rc.left), static_cast<FLOAT>(rc.bottom - rc.top));
	pD2DFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(width,height)),&pRenderTarget);
	pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f)),&pGreenBrush);
}
std::string GetCpuInfo() {
	int cpuInfo[4] = { -1 };
	char cpu_type[32] = { 0 };
	char cpu_name[32] = { 0 };
	char cpu_freq[32] = { 0 };
	std::string cpu_full_name;
	__cpuid(cpuInfo, 0x80000003);
	memcpy(cpu_type, cpuInfo, sizeof(cpuInfo));
	__cpuid(cpuInfo, 0x80000002);
	memcpy(cpu_name, cpuInfo, sizeof(cpuInfo));
	__cpuid(cpuInfo, 0x80000004);
	memcpy(cpu_freq, cpuInfo, sizeof(cpuInfo));
	cpu_full_name += cpu_name;
	cpu_full_name += cpu_type;
	cpu_full_name += cpu_freq;
	return cpu_full_name;
}
std::wstring GetDbgTxt() {
	std::wostringstream stream;
	stream << "FPS :" << fps << std::endl;
	stream << "Total Frames :" << totalFrames << std::endl;
	stream << "Render Delay :" << renderDelay << "ms" << std::endl;
	stream << "Decode Delay :" << decodeDelay << "ms" << std::endl;
	stream << "Graphics Card(s): " << std::endl;
	for (size_t i = 0; i < GPUs.size(); i++)
	{
		stream << "\t" << i << ". " << GPUs[i] << std::endl;
	}
	stream << "CPU: " << CPUInfo.c_str() << std::endl;
	MEMORYSTATUS ms;
	GlobalMemoryStatus(&ms);
	stream << "Memory Used: " << (int)((ms.dwTotalPhys - ms.dwAvailPhys) / 1048576) << "/" << (int)(ms.dwTotalPhys / 1048576) << " MB" << std::endl;
	stream << L"Hardware Acceleration : " << hwdevice;
	return stream.str();
}
void Draw(AVFrame* frame, std::vector<uint8_t>& buffer) {
	AVFrame* swFrame = av_frame_alloc();
	av_hwframe_transfer_data(swFrame, frame, 0);
	frame = swFrame;
	static SwsContext* swsctx = nullptr;
	swsctx = sws_getCachedContext(
		swsctx,
		frame->width, frame->height, (AVPixelFormat)frame->format,
		g_screen_width, g_screen_height, AVPixelFormat::AV_PIX_FMT_BGRA, 0, nullptr, nullptr, nullptr);

	uint8_t* data[] = { &buffer[0] };
	int linesize[] = { g_screen_width * 4 };
	sws_scale(swsctx, frame->data, frame->linesize, 0, frame->height, data, linesize);
	av_frame_free(&swFrame);	D2D1_SIZE_U size;
	size.width = g_screen_width;
	size.height = g_screen_height;
	D2D1_BITMAP_PROPERTIES property = {};
	property.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
	ID2D1Bitmap* pBitmap = nullptr;
	uint32_t pitch = 4 * g_screen_width;
	uint8_t* dstData = &buffer[0];
	HRESULT hr = pRenderTarget->CreateBitmap(size, dstData, pitch, property, &pBitmap);
	if (SUCCEEDED(hr))
	{
		pRenderTarget->BeginDraw();
		pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));
		pRenderTarget->DrawBitmap(pBitmap, D2D1::RectF(0, 0, g_screen_width, g_screen_height));
		ComPtr<ID2D1Bitmap> bmp;
		pBitmap->Release();
		std::wstring text = GetDbgTxt();
		pRenderTarget->DrawText(text.c_str(),text.length(),pDbgTextFormat,&pDbgTextRect,pGreenBrush);
		pRenderTarget->EndDraw();
		currentFrames++;
		totalFrames++;
	}
}
void InitFFDecoder(const char* path) {
	AVFormatContext* fmtCtx = nullptr;
	avformat_open_input(&fmtCtx, path, nullptr, nullptr);
	avformat_find_stream_info(fmtCtx, nullptr);

	AVCodecContext* vcodecCtx = nullptr;
	for (int i = 0; i < fmtCtx->nb_streams; i++) {
		const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[i]->codecpar->codec_id);
		if (codec->type == AVMEDIA_TYPE_VIDEO) {
			decParam.videoStreamIndex = i;
			decParam.vcodec = codec;
			vcodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(vcodecCtx, fmtCtx->streams[i]->codecpar);
			avcodec_open2(vcodecCtx, codec, nullptr);
		}
	}
	AVBufferRef* hw_device_ctx = nullptr;
	for (int i = 0;; i++) {
		const AVCodecHWConfig* config = avcodec_get_hw_config(decParam.vcodec, i);
		if (!config) {
			hwdevice = L"FFMPEG SOFT DECODING";
			break;
		}
		else if (config->device_type == AV_HWDEVICE_TYPE_CUDA) {
			hwdevice = L"AV_HWDEVICE_TYPE_CUDA";
			av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
			break;
		}
		else if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
			hwdevice = L"AV_HWDEVICE_TYPE_D3D11VA";
			av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
			break;
		}
		else if (config->device_type == AV_HWDEVICE_TYPE_DXVA2) {
			hwdevice = L"AV_HWDEVICE_TYPE_DXVA2";
			av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0);
			break;
		}
		else if (config->device_type == AV_HWDEVICE_TYPE_QSV) {
			hwdevice = L"AV_HWDEVICE_TYPE_QSV";
			av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
			break;
		}
		else if (config->device_type == AV_HWDEVICE_TYPE_VULKAN) {
			hwdevice = L"AV_HWDEVICE_TYPE_VULKAN";
			av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0);
			break;
		}
		else if (config->device_type == AV_HWDEVICE_TYPE_OPENCL) {
			hwdevice = L"AV_HWDEVICE_TYPE_OPENCL";
			av_hwdevice_ctx_create(&hw_device_ctx, AVHWDeviceType::AV_HWDEVICE_TYPE_OPENCL, nullptr, nullptr, 0);
			break;
		}
	}
	vcodecCtx->hw_device_ctx = hw_device_ctx;
	decParam.fmtCtx = fmtCtx;
	decParam.vcodecCtx = vcodecCtx;
	decParam.width = vcodecCtx->width;
	decParam.height = vcodecCtx->height;
}

AVFrame* RequestFrame() {
	auto& fmtCtx = decParam.fmtCtx;
	auto& vcodecCtx = decParam.vcodecCtx;
	auto& videoStreamIndex = decParam.videoStreamIndex;

	while (1) {
		AVPacket* packet = av_packet_alloc();
		int ret = av_read_frame(fmtCtx, packet);
		if (ret == 0 && packet->stream_index == videoStreamIndex) {
			ret = avcodec_send_packet(vcodecCtx, packet);
			if (ret == 0) {
				AVFrame* frame = av_frame_alloc();
				ret = avcodec_receive_frame(vcodecCtx, frame);
				if (ret == 0) {
					av_packet_unref(packet);
					return frame;
				}
				else if (ret == AVERROR(EAGAIN)) {
					av_frame_unref(frame);
				}
			}
		}
		else if (ret== AVERROR_EOF)
		{
			avio_seek(fmtCtx->pb, 0, SEEK_SET);
			avformat_seek_file(fmtCtx, videoStreamIndex, 0, 0, fmtCtx[videoStreamIndex].duration, 0);
			break;
		}
		else {
			break;
		}
		av_packet_unref(packet);
	}
	return nullptr;
}
LRESULT CALLBACK MainWindProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}

