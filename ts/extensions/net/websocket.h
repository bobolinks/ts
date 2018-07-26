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
#if !defined(_TS_WEBSOCKET_INC_)
#define _TS_WEBSOCKET_INC_
#pragma once

#include <ts/net.h>
#include <map>

_TS_NAMESPACE_BEGIN

namespace net { namespace websocket {
    //connection::patch[0] is used
    struct connector : protected net::connector {
        template <class T, typename... Args> friend struct ts::bind_t;
        
        connector(runnable const& host, bool usingMask = true);
        ~connector(void);

        bool    connect(const char* url, const char* origin = nullptr, uint64_t timeout = 0 /*in seconds*/);
        void    sendMessage(std::shared_ptr<std::string>& message);
        void    sendHttpRequest(std::shared_ptr<std::string>& message);
        void    ping(std::shared_ptr<std::string>& message);

    private:
        //socket functional
        void    onConnectionRecv(std::shared_ptr<connection>& conn, const address_t& from, std::shared_ptr<std::string>& packet) final;
        void    onConnectionClose(std::shared_ptr<connection>& conn) {} //do nothing default
        
        //wss funcational
        virtual void onMessage(std::shared_ptr<std::string> packet) = 0;
        virtual void onHandshook(int code, const std::map<std::string, std::string>& headers, const std::map<std::string, std::string>& cookies) {}
        
    protected:
        void    onConnectionConnected(std::shared_ptr<connection>& conn);
    };
};};

_TS_NAMESPACE_END

#endif /*_TS_WEBSOCKET_INC_*/
