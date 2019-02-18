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
#if !defined(_TS_HTTP_INC_)
#define _TS_HTTP_INC_
#pragma once

#include <map>
#include <ts/net.h>

_TS_NAMESPACE_BEGIN

namespace net { namespace http {
    //connection::patch[0] is used
    
    struct server : public net::server {
    public:
        server(runnable const& host, uint16_t concurrent = 128, uint64_t session_timeout = 0 /*in seconds*/);
        ~server(void);
        
        /**
         start server
         @ip        - ip address
         @port      - port to listening
         */
        bool    start(const char* ipv4, uint16_t port);
        
    private:
        void    setup(std::string ipv4, uint16_t port);

    protected:
        void    commit(std::shared_ptr<net::connection> pconn, std::shared_ptr<std::string> response);
        
    protected:
        //ts::net::server
        void    onConnectionClose(std::shared_ptr<net::connection>& pconn);

    private:
        void    expireSession(std::shared_ptr<net::connection> pconn, time_t expiredKey);
        
        //default action is to close new request
        virtual void onSessionRequest(std::shared_ptr<net::connection> pconn, const std::string url, std::shared_ptr<std::string> headers, const std::string contentType, std::shared_ptr<std::string> body) {pconn->close();}
        
    private:
        //ts::net::server
        void    onConnectionRecv(std::shared_ptr<net::connection>&& pconn, const net::address_t& from, std::shared_ptr<std::string>& packet) final;
        void    onConnectionComming(std::shared_ptr<net::connection>&& pconn) final;
        
    private:
        struct http_server_cxt*  _cxt;
    };
    
    int request(const char* url, const char* method, std::string& rspBody, const char* contentType = nullptr, const std::string* body = nullptr, std::map<std::string, std::string>* cookie = nullptr, std::map<std::string, std::string>* rspCookie = nullptr);
}};

_TS_NAMESPACE_END

#endif /*_TS_HTTP_INC_*/
