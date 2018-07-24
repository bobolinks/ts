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
#if !defined(_TS_LOG_INC_)
#define _TS_LOG_INC_
#pragma once

#include <stdarg.h>
#include <ts/tss.h>

#define _MAX_EVT_LEN    4096

#ifndef __LOGTAG__
# define    __LOGTAG__  0
#endif

#ifndef __MODULE__
# define    __MODULE__  __FILE__
#endif

_TS_NAMESPACE_BEGIN

namespace log {
    typedef enum {
        /*this type is used for app panic, it most like some app crash or data loss will happen.*/
        panic = 0,
        
        /*this type is used for app logic error*/
        error,
        
        /*this type is used for app logic warning,
         in this case, something happened but we really don't want it be, so just throw a warning
         */
        warning,
        
        /*this type is used to make some tips to user or developer for further purpose.
         there very important thing, like login/out/audit/security operation, is happening.
         */
        generic,
        
        /*this type is used to make some tips to developer for debuging purpose only.*/
        debug
    } level;
    
    typedef void (*hook_t)(level lv, int tag, const char* module, const char* function, int line, const char* message, int contentoffset);

    /**
     init syslog hook to store message.
     
     @h         - hook entry for further use to store message;
     @return    - return previous hook_t entry
     */
    hook_t  hook(hook_t h);
    
    /**
     g is a system log interface to send log messages to TS.
     
     notice: here we don't store any data to disk, we only redirect message to a callback entry previous initialized by hook call.
     
     @lv        - event type, please see 'level' above for more detail;
     @module    - indicate who is calling 'syslog'
     @function  - indicate who is calling 'syslog'
     @line      - indicate code lineno
     @format    - event description
     */
    void    g(level lv, int tag, const char* module, const char* function, int line, const char* format, ...)
#ifndef _MSC_VER
    __attribute__ ((format (printf, 6, 7)))
#endif
    ;
    
    /*
     va_list version of g
     
     @args  - argument list
     */
    void    gv(level lv, int tag, const char* module, const char* function, int line, const char* format, va_list args);
};

#define    log_panic(...)   ts::log::g(ts::log::level::panic,  __LOGTAG__, __MODULE__, __FUNCTION__, __LINE__,  __VA_ARGS__)
#define    log_error(...)   ts::log::g(ts::log::level::error,  __LOGTAG__, __MODULE__, __FUNCTION__, __LINE__,  __VA_ARGS__)
#define    log_warning(...) ts::log::g(ts::log::level::warning,__LOGTAG__, __MODULE__, __FUNCTION__, __LINE__,  __VA_ARGS__)
#define    log_notice(...)  ts::log::g(ts::log::level::generic,__LOGTAG__, __MODULE__, __FUNCTION__, __LINE__,  __VA_ARGS__)

#if defined(_DEBUG) || defined(_DEBUG_) || !defined(NDEBUG)
# define    log_debug(...)  ts::log::g(ts::log::level::debug,   __LOGTAG__, __MODULE__, __FUNCTION__, __LINE__,  __VA_ARGS__)
#else
# define    log_debug(...)
#endif

_TS_NAMESPACE_END

#endif /*_TS_LOG_INC_*/
