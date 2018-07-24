// http://tools.ietf.org/html/rfc6455#section-5.2  Base Framing Protocol
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-------+-+-------------+-------------------------------+
// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
// | |1|2|3|       |K|             |                               |
// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
// |     Extended payload length continued, if payload len == 127  |
// + - - - - - - - - - - - - - - - +-------------------------------+
// |                               |Masking-key, if MASK set to 1  |
// +-------------------------------+-------------------------------+
// | Masking-key (continued)       |          Payload Data         |
// +-------------------------------- - - - - - - - - - - - - - - - +
// :                     Payload Data continued ...                :
// + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
// |                     Payload Data continued ...                |
// +---------------------------------------------------------------+
#include <regex>
#include <ts/log.h>
#if defined(_OS_WIN_)
# include <winsock2.h>
# pragma comment(lib, "ws2_32.lib")
#else
# ifdef _OS_ANDROID_
# else
#  include <ifaddrs.h>
# endif
# include <unistd.h>
# include <arpa/inet.h>
# include <netdb.h>
#endif
#include <ts/string.h>
#if defined(USE_OPENSSL)
# include <openssl/err.h>
# include <openssl/ssl.h>
#endif
#include "websocket.h"

_TS_NAMESPACE_BEGIN

namespace net { namespace websocket {
#if defined(USE_OPENSSL)
    static struct ssl_cxt_holder {
        SSL_CTX *ctx;
        const SSL_METHOD *meth;
        ssl_cxt_holder(void) {
            SSL_library_init();
            SSLeay_add_ssl_algorithms();
            SSL_load_error_strings();
            meth = TLSv1_2_client_method();
            ctx = SSL_CTX_new (meth);
        }
        ~ssl_cxt_holder(void) {
            if (ctx) {
                SSL_CTX_free(ctx);
                ctx = NULL;
                meth = NULL;
            }
        }
    } __ssl_cxt_holder;
    
    struct ssl_io : public net::connection_io {
        SSL*    ssl;
        
        ssl_io(void) {
            ssl = SSL_new (__ssl_cxt_holder.ctx);
            if (!ssl) {
                log_error("Error creating SSL.");
            }
        }
        ~ssl_io(void) {
            if (ssl) {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                ssl = nullptr;
            }
        }
        size_t send(connection& conn, const uint8_t* data, size_t size) {
            return SSL_write(ssl, data, (int)size);
        }
        
        size_t recv(connection& conn, std::shared_ptr<std::string>& packet, size_t size) {
            return SSL_read(ssl, const_cast<char*>(packet->c_str()), (int)size);
        }
    };
#endif
    
    struct header_t {
        uint8_t header_size;
        bool    fin;
        bool    mask;
        enum opcode_t {
            CONTINUATION = 0x0,
            TEXT_FRAME = 0x1,
            BINARY_FRAME = 0x2,
            CLOSE = 8,
            PING = 9,
            PONG = 0xa,
        } opcode;
        int N0;
        uint64_t    bodylen;
        uint8_t masking_key[4];
    };

    struct session_t : public net::connection_patch {
        //for session
        std::string sUrl;
        std::string sHost;
        std::string sOrigin;
        std::map<std::string, std::string> sHeaders;
        std::map<std::string, std::string> sCookies;
        bool        handshook;
        bool        usingMask;

        std::shared_ptr<std::string> sRxBuf;
        std::shared_ptr<std::string> sBody;

        void reset(void) {
            sRxBuf.reset(new std::string());
            sBody.reset(new std::string());
        }
    };
    
    connector::connector(runnable const& host, bool usingMask) : net::connector(host) {
        std::shared_ptr<session_t> psnn(new session_t);
        session_t& session = *psnn.get();
        session.reset();
        session.handshook = false;
        session.usingMask = usingMask;
        get()->setPatch(0, psnn);
    }
    
    connector::~connector(void) {
    }

