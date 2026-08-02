#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

using namespace Microsoft::WRL;

namespace fs = std::filesystem;

namespace {

ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
EventRegistrationToken g_acceleratorKeyToken{};

bool g_isFullScreen = false;
bool g_isCapturingScreenshot = false;
WINDOWPLACEMENT g_windowPlacement{sizeof(WINDOWPLACEMENT)};
DWORD g_windowedStyle = 0;
DWORD g_windowedExStyle = 0;
std::vector<RECT> g_windowedRegions;
ULONG_PTR g_gdiplusToken = 0;

int GetPngEncoderClsid(CLSID* clsid) {
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) {
        return -1;
    }

    auto codecs = std::make_unique<BYTE[]>(size);
    auto* imageCodecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(codecs.get());
    if (Gdiplus::GetImageEncoders(num, size, imageCodecs) != Gdiplus::Ok) {
        return -1;
    }

    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(imageCodecs[i].MimeType, L"image/png") == 0) {
            *clsid = imageCodecs[i].Clsid;
            return static_cast<int>(i);
        }
    }

    return -1;
}

std::wstring BuildScreenshotPath() {
    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath)) || !desktopPath) {
        return L"";
    }

    fs::path folder = fs::path(desktopPath) / L"image";
    CoTaskMemFree(desktopPath);

    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        return L"";
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);

    std::wstringstream fileName;
    fileName << L"screenshot_" << std::put_time(&localTime, L"%Y%m%d_%H%M%S") << L"_" << (nowMs % 1000) << L".png";
    return (folder / fileName.str()).wstring();
}

bool CaptureWindowToPng(HWND hwnd) {
    if (!g_webview || g_isCapturingScreenshot) {
        return false;
    }

    std::wstring filePath = BuildScreenshotPath();
    if (filePath.empty()) {
        return false;
    }

    ComPtr<IStream> outputStream;
    HRESULT hr = SHCreateStreamOnFileEx(
        filePath.c_str(),
        STGM_CREATE | STGM_WRITE | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL,
        TRUE,
        nullptr,
        &outputStream);
    if (FAILED(hr) || !outputStream) {
        return false;
    }

    LARGE_INTEGER zero{};
    outputStream->Seek(zero, STREAM_SEEK_SET, nullptr);

    g_isCapturingScreenshot = true;
    hr = g_webview->CapturePreview(
        COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
        outputStream.Get(),
        Callback<ICoreWebView2CapturePreviewCompletedHandler>(
            [hwnd, outputStream](HRESULT result) mutable -> HRESULT {
                if (SUCCEEDED(result)) {
                    outputStream->Commit(STGC_DEFAULT);
                } else {
                    MessageBoxW(hwnd, L"Chụp màn hình thất bại.", L"Screenshot", MB_OK | MB_ICONWARNING);
                }

                g_isCapturingScreenshot = false;
                return S_OK;
            })
            .Get());

    if (FAILED(hr)) {
        g_isCapturingScreenshot = false;
        return false;
    }

    return true;
}

void ResizeWebView(HWND hwnd) {
    if (!g_controller) {
        return;
    }

    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    g_controller->put_Bounds(bounds);
}

