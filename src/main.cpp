#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

namespace {

ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

void ResizeWebView(HWND hwnd) {
    if (!g_controller) {
        return;
    }

    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    g_controller->put_Bounds(bounds);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            ResizeWebView(hwnd);
            return 0;
        case WM_DESTROY:
            g_webview.Reset();
            g_controller.Reset();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"HiddenWebView2Browser";

    if (!RegisterClassW(&wc)) {
        CoUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Hidden Taskbar Browser",
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

                            ResizeWebView(hwnd);
                            g_webview->Navigate(L"https://www.bing.com");
                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        CoUninitialize();
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
