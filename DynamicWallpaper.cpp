// =============================================================================
// 动态 GIF 壁纸程序 - 使用 Direct2D 高性能渲染
// 功能：将指定路径的 GIF 动画作为 Windows 桌面背景播放，流畅且无噪点
// 作者：岙靔
// 编译命令（在 MSYS2 MinGW64 终端中运行）：
// g++ -O2 -D_WIN32_WINNT=0x0601 -mwindows -municode DynamicWallpaper.cpp -o DynamicWallpaper.exe -ld2d1 -lwindowscodecs -lole32 -luuid -ldwmapi
// =============================================================================

#include <windows.h>        // Windows API 核心头文件
#include <dwmapi.h>         // DWM（桌面窗口管理器）API，用于高级窗口效果
#include <d2d1.h>           // Direct2D 图形渲染 API（硬件加速）
#include <wincodec.h>       // WIC（Windows Imaging Component），用于加载图像（如 GIF）
#include <dxgi.h>           // DXGI 格式定义（如 DXGI_FORMAT_B8G8R8A8_UNORM）
#include <propvarutil.h>    // 用于读取图像元数据（如 GIF 帧延迟）
#include <vector>           // C++ 标准容器，存储 GIF 帧

// 🔴【重要】修改这里：设置你的 GIF 文件路径（必须是 .gif 格式）
// 注意：使用双反斜杠 \\ 或正斜杠 /，例如：
// const wchar_t WALLPAPER_PATH[] = L"C:/wallpapers/animated.gif";
const wchar_t WALLPAPER_PATH[] = L"C:\\wallpapers\\animated.gif";

// ========================
// 全局变量声明
// ========================
HWND g_hEmbedWnd = NULL;                    // 嵌入到桌面的窗口句柄
ID2D1Factory* g_pD2DFactory = nullptr;      // Direct2D 工厂对象（用于创建渲染资源）
ID2D1HwndRenderTarget* g_pRenderTarget = nullptr; // Direct2D 渲染目标（绑定到窗口）
IWICImagingFactory* g_pWICFactory = nullptr;    // WIC 工厂（用于解码图像）
std::vector<IWICBitmapFrameDecode*> g_pFrames;  // 存储 GIF 的每一帧
std::vector<UINT> g_frameDelays;                // 存储每帧的显示时间（毫秒）
UINT g_nCurrentFrame = 0;                       // 当前播放到第几帧
UINT_PTR g_TimerId = 0;                         // 定时器 ID，用于控制动画节奏
bool g_bReady = false;                          // 标记图形系统是否初始化完成

// ========================
// 初始化图形系统（Direct2D + WIC）
// ========================
bool InitGraphics() {
    // 创建 Direct2D 工厂（单线程模式）
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        (void**)&g_pD2DFactory
    );
    if (FAILED(hr)) return false;

    // 创建 WIC 成像工厂（用于加载和解码图像）
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,    // WIC 工厂的 CLSID
        nullptr,
        CLSCTX_INPROC_SERVER,       // 在当前进程中创建
        IID_PPV_ARGS(&g_pWICFactory) // 自动填充接口指针和 IID
    );
    return SUCCEEDED(hr); // 返回是否成功
}

// ========================
// 加载 GIF 文件，解析所有帧和延迟时间
// ========================
bool LoadGif() {
    IWICBitmapDecoder* pDecoder = nullptr;
    // 从文件创建 GIF 解码器
    HRESULT hr = g_pWICFactory->CreateDecoderFromFilename(
        WALLPAPER_PATH,             // 文件路径
        nullptr,                    // 不指定解码器 GUID（自动选择）
        GENERIC_READ,               // 只读访问
        WICDecodeMetadataCacheOnLoad, // 加载时缓存元数据（提高性能）
        &pDecoder                   // 输出解码器指针
    );
    if (FAILED(hr)) return false;

    // 获取总帧数
    UINT frameCount = 0;
    pDecoder->GetFrameCount(&frameCount);
    if (frameCount == 0) {
        pDecoder->Release();
        return false;
    }

    // 预分配内存
    g_pFrames.resize(frameCount);
    g_frameDelays.clear();

    // 遍历 GIF 的每一帧（从第 0 帧到 frameCount - 1）
    for (UINT i = 0; i < frameCount; ++i) {
        // 声明一个指向当前帧解码对象的指针，初始为 nullptr
        IWICBitmapFrameDecode* pFrame = nullptr;

        // 从 WIC 解码器中获取第 i 帧的解码接口
        // 如果失败（如文件损坏、格式不支持等），跳过该帧
        hr = pDecoder->GetFrame(i, &pFrame);
        if (FAILED(hr)) continue;

        // 初始化帧延迟为默认值 100 毫秒（GIF 规范规定：若未指定延迟，则使用 100ms）
        UINT delay = 100; // 默认延迟（毫秒）

        // 尝试为当前帧创建元数据查询读取器（用于读取 GIF 帧的附加信息，如延迟、透明色等）
        IWICMetadataQueryReader* pQueryReader = nullptr;
        if (SUCCEEDED(pFrame->GetMetadataQueryReader(&pQueryReader))) {
            // 定义一个 PROPVARIANT 变量，用于接收元数据的值
            // PROPVARIANT 是 Windows 中通用的变体类型，可存储多种数据（整数、字符串、数组等）
            PROPVARIANT prop;
            PropVariantInit(&prop); // 初始化 prop，避免未定义行为

            if (SUCCEEDED(pQueryReader->GetMetadataByName(L"/grctlext/Delay", &prop)) && prop.vt == VT_UI2) {
                // 确保返回的是 16 位无符号整数（VT_UI2 = unsigned short）
                UINT gifDelay = prop.uiVal; // 获取原始延迟值（单位：1/100 秒）

                // 📜 GIF89a 规范说明：
                //   - 如果 delay 字段为 0，表示“最小可能延迟”，但实际应解释为 100ms（即 1/10 秒）
                //   - 这是为了兼容早期浏览器（如 Netscape）的行为
                if (gifDelay == 0) {
                    delay = 100; // 按规范，delay=0 → 100 毫秒
                } else {
                    delay = gifDelay * 10; // 转换为毫秒（例如：3 → 30ms）
                }
            }

            // 清理 PROPVARIANT 占用的资源（即使未使用也必须调用）
            PropVariantClear(&prop);
            // 释放元数据查询读取器接口
            pQueryReader->Release();
        }

        // 将当前帧的解码对象存入全局帧列表（用于后续渲染）
        g_pFrames[i] = pFrame;
        // 将计算出的延迟（毫秒）存入延迟列表，与帧一一对应
        g_frameDelays.push_back(delay);
    }

    pDecoder->Release();
    return true;
}