    void buildFrame(header_t::opcode_t type, bool usingMask, const std::string& msg, std::shared_ptr<std::string>& out) {
        // TODO:
        // Masking key should (must) be derived from a high quality random
        // number generator, to mitigate attacks on non-WebSocket friendly
        // middleware:
        uint32_t rdnm = usingMask ? std::rand() : 0;
        const uint8_t* masking_key = (uint8_t*)&rdnm;
        // TODO: consider acquiring a lock on txbuf...

        uint64_t message_size = msg.length();
        std::vector<uint8_t> header;
        header.assign(2 + (message_size >= 126 ? 2 : 0) + (message_size >= 65536 ? 6 : 0) + (usingMask ? 4 : 0), 0);
        header[0] = 0x80 | type;
        if (false) { }
        else if (message_size < 126) {
            header[1] = (message_size & 0xff) | (usingMask ? 0x80 : 0);
            if (usingMask) {
                header[2] = masking_key[0];
                header[3] = masking_key[1];
                header[4] = masking_key[2];
                header[5] = masking_key[3];
            }
        }
        else if (message_size < 65536) {
            header[1] = 126 | (usingMask ? 0x80 : 0);
            header[2] = (message_size >> 8) & 0xff;
            header[3] = (message_size >> 0) & 0xff;
            if (usingMask) {
                header[4] = masking_key[0];
                header[5] = masking_key[1];
                header[6] = masking_key[2];
                header[7] = masking_key[3];
            }
        }
        else { // TODO: run coverage testing here
            header[1] = 127 | (usingMask ? 0x80 : 0);
            header[2] = (message_size >> 56) & 0xff;
            header[3] = (message_size >> 48) & 0xff;
            header[4] = (message_size >> 40) & 0xff;
            header[5] = (message_size >> 32) & 0xff;
            header[6] = (message_size >> 24) & 0xff;
            header[7] = (message_size >> 16) & 0xff;
            header[8] = (message_size >>  8) & 0xff;
            header[9] = (message_size >>  0) & 0xff;
            if (usingMask) {
                header[10] = masking_key[0];
                header[11] = masking_key[1];
                header[12] = masking_key[2];
                header[13] = masking_key[3];
            }
        }
        // N.B. - txbuf will keep growing until it can be transmitted over the socket:
        size_t oln = out->length();
        out->append((const char*)&header[0], header.size());
        out->append(msg);
        size_t nln = out->length();
        if (usingMask) {
            size_t message_offset = nln - oln - message_size;
            char* ptx = const_cast<char*>(out->c_str() + oln);
            for (size_t i = 0; i != message_size; ++i) {
                ptx[message_offset + i] ^= masking_key[i&0x3];
            }
        }
    }
    
    bool    connector::connect(const char* url, const char* origin, uint64_t timeout) {
        const char* p = url;
        const char* token = url;
        if(url == NULL) {
            log_error("[%d]Illegal parameters!", __LINE__);
            return false;
        }
        while (*p && *p != ':') {
            p++;
        }
        
        std::string sProtocol(token, p - token);
        
#if defined(USE_OPENSSL)
        bool isSSL = strncasecmp(sProtocol.c_str(), "wss", 3) == 0;
#else
        bool isSSL = false;
#endif
        
        p += 3;
        token = p;
        
        while (*p && *p != ':' && *p != '/') {
            p++;
        }
        std::string sHost(token, p - token);
        uint16_t port = uint16_t(*p == ':' ? atoi(p + 1) : (isSSL?443:80));
        
        struct hostent* phost = NULL;
        u_long  addr = 0;
        
        phost = gethostbyname(sHost.c_str());
        if(phost == NULL) {
            log_error("name resolved failed!");
            return false;
        }
        addr = *(unsigned long*)(phost->h_addr_list[0]);

        std::shared_ptr<connection> pconn = get();
        std::shared_ptr<session_t> pssn = std::dynamic_pointer_cast<session_t>(pconn->getPatch(0));
        session_t& ssn = *pssn.get();
        ssn.sUrl = url;
        if (port != 80 && port != 443) {
            ts::string::format(ssn.sHost, "%s:%d", sHost.c_str(), port);
        }
        else {
            ssn.sHost = sHost;
        }
        if (origin) {
            ssn.sOrigin = origin;
        }
        else {
            ts::string::format(ssn.sOrigin, "%s://%s", isSSL ? "https" : "http", sHost.c_str());
        }
        
#if defined(USE_OPENSSL)
        if (isSSL) {
            get()->setStreamer(std::shared_ptr<connection_io>(new ssl_io()));
        }
#endif
        
        net::address_t target(inet_ntoa(*(in_addr*)&addr), port, net::TCP, net::V4);
        if (net::connector::connect(target, timeout, isSSL ? false : true) == false) { //as blocking mode
            return false;
        }
        
#if defined(USE_OPENSSL)
        ssl_io* io = nullptr;
        if ((io = (ssl_io*)pconn->getStreamer().get()) != nullptr) { //is ssl
            int err = SSL_set_fd(io->ssl, pconn->id());
            err = SSL_connect(io->ssl);
            int errorStatus = SSL_get_error (io->ssl, err);
            if (err <= 0) {
                log_error("Error creating SSL connection.  err=%s", ERR_error_string(errorStatus, nullptr));
                close();
                return false;
            }
            //enable nonblock again
            nonblock(true);
            
            onConnectionConnected(pconn);
        }
#endif
        return true;
    }

