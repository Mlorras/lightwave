#ifdef _WIN32
#pragma once

// The following macros define the minimum required platform.  The minimum required platform
// is the earliest version of Windows, Internet Explorer etc. that has the necessary features to run 
// your application.  The macros work by enabling all features available on platform versions up to and 
// including the version specified.

#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WS08
#endif

#ifndef _WIN32_WINNT 
#define _WIN32_WINNT _WIN32_WINNT_WS08
#endif

#ifndef WINVER 
#define WINVER _WIN32_WINNT
#endif

#ifndef _WIN32_IE 
#define _WIN32_IE _WIN32_IE_WIN6 
#endif

#include <sdkddkver.h>

#endif