void ToggleFullScreen(HWND hwnd) {
    if (!g_isFullScreen) {
        g_windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        g_windowedExStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        GetWindowPlacement(hwnd, &g_windowPlacement);

        g_windowedRegions.clear();
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        if (region && GetWindowRgn(hwnd, region) != ERROR) {
            const DWORD bytes = GetRegionData(region, 0, nullptr);
            if (bytes > 0) {
                auto data = std::make_unique<BYTE[]>(bytes);
                auto* regionData = reinterpret_cast<RGNDATA*>(data.get());
                if (GetRegionData(region, bytes, regionData) != 0) {
                    const auto* rects = reinterpret_cast<RECT*>(regionData->Buffer);
                    for (DWORD i = 0; i < regionData->rdh.nCount; ++i) {
                        g_windowedRegions.push_back(rects[i]);
                    }
                }
            }
        }
        if (region) {
            DeleteObject(region);
        }

        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);

        SetWindowRgn(hwnd, nullptr, FALSE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, g_windowedExStyle & ~WS_EX_TOOLWINDOW);
        SetWindowPos(
            hwnd,
            HWND_TOP,
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        ResizeWebView(hwnd);
        g_isFullScreen = true;
        return;
    }

    SetWindowLongPtrW(hwnd, GWL_STYLE, g_windowedStyle);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, g_windowedExStyle);
    SetWindowPlacement(hwnd, &g_windowPlacement);
    SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    if (!g_windowedRegions.empty()) {
        HRGN restoreRegion = CreateRectRgn(0, 0, 0, 0);
        if (restoreRegion) {
            for (const RECT& rect : g_windowedRegions) {
                HRGN part = CreateRectRgn(rect.left, rect.top, rect.right, rect.bottom);
                if (part) {
                    CombineRgn(restoreRegion, restoreRegion, part, RGN_OR);
                    DeleteObject(part);
                }
            }
            SetWindowRgn(hwnd, restoreRegion, TRUE);
        }
        g_windowedRegions.clear();
    }

    ShowWindow(hwnd, SW_RESTORE);
    ResizeWebView(hwnd);
    g_isFullScreen = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            ResizeWebView(hwnd);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wParam == VK_F11) {
                ToggleFullScreen(hwnd);
                return 0;
            }
            if (wParam == VK_F9) {
                const bool isStarted = CaptureWindowToPng(hwnd);
                if (!isStarted && !g_isCapturingScreenshot) {
                    MessageBoxW(hwnd, L"Chụp màn hình thất bại.", L"Screenshot", MB_OK | MB_ICONWARNING);
                }
                return 0;
            }
            break;
        case WM_DESTROY:
            if (g_controller) {
                g_controller->remove_AcceleratorKeyPressed(g_acceleratorKeyToken);
            }
            g_webview.Reset();
            g_controller.Reset();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HiddenWebView2Browser";

    if (!RegisterClassW(&wc)) {
        CoUninitialize();
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Hello",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1200,
        800,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd) {
        CoUninitialize();
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        nullptr,
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    return E_FAIL;
                }

                return env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                return E_FAIL;
                            }

                            g_controller = controller;
                            HRESULT coreResult = g_controller->get_CoreWebView2(&g_webview);
                            if (FAILED(coreResult) || !g_webview) {
                                return E_FAIL;
                            }

                            g_controller->add_AcceleratorKeyPressed(
                                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [hwnd](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                        if (!args) {
                                            return S_OK;
                                        }

                                        UINT key = 0;
                                        if (FAILED(args->get_VirtualKey(&key))) {
                                            return S_OK;
                                        }

                                        COREWEBVIEW2_KEY_EVENT_KIND kind{};
                                        if (FAILED(args->get_KeyEventKind(&kind))) {
                                            return S_OK;
                                        }

                                        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
                                            kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                            if (key == VK_F11) {
                                                args->put_Handled(TRUE);
                                                ToggleFullScreen(hwnd);
                                            } else if (key == VK_F9) {
                                                args->put_Handled(TRUE);
                                                const bool isStarted = CaptureWindowToPng(hwnd);
                                                if (!isStarted && !g_isCapturingScreenshot) {
                                                    MessageBoxW(hwnd, L"Chụp màn hình thất bại.", L"Screenshot", MB_OK | MB_ICONWARNING);
                                                }
                                            }
                                        }

                                        return S_OK;
                                    })
                                    .Get(),
                                &g_acceleratorKeyToken);

                            ResizeWebView(hwnd);
                            g_webview->Navigate(DEFAULT_URL_WIDE);
                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        CoUninitialize();
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return 0;
}