    void    connector::sendMessage(std::shared_ptr<std::string>& message) {
        std::shared_ptr<session_t> pssn = std::dynamic_pointer_cast<session_t>(get()->getPatch(0));
        session_t& ssn = *pssn.get();
        std::shared_ptr<std::string> packet(new std::string);
        buildFrame(header_t::TEXT_FRAME, ssn.usingMask, *message.get(), packet);
        get()->send(packet);
    }

    void    connector::onConnectionRecv(std::shared_ptr<connection>& pconn, const address_t& from, std::shared_ptr<std::string>& packet) {
        net::connection& conn = *pconn.get();
        std::shared_ptr<session_t> pssn = std::dynamic_pointer_cast<session_t>(conn.getPatch(0));
        if (!pssn) {
            log_error("patch has not be set up!");
            return;
        }
        
        printf("<<%s\n", packet->c_str());
        
        session_t& ssn = *pssn.get();
        ssn.sRxBuf->append(*packet.get());
        
        if (ssn.handshook == false) {
            const char* ptr = ssn.sRxBuf->c_str();
            const char* lnln = strstr(ptr, "\r\n\r\n");
            if (lnln) {
                const char* ln = strstr(ptr, "\r\n"); //first line
                const char* lnCode = ptr;
                while (*lnCode != ' ' && lnCode != ln) {
                    lnCode++;
                }
                int code = atoi(lnCode + 1);

                int len = (int)(lnln + 2 - ptr);
                std::regex word_regex("(\\S+): ([^\\r\\n]+)\\r\\n");
                auto words_begin = std::sregex_iterator(ssn.sRxBuf->begin(), ssn.sRxBuf->begin() + len, word_regex);
                for (std::sregex_iterator i = words_begin; i != std::sregex_iterator(); ++i) {
                    std::smatch match = *i;
                    std::string value = match[2];
                    ssn.sHeaders[match[1]] = value;
                    
                    if (match[1] == "Set-Cookie") { //set cookie
                        std::regex cookie_regex("(\\S+)(=\\S+;)?");
                        auto cookies_begin = std::sregex_iterator(value.begin(), value.end(), cookie_regex);
                        for (std::sregex_iterator j = cookies_begin; j != std::sregex_iterator(); ++j) {
                            std::smatch cookie_match = *j;
                            std::string cookie_value = cookie_match.size() >= 3 ? cookie_match[2] : std::string("");
                            ssn.sCookies[cookie_match[1]] = cookie_value;
                        }
                    }
                }
                ssn.sRxBuf->erase(ssn.sRxBuf->begin(), ssn.sRxBuf->begin() + len + 2);
                ssn.handshook = true;
                onHandshook(code, ssn.sHeaders, ssn.sCookies);
            }
            else return;
        }
        
        std::string& rxbuf = *ssn.sRxBuf.get();
        std::string& receivedData = *ssn.sBody.get();
        
        bool handleClose = false;

        while (true) {
            if (rxbuf.size() < 2) { break; /* Need at least 2 */ }
            const uint8_t * data = (uint8_t *) &rxbuf[0]; // peek, but don't consume
            header_t ws;
            ws.fin = (data[0] & 0x80) == 0x80;
            ws.opcode = (header_t::opcode_t) (data[0] & 0x0f);
            ws.mask = (data[1] & 0x80) == 0x80;
            ws.N0 = (data[1] & 0x7f);
            ws.header_size = 2 + (ws.N0 == 126? 2 : 0) + (ws.N0 == 127? 8 : 0) + (ws.mask? 4 : 0);
            if (rxbuf.size() < ws.header_size) { break; /* Need: ws.header_size - rxbuf.size() */ }
            int i = 0;
            if (ws.N0 < 126) {
                ws.bodylen = ws.N0;
                i = 2;
            }
            else if (ws.N0 == 126) {
                ws.bodylen = 0;
                ws.bodylen |= ((uint64_t) data[2]) << 8;
                ws.bodylen |= ((uint64_t) data[3]) << 0;
                i = 4;
            }
            else if (ws.N0 == 127) {
                ws.bodylen = 0;
                ws.bodylen |= ((uint64_t) data[2]) << 56;
                ws.bodylen |= ((uint64_t) data[3]) << 48;
                ws.bodylen = ((uint64_t) data[4]) << 40;
                ws.bodylen |= ((uint64_t) data[5]) << 32;
                ws.bodylen |= ((uint64_t) data[6]) << 24;
                ws.bodylen |= ((uint64_t) data[7]) << 16;
                ws.bodylen |= ((uint64_t) data[8]) << 8;
                ws.bodylen |= ((uint64_t) data[9]) << 0;
                i = 10;
            }
            if (ws.mask) {
                ws.masking_key[0] = ((uint8_t) data[i+0]) << 0;
                ws.masking_key[1] = ((uint8_t) data[i+1]) << 0;
                ws.masking_key[2] = ((uint8_t) data[i+2]) << 0;
                ws.masking_key[3] = ((uint8_t) data[i+3]) << 0;
            }
            else {
                ws.masking_key[0] = 0;
                ws.masking_key[1] = 0;
                ws.masking_key[2] = 0;
                ws.masking_key[3] = 0;
            }
            if (rxbuf.size() < ws.header_size+ws.bodylen) { break; /* Need: ws.header_size+ws.N - rxbuf.size() */ }
            
            // We got a whole message, now do something with it:
            if (false) { }
            else if (
                     ws.opcode == header_t::TEXT_FRAME
                     || ws.opcode == header_t::BINARY_FRAME
                     || ws.opcode == header_t::CONTINUATION
                     ) {
                if (ws.mask) {
                    for (size_t i = 0; i != ws.bodylen; ++i) {
                        rxbuf[i+ws.header_size] ^= ws.masking_key[i&0x3];
                    }
                }
                receivedData.insert(receivedData.end(), rxbuf.begin()+ws.header_size, rxbuf.begin()+ws.header_size+(size_t)ws.bodylen);// just feed
                if (ws.fin) {
                    std::shared_ptr<std::string> msg = ssn.sBody;
                    onMessage(msg);
                    receivedData.clear();
                }
            }
            else if (ws.opcode == header_t::PING) {
                if (ws.mask) { for (size_t i = 0; i != ws.bodylen; ++i) { rxbuf[i+ws.header_size] ^= ws.masking_key[i&0x3]; } }
                std::string data(rxbuf.begin()+ws.header_size, rxbuf.begin()+ws.header_size+(size_t)ws.bodylen);
                if (!handleClose) {
                    std::shared_ptr<std::string> packet(new std::string);
                    buildFrame(header_t::PONG, ssn.usingMask, data, packet);
                    get()->send(packet);
                }
            }
            else if (ws.opcode == header_t::PONG) { }
            else if (ws.opcode == header_t::CLOSE) { handleClose = true; }
            else { log_error("ERROR: Got unexpected WebSocket message.\n"); handleClose = true; }
            
            rxbuf.erase(rxbuf.begin(), rxbuf.begin() + ws.header_size+(size_t)ws.bodylen);
        }
        
        if (handleClose) {
            pconn->close();
        }
    }
    
