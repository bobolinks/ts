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
#if !defined(_TS_STRING_INC_)
#define _TS_STRING_INC_
#pragma once

#include <ts/tss.h>
#include <string>

_TS_NAMESPACE_BEGIN

namespace string {
    char* stristr(char* s, const char* pattern);
    const char* stristr(const char* s, const char* pattern);

    std::string  format(const char *fmt, ...);
    std::string& format(std::string&s, const char *fmt, ...);
    std::string& toupper(std::string&s);
    
    std::string  tostr(const uint8_t* ptr, int len);
    std::string  dump(const uint8_t* p, int len);
    
    const char*  eatdot(const char* path, std::string& eaten);
};

_TS_NAMESPACE_END

#endif /*_TS_STRING_INC_*/
