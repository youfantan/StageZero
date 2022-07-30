# 使用FFMPEG与Direct2D实现动态壁纸

## 原理
在Visual Studio 2019的`工具->Spy++`中可以看到`ProgMan`进程下的`SysListView`即为渲染桌面图标的进程，那么只需要把这个进程从`ProgMan`中分离，然后把自己的HWND设置成`ProgMan`的子进程即可。Windows提供了一个未公开的窗体消息`0x052c`用来把`ProgMan`分离为两个`WorkerW`进程，按照这个思路，可以编写出如下代码：  
```c++
HWND _workerw = nullptr;

inline BOOL CALLBACK EnumWindowsProc(_In_ HWND tophandle, _In_ LPARAM topparamhandle)
{
    HWND defview = FindWindowEx(tophandle, 0, L"SHELLDLL_DefView", nullptr);
    if (defview != nullptr)
    {
        _workerw = FindWindowEx(0, tophandle, L"WorkerW", 0);
    }
    return true;
}

HWND GetWorkerW(){
    int result;
    HWND windowHandle = FindWindow(L"Progman", nullptr);
    SendMessageTimeout(windowHandle, 0x052c, 0 ,0, SMTO_NORMAL, 0x3e8,(PDWORD_PTR)&result);
    EnumWindows(EnumWindowsProc,(LPARAM)nullptr);
    ShowWindow(_workerw,SW_HIDE);
    return windowHandle;
}
```
使用时只需要`SetParent(hwnd, GetWorkerW());`即可。

