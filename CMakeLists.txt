cmake_minimum_required(VERSION 3.20)
project(HiddenBrowser LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project only supports Windows (Win32/WebView2).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW2_SDK_DIR "" CACHE PATH "Path to Microsoft.Web.WebView2 SDK root (contains build/native/include/WebView2.h)")

include(cmake/WebView2.cmake)

add_executable(HiddenBrowser WIN32 src/main.cpp)

target_compile_definitions(HiddenBrowser PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(HiddenBrowser PRIVATE webview2::loader user32 ole32)