// ========================
// 将 WIC 图像帧转换为 Direct2D 位图（关键格式转换）
// ========================
ID2D1Bitmap* ConvertToD2DBitmap(IWICBitmapSource* pSource) {
    if (!g_pRenderTarget) return nullptr;

    // 获取图像尺寸（可选）
    UINT width = 0, height = 0;
    pSource->GetSize(&width, &height);

    // 创建格式转换器
    IWICFormatConverter* pConverter = nullptr;
    g_pWICFactory->CreateFormatConverter(&pConverter);

    // 转换为 Direct2D 支持的像素格式：32bppPBGRA（预乘 Alpha）
    // 这是 Direct2D 推荐的格式，确保最佳兼容性和性能
    pConverter->Initialize(
        pSource,
        GUID_WICPixelFormat32bppPBGRA, // 目标格式
        WICBitmapDitherTypeNone,       // 不抖动
        nullptr,                       // 无调色板
        0.0f,                          // 无 alpha 阈值
        WICBitmapPaletteTypeMedianCut  // 调色板类型（此处不使用）
    );

    // 从 WIC 位图创建 Direct2D 位图
    ID2D1Bitmap* pBitmap = nullptr;
    g_pRenderTarget->CreateBitmapFromWicBitmap(pConverter, nullptr, &pBitmap);
    pConverter->Release(); // 释放转换器
    return pBitmap;
}

// ========================
// 渲染当前 GIF 帧到屏幕
// ========================
void RenderFrame() {
    // 安全检查
    if (!g_bReady || !g_pRenderTarget || g_pFrames.empty()) return;

    g_pRenderTarget->BeginDraw(); // 开始绘制

    // ✅ 关键修复：清屏为【不透明黑色】（Alpha=1.0）
    // 原因：避免帧间半透明混合导致“噪点”或“鬼影”
    // 即使 GIF 有透明通道，壁纸也应覆盖整个屏幕（不透明）
    g_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

    // 转换当前帧为 Direct2D 位图
    ID2D1Bitmap* pBitmap = ConvertToD2DBitmap(g_pFrames[g_nCurrentFrame]);
    if (pBitmap) {
        D2D1_SIZE_F rtSize = g_pRenderTarget->GetSize(); // 获取窗口大小
        // 拉伸绘制到全屏
        g_pRenderTarget->DrawBitmap(
            pBitmap,
            D2D1_RECT_F{0, 0, rtSize.width, rtSize.height}, // 目标矩形
            1.0f,                                           // 不透明度
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR          // 线性插值（平滑缩放）
        );
        pBitmap->Release(); // 释放位图
    }

    g_pRenderTarget->EndDraw(); // 结束绘制并提交到 GPU
}

