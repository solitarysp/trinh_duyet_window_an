# Hidden Browser (C++ + WebView2)

Trình duyệt Win32 dùng WebView2, cửa sổ có `WS_EX_TOOLWINDOW` nên không hiện trên taskbar.

## 1) Cài WebView2 SDK

Cách nhanh (NuGet):

```powershell
nuget install Microsoft.Web.WebView2 -OutputDirectory packages
```

## 2) Build (Visual Studio generator)

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Nếu CMake không tự tìm SDK:

```powershell
cmake -S . -B build -A x64 -DWEBVIEW2_SDK_DIR="C:/path/to/packages/Microsoft.Web.WebView2.1.0.xxxxx.x"
cmake --build build --config Release
```

## 3) Chạy

```powershell
./build/Release/HiddenBrowser.exe
```

## 4) GitHub Actions (CI)

Workflow nằm tại: `.github/workflows/build-windows.yml`

- Trigger: `push`, `pull_request`
- Runner: `windows-latest`
- Steps: restore WebView2 SDK (NuGet) -> CMake configure -> build Release -> upload `HiddenBrowser.exe`

## Notes
- Chỉ hỗ trợ Windows.
- Cần WebView2 Runtime trên máy chạy (đa số Win11 đã có).