    void    connector::onConnectionConnected(std::shared_ptr<connection>& pconn) {
        std::shared_ptr<session_t> pssn = std::dynamic_pointer_cast<session_t>(get()->getPatch(0));
        session_t& ssn = *pssn.get();
        //construct resuest
        std::shared_ptr<std::string> packet(new std::string);
        
#if 1
        ts::string::format(*packet.get(), "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Pragma: no-cache\r\n"
                           "Cache-Control: no-cache\r\n"
                           "Origin: %s\r\n"
                           "Sec-WebSocket-Key: BADimGl2o/RIaa2Z46yDYw==\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "Accept-Encoding: gzip, deflate, br\r\n"
                           "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
                           "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
                           "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_13_5) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/67.0.3396.99 Safari/537.36\r\n"
                           "Cookie: __cfduid=d4a849ed96fbc6d929cf7f941f62e67b61531732903; gr_user_id=e294fe61-1760-4ea4-ae3b-e061cde2f1f6; _ga=GA1.2.967375190.1531732906; _gid=GA1.2.894532720.1531732906; grwng_uid=95f1ece8-1c2e-47a6-9d3d-35fa5f9e902d; __zlcmid=nQhUhVGCk8oQQ8; 8838a5745a973a12_gr_session_id=096c1d6a-fb35-4e81-9b3b-099c0720bd9c; 8838a5745a973a12_gr_session_id_096c1d6a-fb35-4e81-9b3b-099c0720bd9c=true; SESSION=70376cd7-e218-4adf-87cb-6e140e6bd37d; ADRUM=s=1531986381322&r=https%%3A%%2F%%2Fwww.hbg.com%%2Fzh-cn%%2Fcoin_coin%%2Fexchange%%2F%%3F396617505\r\n"
                           "\r\n", ssn.sUrl.c_str(), ssn.sHost.c_str(), ssn.sOrigin.c_str());
#else
        *packet.get() = ""
        "GET /zh-cn/btc_usdt/exchange/ HTTP/1.1\r\n"
        "Host: www.hbg.com\r\n"
        "Origin: https://www.hbg.com\r\n"
        "accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\n"
        "accept-encoding: gzip, deflate, br\r\n"
        "accept-language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
        "cache-control: max-age=0\r\n"
        "cookie: _uab_collina=153199044947128087267389; _uab_collina=153199257723436547127946; 8838a5745a973a12_gr_session_id=d32dcd4d-bd45-4232-9578-27d1655f84fc; __cfduid=d283e10722fd6df38ee1263c2558caf5d1531989058; gr_user_id=981128fc-3cce-48e0-b1b1-bb007733daf8; grwng_uid=f7f54fee-0368-4633-a7df-e26b42e66382; SESSION=e8b85d83-0b70-41d6-84e1-17e2efce2ecc; _ga=GA1.2.2016936988.1531989064; _gid=GA1.2.61143656.1531989064; __zlcmid=nThV9ofF4idqSA; HB-UC-TOKEN=pBGf9EtAP%%2BNV8TOhfkw9QthCuAmbLVhC%%2BAAHWaOKdkXkY6QVHs2tqQJIdcnjhZuG9DpVANaVpKobUxuZXcNEooLHF5hxoUEDGQInhPkEOMWdDlMssFy0KCG%%2Fge6YG3KRkwOgAuEKiLf74mE0L6ZzuSFlBfAXqu8D%%2FhtoAi462R8%%3D; HB-PRO-TOKEN=d3M190UC_xyclhuAraLAzLpXqcz-DEinIy6RaCKj7z0Y-uOP2m0-gvjE57ad1qDF; HB-VOTE-TOKEN=ee5b23e685164c4b85fad86e571ca11b; 8838a5745a973a12_gr_last_sent_sid_with_cs1=d32dcd4d-bd45-4232-9578-27d1655f84fc; 8838a5745a973a12_gr_last_sent_cs1=A7FE8D839BDD973EE9DF3B65333203101737C3AE39E4231B4192D8B976BE3C0E; 8838a5745a973a12_gr_session_id_d32dcd4d-bd45-4232-9578-27d1655f84fc=true; HB-OLD-TOKEN=d3M190UC_xyclhuAraLAzLpXqcz-DEinIy6RaCKj7z0Y-uOP2m0-gvjE57ad1qDF; HBP_inviterId=11116490; 8838a5745a973a12_gr_cs1=A7FE8D839BDD973EE9DF3B65333203101737C3AE39E4231B4192D8B976BE3C0E; _gat_gtag_UA_108346576_1=1; ADRUM=s=1531995947212&r=https%%3A%%2F%%2Fwww.hbg.com%%2Fzh-cn%%2Fbtc_usdt%%2Fexchange%%2F%%3F0\r\n"
        "if-modified-since: Wed, 18 Jul 2018 10:31:52 GMT\r\n"
        "referer: https://www.hbg.com/btc_usdt/exchange/\r\n"
        "upgrade-insecure-requests: 1\r\n"
        "user-agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_13_5) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/67.0.3396.99 Safari/537.36\r\n"
        "\r\n"
#if 0
        "GET wss://www.hbg.com/-/s/pro/ws HTTP/1.1\r\n"
        "Host: www.hbg.com\r\n"
        "Connection: Upgrade\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-cache\r\n"
        "Upgrade: websocket\r\n"
        "Origin: https://www.hbg.com\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_13_5) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/67.0.3396.99 Safari/537.36\r\n"
        "Accept-Encoding: gzip, deflate, br\r\n"
        "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
        "Cookie: 8838a5745a973a12_gr_session_id=d32dcd4d-bd45-4232-9578-27d1655f84fc; __cfduid=d283e10722fd6df38ee1263c2558caf5d1531989058; gr_user_id=981128fc-3cce-48e0-b1b1-bb007733daf8; grwng_uid=f7f54fee-0368-4633-a7df-e26b42e66382; SESSION=e8b85d83-0b70-41d6-84e1-17e2efce2ecc; _ga=GA1.2.2016936988.1531989064; _gid=GA1.2.61143656.1531989064; __zlcmid=nThV9ofF4idqSA; HB-UC-TOKEN=pBGf9EtAP%%2BNV8TOhfkw9QthCuAmbLVhC%%2BAAHWaOKdkXkY6QVHs2tqQJIdcnjhZuG9DpVANaVpKobUxuZXcNEooLHF5hxoUEDGQInhPkEOMWdDlMssFy0KCG%%2Fge6YG3KRkwOgAuEKiLf74mE0L6ZzuSFlBfAXqu8D%%2FhtoAi462R8%%3D; HB-PRO-TOKEN=d3M190UC_xyclhuAraLAzLpXqcz-DEinIy6RaCKj7z0Y-uOP2m0-gvjE57ad1qDF; HB-VOTE-TOKEN=ee5b23e685164c4b85fad86e571ca11b; 8838a5745a973a12_gr_last_sent_sid_with_cs1=d32dcd4d-bd45-4232-9578-27d1655f84fc; 8838a5745a973a12_gr_last_sent_cs1=A7FE8D839BDD973EE9DF3B65333203101737C3AE39E4231B4192D8B976BE3C0E; 8838a5745a973a12_gr_session_id_d32dcd4d-bd45-4232-9578-27d1655f84fc=true; HB-OLD-TOKEN=d3M190UC_xyclhuAraLAzLpXqcz-DEinIy6RaCKj7z0Y-uOP2m0-gvjE57ad1qDF; _gat=1; ADRUM=s=1531992638556&r=https%%3A%%2F%%2Fwww.hbg.com%%2Fzh-cn%%2Fbtc_usdt%%2Fexchange%%2F%%3F0; 8838a5745a973a12_gr_cs1=A7FE8D839BDD973EE9DF3B65333203101737C3AE39E4231B4192D8B976BE3C0E\r\n"
        "Sec-WebSocket-Key: hRaOlBYWuA3jOJAgfBj3rg==\r\n"
        "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
        "\r\n"
#endif
        ;
#endif

        send(packet);
    }

    void    connector::sendHttpRequest(std::shared_ptr<std::string>& message) {
        send(message);
    }

};};

_TS_NAMESPACE_END
