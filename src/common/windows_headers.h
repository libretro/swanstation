#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

// require vista+, but don't set it when compiling for UWP
#ifndef WINAPI_FAMILY
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif

#include <windows.h>