// ========================
// 嵌入窗口的消息处理函数
// ========================
LRESULT CALLBACK EmbedProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            // ✅ 阻止 Windows 默认擦除背景，减少闪烁
            return TRUE;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            RenderFrame(); // 重绘窗口内容
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_TIMER:
            // 定时器触发：切换到下一帧
            if (wParam == 1 && !g_pFrames.empty()) {
                g_nCurrentFrame = (g_nCurrentFrame + 1) % static_cast<UINT>(g_pFrames.size());
                UINT delay = g_frameDelays[g_nCurrentFrame];
                // 重新设置定时器（实现可变帧率）
                KillTimer(hwnd, g_TimerId);
                g_TimerId = SetTimer(hwnd, 1, delay, nullptr);
                // 请求重绘
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case WM_DESTROY:
            // 窗口销毁时清理资源
            if (g_TimerId) KillTimer(hwnd, g_TimerId);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ========================
// 辅助函数：触发 Windows 桌面刷新，确保 WorkerW 窗口存在
// ========================
BOOL RaiseDesktop(HWND hProgman) {
    // 发送神秘消息 0x52C（微软内部消息），强制 Progman 创建 WorkerW
    SendMessageTimeoutW(hProgman, 0x52C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    Sleep(300); // 等待窗口创建
    return TRUE;
}

// ========================
// 程序入口点（Unicode 版本）
// ========================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // 告诉 Windows 本程序支持高 DPI（避免模糊）
    SetProcessDPIAware();
    // 初始化 COM 库（WIC 和 Direct2D 依赖 COM）
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 初始化图形系统
    if (!InitGraphics()) {
        MessageBoxW(nullptr, L"初始化 Direct2D/WIC 失败！", L"错误", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 加载 GIF
    if (!LoadGif()) {
        MessageBoxW(nullptr, L"无法加载 GIF 文件！", L"错误", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 注册窗口类
    WNDCLASSW wc = {};
    wc.lpfnWndProc = EmbedProc;         // 消息处理函数
    wc.hInstance = hInst;
    wc.lpszClassName = L"EmbedWindow";  // 窗口类名
    RegisterClassW(&wc);

    // 创建一个 1x1 的分层窗口（初始隐藏）
    g_hEmbedWnd = CreateWindowExW(
        WS_EX_LAYERED,                  // 分层窗口（支持透明）
        L"EmbedWindow",
        L"Embed",
        WS_POPUP,                       // 无边框
        0, 0, 1, 1,                     // 初始大小 1x1
        nullptr, nullptr, hInst, nullptr
    );

    if (!g_hEmbedWnd) {
        MessageBoxW(nullptr, L"创建窗口失败", L"Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 查找 Progman 窗口（Windows 桌面管理器）
    HWND hProgman = FindWindowW(L"Progman", nullptr);
    if (!hProgman) {
        DestroyWindow(g_hEmbedWnd);
        CoUninitialize();
        return 1;
    }

    // 触发 WorkerW 创建
    RaiseDesktop(hProgman);

    // 查找 WorkerW 窗口（实际桌面壁纸容器）
    HWND hWorkerW = FindWindowExW(hProgman, nullptr, L"WorkerW", nullptr);
    if (!hWorkerW) {
        // 如果没找到，遍历所有顶级窗口查找 WorkerW
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t cls[64];
            if (GetClassNameW(hwnd, cls, 64) && !wcscmp(cls, L"WorkerW")) {
                *(HWND*)lParam = hwnd;
                return FALSE; // 找到，停止枚举
            }
            return TRUE; // 继续
        }, (LPARAM)&hWorkerW);
    }

    // 设置窗口为分层，并完全不透明（255 = 100% 不透明）
    SetWindowLongW(g_hEmbedWnd, GWL_EXSTYLE, GetWindowLongW(g_hEmbedWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g_hEmbedWnd, 0, 255, LWA_ALPHA);
    // 将窗口嵌入到 WorkerW（或 Progman）之下
    HWND hParent = hWorkerW ? hWorkerW : hProgman;
    SetParent(g_hEmbedWnd, hParent);

    // 调整窗口为全屏
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    MoveWindow(g_hEmbedWnd, 0, 0, w, h, TRUE);

    // 创建 Direct2D 渲染目标（绑定到窗口）
    D2D1_RENDER_TARGET_PROPERTIES rtProps = { };
    rtProps.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rtProps.pixelFormat.format = DXGI_FORMAT_UNKNOWN;     // 自动匹配
    rtProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
    rtProps.dpiX = rtProps.dpiY = 0.0f;
    rtProps.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rtProps.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = { };
    hwndProps.hwnd = g_hEmbedWnd;
    hwndProps.pixelSize.width = w;
    hwndProps.pixelSize.height = h;
    hwndProps.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY; // 立即呈现

    HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(&rtProps, &hwndProps, &g_pRenderTarget);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"创建 Direct2D 渲染目标失败！", L"错误", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 启动成功！
    g_bReady = true;
    ShowWindow(g_hEmbedWnd, SW_SHOW);
    UpdateWindow(g_hEmbedWnd);

    // 启动动画定时器
    UINT firstDelay = g_frameDelays.empty() ? 100 : g_frameDelays[0];
    g_TimerId = SetTimer(g_hEmbedWnd, 1, firstDelay, nullptr);

    // 消息循环
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理资源
    for (auto* pFrame : g_pFrames) {
        if (pFrame) pFrame->Release();
    }
    if (g_pRenderTarget) g_pRenderTarget->Release();
    if (g_pD2DFactory) g_pD2DFactory->Release();
    if (g_pWICFactory) g_pWICFactory->Release();
    CoUninitialize();
    return 0;
}