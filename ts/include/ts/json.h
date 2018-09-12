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
#if !defined(_TS_JSON_INC_)
#define _TS_JSON_INC_
#pragma once

#include <ts/pie.h>

_TS_NAMESPACE_BEGIN

namespace json {
    constexpr static int flags_is_boolean   = 0x000001;
    constexpr static int flags_is_jsfunction= 0x000002;

    bool            parse(ts::pie& out, const char* src, std::string& err);
    std::string&    format(const ts::pie& js, std::string& out, bool quot = false, bool align = false);

    bool            fromFile(ts::pie& out, const char* file, std::string& err);
    long            toFile(const ts::pie& js, const char* file, bool align = false);

    //tool set, skip '\n','\r','\t',' ', line comment(//) and comment block(/**/)
    bool            skip_unmeaning(std::string& err, const char*& ptr, int len, int& line);
    void            skip_until_commit(std::string& err, const char*& ptr, int len, int& line, const std::pair<char, char>& parantheses);
    bool            skip_parantheses(std::string& err, const char*& ptr, int len, int& line, const std::pair<char, char>& parantheses);
};

_TS_NAMESPACE_END

#endif /*_TS_JSON_INC_*/
