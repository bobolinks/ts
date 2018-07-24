#include <stdio.h>
#include <time.h>
#include <iostream>
#include <thread>
#include <ts/log.h>

_TS_NAMESPACE_BEGIN

namespace log {
    static void _buildin_syslog_func(level lv, int tag, const char* module, const char* function, int line, const char* message, int contentoffset) {
        printf("%s\n", message);
    }
    
    static hook_t       s_hook = &_buildin_syslog_func;
    static const char*  s_lv2desc[] = {"P", "E", "W", "N", "D", nullptr};
    
    /**
     init syslog hook to store message.
     
     @h         - hook entry for further use to store message;
     @return    - return previous hook_t entry
     */
    hook_t  hook(hook_t h) {
        hook_t old_hook = s_hook == &_buildin_syslog_func ? nullptr : s_hook;
        s_hook = h;
        return old_hook;
    }
    
    
    /**
     g is a system log interface to send log messages to TS.
     
     notice: here we don't store any data to disk, we only redirect message to a callback entry previous initialized by hook call.
     
     @lv        - event type, please see 'level' above for more detail;
     @module    - indicate who is calling 'syslog'
     @function  - indicate who is calling 'syslog'
     @line      - indicate code lineno
     @format    - event description
     */
    void    g(level lv, int tag, const char* module, const char* function, int line, const char* format, ...) {
        va_list        args;
        va_start(args, format);
        gv(lv, tag, module, function, line, format, args);
        va_end(args);
    }
    
    /*
     va_list version of g
     
     @args  - argument list
     */
    void    gv(level lv, int tag, const char* module, const char* function, int line, const char* format, va_list args) {
        if (!s_hook) {
            return;
        }
        
        char    tmp[16];
        char    msg[_MAX_EVT_LEN + 7];    /*5bytes for '...' and '\r\n'*/
        time_t  tm = time(nullptr);
        int     len = 0, _r = 0, width = 8;
        int     contentoffset = 0;
        
        /*20 bytes log time*/
        len = (int)strftime(msg, _MAX_EVT_LEN, "[%Y-%m-%d %H:%M:%S]", localtime(&tm));
        
        /*1 bytes log level*/
        strcpy(&(msg[len]), s_lv2desc[lv]);
        len++;
        msg[len++] = '|';

        /*12 bytes log thread info*///0~4294967296
        pthread_t tid = pthread_self();
        uint64_t thread_id = 0;
        memcpy(&thread_id, &tid, std::min(sizeof(thread_id), sizeof(tid)));
        width = 12;
        _r = sprintf(&(msg[len]), "%llu", thread_id);
        if (_r < width)memset(&msg[len + _r], ' ', width - _r);
        len += width;
        msg[len++] = '|';
        
        /*24 bytes log module:line*/
        width = 24;
        int mlen = (int)strlen(module);
        if (mlen) {
            const char* me = module + mlen - 1;
            while (me > module) {
                if (*me == '/' || *me == '\\') {
                    mlen -= me - module + 1;
                    module = me + 1;
                    break;
                }
                me--;
            }
        }
        _r = sprintf(tmp, ":%d", line);
        if ((_r + mlen) < width) {
            memset(&msg[len + mlen + _r], ' ', width - _r - mlen);
        }
        else {
            mlen = width - _r;
        }
        strncpy(&(msg[len]), module, mlen);
        strncpy(&(msg[len + mlen]), tmp, _r);
        len += width;
        msg[len++] = '|';
        
#if 0
        /*20 bytes log function name*/
        width = 20;
        if (*function == '-' || *function == '+') {//objective-c style
            function = strchr(function, ' ');
            if (function)function++;
        }
        _r = function ? (int)strlen(function) : 0;
        if (_r < width)memset(&msg[len + _r], ' ', width - _r);
        else _r = width;
        if (function)strncpy(&(msg[len]), function, _r);
        len += width;
        msg[len++] = '|';
#endif
        
        /*log message*/
        msg[len++] = ':';
        msg[len++] = ' ';
        contentoffset = len;
        _r = vsnprintf(&(msg[len]), _MAX_EVT_LEN - len, format, args);
        if (_r < 0) {
            /*utf-8 truncate*/
            int maxln = _MAX_EVT_LEN;
            if (msg[_MAX_EVT_LEN - 1] < 0) {
                maxln--;
                if (msg[_MAX_EVT_LEN - 2] < 0) {
                    maxln--;
                }
            }
            strcpy(&(msg[maxln]), "...");
            len = maxln + 3;
        }
        else if (_r >= (_MAX_EVT_LEN - len)) {
            strcpy(&(msg[_MAX_EVT_LEN]), "...");
            len = _MAX_EVT_LEN + 3;
        }
        else {
            len += _r;
        }
        msg[len] = 0;
        
        s_hook(lv, tag, module, function, line, msg, contentoffset);
    }
}


_TS_NAMESPACE_END
