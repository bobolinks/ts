/* ====================================================================
 * Copyright (c) 2018-2022 The TS Project.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ====================================================================
 */
#if !defined(_TSS_INC_)
#define _TSS_INC_
#pragma once

#if __cplusplus < 201103L
# error "only for C++11 or later"
#endif

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdexcept>

/*system detecting*/
#if defined(__ANDROID__) || defined (ANDROID)
#    define _OS_ANDROID_
#elif (defined(_WIN32) || defined(_WIN64))
#    if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0400)
#        define _WIN32_WINNT    0x0400 /*here we only support windows NT 4.0 and above*/
#    endif
#    define _OS_WIN_        1
#    define _WINSOCKAPI_
#    include <windows.h>
#    undef _WINSOCKAPI_
#    if defined(WINAPI_FAMILY_PARTITION)
#        if WINAPI_FAMILY_PARTITION(WINAPI_FAMILY_DESKTOP_APP)
#            define _OS_WIN_DESKTOP_    1
#        else
#            define _OS_WIN8_       1
#            define _OS_WIN_APP_    1
#        endif
#    else
#        define _OS_WIN_DESKTOP_    1
#    endif
#elif defined(__APPLE__) || defined(__MACH__)
#    include    <TargetConditionals.h>
#    if TARGET_OS_IPHONE
#        define _OS_IOS_    1
#    elif TARGET_IPHONE_SIMULATOR
#        define _OS_IOS_    1
#        define _OS_IOS_SIMULATOR_  1
#    elif TARGET_OS_MAC
#        define _OS_MAC_    1
#    else
#        error Unsupported platform
#    endif
#elif defined(__linux__) || defined(__CYGWIN__)
#    define _OS_LINUX_      1
#elif (!defined(_OS_MAC_) && !defined(_OS_IOS_) && !defined(_OS_ANDROID_) && !defined(_OS_WIN_) && !defined(_OS_LINUX_))
#    error current system is not supported
#endif

#define _TS_NAMESPACE_BEGIN     namespace ts {
#define _TS_NAMESPACE_END       }
#define _TS_NAMESPACE_USING     using namespace ts;

_TS_NAMESPACE_BEGIN

#if defined(_ERRSTR_R_)
const char* errstr(int);
#else
inline const char* errstr(int e) {
    return strerror(e);
}
#endif

_TS_NAMESPACE_END

#endif /*_TSS_INC_*/