## 实现
本文以Visual Studio 2019作为开发环境，使用C++编写。
### 准备FFMPEG环境
在[FFMPEG Release](https://github.com/BtbN/FFmpeg-Builds/releases)中下载最新的AutoBuild(可以省去编译的时间)，我选择了`ffmpeg-master-latest-win64-gpl-shared.zip`。下载后解压到项目根目录，在`项目->VC++目录`中设置包含目录和库目录分别为解压后的`include`与`lib`，新建一个cpp文件，在文件开头输入：
```c++
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
```
即可。

### 准备Direct2D环境
确保已经正确配置并安装Windows SDK。  
在文件开头输入
```c++
//..
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib,"DXGI.lib")
#pragma comment(lib,"dwrite.lib")

//..
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <comdef.h>
```
本文大量涉及到COM对象的使用，你可以选择使用微软提供的WRL。若需要只用，只需添加：
```c++
//..
#include <wrl.h>

using Microsoft::WRL::ComPtr;
```

### 编写窗体
程序的主题是一个传统Win32窗体。本文的实现如下
```c++
LRESULT CALLBACK MainWindProc(HWND, UINT, WPARAM, LPARAM);

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
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPreInstance, LPSTR lpCmdLine, int nCmdShow){
    #ifdef _DEBUG //方便使用标准输出调试
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
#endif
	SetProcessDPIAware(); //适配高DPI
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
	HWND hwnd = CreateWindowEx(0,TEXT("StageZero"),TEXT("Stage 0"),WS_POPUP,0,0,g_screen_width,g_screen_height,nullptr,nullptr,hInstance,nullptr);
    //WS_POPUP可以取消程序边框和标题栏，因为我们使用动态壁纸，自然不需要边框
    SetParent(hwnd, GetWorkerW());
    ShowWindow(hwnd, nCmdShow);//将HWND显示到桌面
	UpdateWindow(hwnd);
    MSG msg;
	while (1) {
		bool hasMsg = PeekMessage(&msg, hwnd, 0, 0,PM_REMOVE);//因为程序需要一直解码并显示，GetMessage为阻塞方法，我们使用不阻塞的PeekMessage
		if (hasMsg)
		{
			if (msg.message == WM_QUIT) break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return 0;
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
```
现在运行程序，你应该可以看见桌面的壁纸被覆盖，取而代之的是一片白色，但图标仍然显示。

### FFMPEG的解码过程
篇幅所限，我只在这部分简要介绍FFMPEG的解码过程。
`AVFormatContext`是一个视频的上下文数据。  
`avformat_open_input`是从文件中读取`AVFormatContext`数据。  
`avformat_find_stream_info`是从文件中读取流数据。  
`AVCodecContext`是解码上下文数据。   
首先，使用`avformat_open_input`与`avformat_find_stream_info`初始化视频上下文。一个视频文件可以有多个流，比如音频流、视频流等等。我们暂时不考虑播放声音，那么只需要获取视频流即可。我们使用一个结构体`DecoderParam`来存储这些信息。
```c++
struct DecoderParam
{
	AVFormatContext* fmtCtx;
	AVCodecContext* vcodecCtx;
	const AVCodec* vcodec;
	int width;
	int height;
	int videoStreamIndex;
};

//..
DecoderParam decParam = {};

//..
void InitFFDecoder(const char* path) {
	AVFormatContext* fmtCtx = nullptr;
	avformat_open_input(&fmtCtx, path, nullptr, nullptr);
	avformat_find_stream_info(fmtCtx, NULL);

	AVCodecContext* vcodecCtx = nullptr;
	for (int i = 0; i < fmtCtx->nb_streams; i++) {
		const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[i]->codecpar->codec_id);
		if (codec->type == AVMEDIA_TYPE_VIDEO) {
			decParam.videoStreamIndex = i;
			decParam.vcodec = codec;
			vcodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(vcodecCtx, fmtCtx->streams[i]->codecpar);
			avcodec_open2(vcodecCtx, codec, NULL);
		}
	}
    decParam.fmtCtx = fmtCtx;
	decParam.vcodecCtx = vcodecCtx;
	decParam.width = vcodecCtx->width;
	decParam.height = vcodecCtx->height;
}
```
大部分主流显卡和CPU都提供了硬件编解码的能力，如Intel的QSV，Nvidia的CUDA等，我们可以通过设置解码上下文的`hw_device_ctx`属性来使用硬件编解码。
```c++
//..
std::wstring hwdevice; //用来记录硬件编解码设备名

//..
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
```
然后我们开始编写`RequestFrame`的函数。这个函数是用来逐帧获取视频内容的，返回值是一个`AVFrame`指针。
```c++
//..
AVFrame* RequestFrame();

//..
AVFrame* RequestFrame(){
    	auto& fmtCtx = param.fmtCtx;
	auto& vcodecCtx = param.vcodecCtx;
	auto& videoStreamIndex = param.videoStreamIndex;

	while (1) {
		AVPacket* packet = av_packet_alloc();
		int ret = av_read_frame(fmtCtx, packet);//由于视频的每一帧都是相对的，因此一个AVPacket可能解不出来一帧，需要读到能解出来为止，所以这个函数输入的是一个AVPacket指针
		if (ret == 0 && packet->stream_index == videoStreamIndex) {
            //判断这一帧是否解码成功且是否为视频流
			ret = avcodec_send_packet(vcodecCtx, packet);//把这些Packet送入解码器解码
			if (ret == 0) {
				AVFrame* frame = av_frame_alloc();
				ret = avcodec_receive_frame(vcodecCtx, frame);//获取一帧
				if (ret == 0) {
					av_packet_unref(packet);
					return frame;
				}
				else if (ret == AVERROR(EAGAIN)) {
					av_frame_unref(frame);
				}
			}
		}
		else if (ret== AVERROR_EOF)//如果视频读取到结尾了会返回AVERROR_EOF，因为我们希望它可以连续播放，那么把文件指针移到初始位置即可。
		{
			avio_seek(fmtCtx->pb, 0, SEEK_SET);
			avformat_seek_file(fmtCtx, videoStreamIndex, 0, 0, fmtCtx[videoStreamIndex].duration, 0);
			break;
		}
		else {
			break;//一定要break，否则会不停的av_packet_alloc()导致内存溢出（血与泪的教训）
		}
		av_packet_unref(packet);
	}
	return nullptr;
}
```
### 使用Direct2D
Direct2D是一个复杂的API，有关它的使用方法可以参考官方文档，我只粗略的介绍它的使用过程。  
任何Direct2D程序都需要有一个`ID2D1Factory`用来创建一些COM接口。`ID2D1HwndRenderTarget`可以看作一个画板。我们将让FFMPEG返回一个BGRA(Blue Green Reed Alpha各八位，最终是一个无符号32位整数)数组，然后DirectX通过这个数组创建一个位图，最终把位图绘制到窗口上。

```c++
//..
void InitD2D(HWND hwnd, int width, int height);//初始化Direct2D
//..
ID2D1Factory* pD2DFactory = nullptr;
ID2D1HwndRenderTarget* pRenderTarget = nullptr;
//..
void InitD2D(HWND hwnd,int width,int height) {
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&pD2DFactory);//创建工厂接口
	pD2DFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(width,height)),&pRenderTarget);//创建渲染目标
}
```
### 绘制
这是本文的重头戏，几乎每一行都做了注释。
```c++
//..
#include <vector>
#include <chrono>
#include <thread>
//..
void Draw(AVFrame* frame, std::vector<uint8_t>& buffer);
DWORD WINAPI FPSCounter(LPVOID lpParameter);//计算帧率线程

//..
int currentFrames = 0;//自上一秒已经渲染的帧数
int totalFrames = 0;//自程序开始已经渲染的帧数
int fps = 0;//当前帧率
int renderDelay = 0;//渲染延迟
int decodeDelay = 0;//解码延迟

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPreInstance, LPSTR lpCmdLine, int nCmdShow)
{
    //..
    InitFFDecoder("video0.mp4");//这是我们需要播放的视频
	InitD2D(hwnd, g_screen_width, g_screen_height);
    //..
    std::vector<uint8_t> buffer(decParam.width * decParam.height * 4);//提前分配每一帧的数据
    auto wait = std::chrono::system_clock::now();//初始化帧率同步的时间
	double framerate = (double)decParam.vcodecCtx->framerate.den / decParam.vcodecCtx->framerate.num;//获取视频帧速率
	HANDLE hThread = CreateThread(nullptr, 0, FPSCounter, nullptr, 0, nullptr);//创建一个线程计算帧率
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
				Draw(frame, buffer);//渲染
				auto t2 = std::chrono::system_clock::now();
				decodeDelay = (t1.time_since_epoch().count() - t0.time_since_epoch().count())/1000;//计算解码延迟
				renderDelay = (t2.time_since_epoch().count() - t1.time_since_epoch().count())/1000;//计算渲染延迟
				av_frame_free(&frame);//释放帧
			}
			wait += std::chrono::milliseconds((int)(framerate * 1000));//帧率同步
			std::this_thread::sleep_until(wait);	
			wait = std::chrono::system_clock::now();
		}
	}
}
//..
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

void Draw(AVFrame* frame, std::vector<uint8_t>& buffer) {
	AVFrame* swFrame = av_frame_alloc();
	av_hwframe_transfer_data(swFrame, frame, 0);//调用硬件编解码实际上是把流读入显存，再从显存读回来，因此我们需要先把解码后的数据从显存读到内存
	frame = swFrame;
	static SwsContext* swsctx = nullptr;//我们用这个上下文读取数据并进行缩放或扩放
	swsctx = sws_getCachedContext(
		swsctx,
		frame->width, frame->height, (AVPixelFormat)frame->format,
		g_screen_width, g_screen_height, AVPixelFormat::AV_PIX_FMT_BGRA, 0, nullptr, nullptr, nullptr);
	uint8_t* data[] = { &buffer[0] };//这是存放BGRA数据的指针
	int linesize[] = { g_screen_width * 4 };//LineSize，即每行扫描的像素数量，由于我们使用了红绿蓝和Alpha四个通道，实际扫描像素应该是屏幕宽乘以4
	sws_scale(swsctx, frame->data, frame->linesize, 0, frame->height, data, linesize);//缩放
	av_frame_free(&swFrame);//释放临时帧
    D2D1_SIZE_U size;//BitMap的大小
	size.width = g_screen_width;
	size.height = g_screen_height;
	D2D1_BITMAP_PROPERTIES property = {};//BitMap的属性
	property.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);//我们使用无符号BGRA格式，并且处理Alpha通道
	ID2D1Bitmap* pBitmap = nullptr;//创建目标BitMap
	uint32_t pitch = 4 * g_screen_width;//同LineSize
	uint8_t* dstData = &buffer[0];//取数据指针
	HRESULT hr = pRenderTarget->CreateBitmap(size, dstData, pitch, property, &pBitmap);//创建BitMap
	if (SUCCEEDED(hr))
	{
		pRenderTarget->BeginDraw();//开始渲染
		pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));//把画布以白色初始化
		pRenderTarget->DrawBitmap(pBitmap, D2D1::RectF(0, 0, g_screen_width, g_screen_height));//绘制BitMap
		pBitmap->Release();//释放位图，否则会吃满显存
		pRenderTarget->EndDraw();
		currentFrames++;
		totalFrames++;
	}
}
```
现在开始调试程序，你应该可以看见桌面的壁纸被替换成视频并开始以固定的帧率播放。  
完整代码已经上传到[StageZero仓库](https://github.com/youfantan/StageZero)。

## 参考
+ [【C++】从零开始，只使用FFmpeg，Win32 API，实现一个播放器（一） ](https://www.cnblogs.com/judgeou/p/14724951.html) (强烈推荐，讲的非常详细。其中第二篇讲了使用DirectX11 3D实现，性能会大幅提升)
+ [如何实现一个 windows 桌面动态壁纸
](https://zhuanlan.zhihu.com/p/37877070)
+ [Repeating ffmpeg stream (libavcodec/libavformat)](https://stackoverflow.com/questions/45526098/repeating-ffmpeg-stream-libavcodec-libavformat)
+ [What is the srcData for ID2D1Bitmap::CreateBitmap](https://stackoverflow.com/questions/57538664/what-is-the-srcdata-for-id2d1bitmapcreatebitmap)
+ [Direct2D 快速入门](https://docs.microsoft.com/zh-cn/windows/win32/direct2d/getting-started-with-direct2d)