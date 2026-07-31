# Resolve WebView2 SDK and expose imported target: webview2::loader

set(_WEBVIEW2_ROOT_HINTS)

if(WEBVIEW2_SDK_DIR)
  list(APPEND _WEBVIEW2_ROOT_HINTS "${WEBVIEW2_SDK_DIR}")
endif()

# NuGet package restored at repo level: ./packages/Microsoft.Web.WebView2.<ver>/
file(GLOB _LOCAL_WEBVIEW2_PACKAGES "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2*")
list(SORT _LOCAL_WEBVIEW2_PACKAGES ORDER DESCENDING)
list(APPEND _WEBVIEW2_ROOT_HINTS ${_LOCAL_WEBVIEW2_PACKAGES})

# User-level NuGet cache: %USERPROFILE%/.nuget/packages/microsoft.web.webview2/<ver>/
if(DEFINED ENV{USERPROFILE})
  file(GLOB _USER_NUGET_WEBVIEW2 "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/*")
  list(SORT _USER_NUGET_WEBVIEW2 ORDER DESCENDING)
  list(APPEND _WEBVIEW2_ROOT_HINTS ${_USER_NUGET_WEBVIEW2})
endif()

set(_WEBVIEW2_FOUND_ROOT "")
foreach(_candidate IN LISTS _WEBVIEW2_ROOT_HINTS)
  if(EXISTS "${_candidate}/build/native/include/WebView2.h")
    set(_WEBVIEW2_FOUND_ROOT "${_candidate}")
    break()
  endif()
endforeach()

if(NOT _WEBVIEW2_FOUND_ROOT)
  message(FATAL_ERROR
    "WebView2 SDK not found.\n"
    "- Set -DWEBVIEW2_SDK_DIR=<path to Microsoft.Web.WebView2.*>\n"
    "- Or install via NuGet into ./packages\n"
    "Example: nuget install Microsoft.Web.WebView2 -OutputDirectory packages")
endif()

set(_WEBVIEW2_INCLUDE_DIR "${_WEBVIEW2_FOUND_ROOT}/build/native/include")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_WEBVIEW2_ARCH_DIR x64)
else()
  set(_WEBVIEW2_ARCH_DIR x86)
endif()

set(_WEBVIEW2_LOADER_LIB "${_WEBVIEW2_FOUND_ROOT}/build/native/${_WEBVIEW2_ARCH_DIR}/WebView2LoaderStatic.lib")

if(NOT EXISTS "${_WEBVIEW2_LOADER_LIB}")
  message(FATAL_ERROR "WebView2 loader lib not found: ${_WEBVIEW2_LOADER_LIB}")
endif()

add_library(webview2::loader STATIC IMPORTED GLOBAL)
set_target_properties(webview2::loader PROPERTIES
  IMPORTED_LOCATION "${_WEBVIEW2_LOADER_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${_WEBVIEW2_INCLUDE_DIR}"
)
