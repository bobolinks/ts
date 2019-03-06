#ifdef _OS_WIN_
typedef int32 socklen_t;
#pragma comment(lib, "ws2_32.lib")
#else
#include <errno.h>
#ifdef _OS_ANDROID_
#    include <linux/if.h>
#    include <sys/select.h>
#else
#    include <ifaddrs.h>
#endif
#    include <sys/types.h>
#    include <fcntl.h>
#    include <unistd.h>
#    include <sys/ioctl.h>
#    include <sys/utsname.h>
#    include <arpa/inet.h>
#    include <sys/socket.h>
#    include <net/if.h>
#    include <netinet/tcp.h>
#    include <netinet/in.h>
#    include <netdb.h>
#endif
#if defined(USE_OPENSSL)
# include <openssl/err.h>
# include <openssl/ssl.h>
#else
typedef struct SSL {} SSL;
#endif
#include <string>
#include <mutex>
#include <list>
#include <algorithm>
#include <ts/log.h>
#include <ts/string.h>
#include "http.h"

#ifndef _OS_WIN_
#    define strnicmp    strncasecmp
#    define stricmp     strcasecmp
#endif

_TS_NAMESPACE_USING
_TS_NAMESPACE_BEGIN

#define MAX_HEADERS_LEN         (1024 * 1024)      //1024K bytes

namespace net { namespace http {
    typedef long long  i64;
    typedef unsigned long long  u64;
    
    struct session_t : public net::connection_patch {
        //for session
        bool        keepalive;
        //-->
        
        //for request
        std::string sUrl;
        std::string sContentType;
        std::shared_ptr<std::string> sHeaders;
        std::shared_ptr<std::string> sBody;
        std::shared_ptr<std::string> sOut;
        
        int         hdpro;
        bool        hddone;
        int         bdpro;
        int         bdlen;
        bool        bddone;
        
        i64         posb; // < 0 for backward seeking
        u64         pose;
        u64         ttl;
        //-->
        
        time_t      tmActive;
        
        void reset(void) {
            sUrl.clear();
            sContentType.clear();
            sHeaders.reset(new std::string());
            sBody.reset(new std::string());
            sOut.reset(new std::string());
            
            hdpro   = 0;
            hddone  = false;
            bdpro   = 0;
            bdlen   = 0;
            bddone  = false;
            
            posb    = 0;
            pose    = 0;
            ttl     = 0;
            
            tmActive    = 0;
        }
    };
    
    struct http_server_cxt {
        uint16_t    _timeout;
    };
    
    server::server(runnable const& host, uint16_t concurrent, uint64_t session_timeout) : net::server(host, concurrent) {
        _cxt = new http_server_cxt();
        _cxt->_timeout = session_timeout;
    }
    
    server::~server(void) {
        delete _cxt;
    }
    
    bool server::start(const char* ipv4, uint16_t port) {
        ts::asyn2(std::dynamic_pointer_cast<runnable>(_host.clone()), this, &server::setup, std::string(ipv4), port);
        return true;
    }
    
    void server::setup(std::string ipv4, uint16_t port) {
        net::address_t loc(ipv4.c_str(), port, net::TCP, net::V4);
        if (net::server::bind(loc) == false) {
            log_panic("failed to setup rpc server!");
        }
    }
    
    void server::commit(std::shared_ptr<net::connection> pconn, std::shared_ptr<std::string> response) {
        if (runnable::current() != &_host) {
            ts::asyn2(std::dynamic_pointer_cast<ts::runnable>(this->clone()), this, &server::commit, pconn, response);
            return;
        }
        net::connection& conn = *pconn.get();
        session_t& session = *std::dynamic_pointer_cast<session_t>(conn.getPatch(0));
        
        session.ttl = response->length();
        
        //construct response header
        char rxbuf[1024];
        int rcode = session.posb ? 206 : (session.ttl ? 200 : 204); //PARTIAL or OK or NO_CONTENT
        const char* scode = (rcode == 200) ? "OK" : ((rcode == 206) ? "PARTIAL" : "NO_CONTENT");
        sprintf(rxbuf,
                "HTTP/1.1 %u %s\r\n"
                "Server: jsrpc/1.0.0 (x)\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %llu\r\n"
                "Connection: %s\r\n"
                "\r\n"
                , rcode, scode, session.sContentType.size() ? session.sContentType.c_str() : "application/json", session.ttl, session.keepalive ? "keep-alive" : "close");
        
        session.sOut->assign(rxbuf);
        
        *session.sOut.get() += *response.get();
        
        log_debug("send response %s [session:{fd:%d,url:'%s'}]!", session.sOut->c_str(), conn.id(), session.sUrl.c_str());

        if (conn.send(session.sOut) < 0) {
            log_debug("clear bad session[session:{fd:%d,url:'%s'}]!", conn.id(), session.sUrl.c_str());
            conn.close();
        }
        session.reset();
    }
    
    bool processRecv(net::connection& conn, const net::address_t& from, const uint8_t* packet, uint32_t len, bool& trigger) {
        session_t& session = *std::dynamic_pointer_cast<session_t>(conn.getPatch(0));
        
        int rxttl = int(session.sHeaders->length()), rxttl_s = rxttl;
        
        log_debug("do receive [session:{fd:%d,url:'%s'}]!", conn.id(), session.sUrl.c_str());
        
        rxttl += len;
        if (rxttl > MAX_HEADERS_LEN) { //out of range
            log_debug("out of range [session:{fd:%d,url:'%s'}]!", conn.id(), session.sUrl.c_str());
            return false;
        }
        session.sHeaders->append((char*)packet, len);
        
        if (rxttl_s == rxttl) { //readable event but not data found?
            return false;
        }
        
        if (session.hddone == false) {
            int spo = session.hdpro - 4;
            if (spo < 0) {
                spo = 0;
            }
            const char* sb = session.sHeaders->c_str();
            const char* lnln = NULL;
            if ( (lnln = strstr(sb + spo, "\r\n\r\n")) != 0) { //
                const char* sln = NULL;
                if (strncmp(sb, "GET ", 4) == 0) {
                    sln = strchr(sb + 4, ' ');
                    if (sln == NULL) { //illegal http header
                        return false;
                    }
                    session.sUrl.assign(sb + 4, sln - sb - 4);
                }
                else if (strncmp(sb, "POST ", 5) == 0) {
                    sln = strchr(sb + 5, ' ');
                    if (sln == NULL) { //illegal http header
                        return false;
                    }
                    session.sUrl.assign(sb + 5, sln - sb - 5);
                }
                else if (strncmp(sb, "OPTIONS ", 8) == 0) {
                    sln = strchr(sb + 8, ' ');
                    if (sln == NULL) { //illegal http header
                        return false;
                    }
                    session.sUrl.assign(sb + 8, sln - sb - 8);
                }
                else {
                    log_debug("only 'GET'|'POST' method supprted!");
                    return false;
                }
                if (string::stristr(sln, "Connection: keep-alive")) {
                    session.keepalive = true;
                }
                else {
                    session.keepalive = false;
                }
                
                // Examples of byte-ranges-specifier values (assuming an entity-body of length 10000):
                //
                // - The first 500 bytes (byte offsets 0-499, inclusive):  bytes=0-499
                //
                // - The second 500 bytes (byte offsets 500-999, inclusive): bytes=500-999
                //
                // - The final 500 bytes (byte offsets 9500-9999, inclusive): bytes=-500
                //
                // - Or bytes=9500-
                
                //____________!!!!!!following format is unsupported!!!!!!!
                // - The first and last bytes only (bytes 0 and 9999):  bytes=0-0,-1
                //
                // - Several legal but not canonical specifications of the second 500 bytes (byte offsets 500-999, inclusive):
                // bytes=500-600,601-999
                // bytes=500-700,601-999
                //
                
                const char* srng = string::stristr(sb, "Range: bytes=");
                if (srng) { //in header
                    srng += 13;
                    if (*srng == '-') {
                        session.posb = - atoll(srng + 1);
                    }
                    else {
                        session.posb = atoll(srng);
                        while (isdigit(*srng)) {
                            srng++;
                        }
                        if (*srng == '-') {
                            session.pose = atoll(srng + 1);
                        }
                    }
                }
                else { //in url
                    srng = string::stristr(session.sUrl.c_str(), "range=");
                    if (srng) {
                        session.posb = - atoll(srng + 6);
                    }
                }
                
                session.hdpro  = int(lnln - sb + 4);
                session.hddone = true;
                const char* scl = string::stristr(sln, "Content-Length:");
                if (scl) {
                    const char* sclv = scl + strlen("Content-Length:");
                    if (*sclv == ' ')sclv++;
                    session.bdlen  = atoi(sclv);
                    if (session.bdlen > MAX_HEADERS_LEN) {
                        log_debug("out of range [session:{fd:%d,url:'%s'}]!", conn.id(), session.sUrl.c_str());
                        return false;
                    }
                    session.bdpro  = int(session.sHeaders->length() - session.hdpro);
                    if (session.bdpro == session.bdlen) {
                        session.bddone = true;
                        trigger = true;
                    }
                    else {
                        session.bddone = false;
                    }
                }
                else {
                    trigger = true;
                    session.bddone = true;
                    session.bdlen  = 0;
                    session.bdpro  = 0;
                    if (session.sHeaders->length() != session.hdpro) {
                        log_error("request is refused!");
                        return false;
                    }
                }
            }
            else {
                session.hdpro  = rxttl;
            }
        }
        
        if (session.hddone && session.bddone == false) {
            session.bdpro  = int(session.sHeaders->length() - session.hdpro);
            if (session.bdpro == session.bdlen) {
                session.bddone = true;
                trigger = true;
            }
            else {
                session.bddone = false;
            }
        }
        
        return true;
    }
    
    void server::expireSession(std::shared_ptr<net::connection> pconn, time_t expiredKey) {
        net::connection& conn = *pconn.get();
        session_t& session = *std::dynamic_pointer_cast<session_t>(conn.getPatch(0));
        if (session.tmActive == expiredKey) { //expired
            log_debug("session expired:{fd:%d,url:'%s'}!", conn.id(), session.sUrl.c_str());
            conn.close();
        }
        else if (_cxt->_timeout) { //try again
            ts::delay(_cxt->_timeout * 1000, this, &server::expireSession, pconn, session.tmActive);
        }
    }

    void server::onConnectionRecv(std::shared_ptr<net::connection>&& pconn, const net::address_t& from, std::shared_ptr<std::string>& packet) {
        net::connection& conn = *pconn.get();
        std::shared_ptr<session_t> pssn = std::dynamic_pointer_cast<session_t>(conn.getPatch(0));
        if (!pssn) {
            log_error("patch has not be set up!");
            return;
        }
        
        session_t& session = *pssn.get();
        
        session.tmActive    = time(nullptr);

        bool trigger = false;
        bool rt = processRecv(*pconn.get(), from, (const uint8_t*)packet->c_str(), (uint32_t)packet->length(), trigger);
        if (rt == false) {
            log_debug("clear bad session[session:{fd:%d,url:'%s'}]!", conn.id(), session.sUrl.c_str());
            conn.close();
        }
        else if ( trigger ) {
            log_debug("reuest is ready to execute [session:{fd:%d,url:'%s',content:'%s'}]!", conn.id(), session.sUrl.c_str(), session.sHeaders->c_str());
            
            if (session.bdlen) {
                session.sBody->assign(session.sHeaders->c_str() + session.hdpro, session.bdlen);
                session.sHeaders->resize(session.hdpro);
                
                const char* p = string::stristr(session.sHeaders->c_str(), "Content-Type:");
                if (p) {
                    p += 13;
                    while (*p ==' ') {
                        p++;
                    }
                    const char* ln = string::stristr(p, "\r\n");
                    if (ln) session.sContentType.assign(p, ln - p);
                    else session.sContentType = p;
                }
            }
            ts::slide(this, &server::onSessionRequest, pconn, session.sUrl, session.sHeaders, session.sContentType, session.sBody);
        }
    }
    
    void server::onConnectionClose(std::shared_ptr<net::connection>& pconn) {
        net::connection& conn = *pconn.get();
        session_t& session = *std::dynamic_pointer_cast<session_t>(conn.getPatch(0));
        log_debug("session:{fd:%d,url:'%s',content:'%s'} closed!", conn.id(), session.sUrl.c_str(), session.sHeaders->c_str());
    }
    
    void server::onConnectionComming(std::shared_ptr<net::connection>&& pconn) {
        std::shared_ptr<session_t> psnn(new session_t);
        session_t& session = *psnn.get();
        session.reset();
        session.keepalive   = false;
        session.tmActive    = time(nullptr);
        
        pconn->setPatch(0, psnn);
        
        if (_cxt->_timeout) {
            ts::delay(_cxt->_timeout * 1000, this, &server::expireSession, pconn, session.tmActive);
        }
    }
    
    //client
    //________________________
    inline int  read_until(SSL *ssl, int sk, const char* until, std::string& out, char* buf, int& rx, int bsize) {
        int     bxmv = 0;
        int     bxrx = 0;
        int     slen = (int)strlen(until);
        int     step = (slen > (bsize - 1)) ? (bsize - 1) : slen;
        char*   pos = strstr(buf, until);
        
        out.clear();
        while(pos == NULL) {
            if(rx >= slen) {
                bxmv = rx - slen + 1;    /*bytes to copy*/
                out.append((char*)buf, bxmv);
                memmove(buf, buf + bxmv, rx - bxmv);
                rx = slen - 1;
                buf[rx] = 0;
            }
            bxrx = (step > (bsize - rx - 1)) ? (bsize - rx - 1) : step;    /*bytes to recv*/
#if defined(USE_OPENSSL)
            if (ssl) {
                if( ( bxrx = (int)SSL_read(ssl, buf + rx, bxrx)) <= 0) {
                    return -1;
                }
            }
            else {
#endif
                if( ( bxrx = (int)recv(sk, buf + rx, bxrx, 0)) <= 0) {
                    return -1;
                }
#if defined(USE_OPENSSL)
            }
#endif
            rx += bxrx;
            buf[rx] = 0;
            pos = strstr(buf, until);
        }
        bxmv = int(pos - buf + slen);
        out.append((char*)buf, bxmv);
        memmove(buf, buf + bxmv, rx - bxmv);
        rx -= bxmv;
        buf[rx] = 0;
        
        return (int)out.size();
    }
    
    /*
     * return value: < 0 failed; >=0 socket id created
     */
    static int http_socket_rttm(const char* host/*[in]*/, unsigned short port/*[in]*/, int rtm = 0, int ttm = 0) {
        int optval = 0;
        int sk = -1;
        struct hostent* phost = NULL;
        struct sockaddr_in  ska;
        std::string shost;
        u_long  addr = 0;
        
        if(host == NULL) {
            log_error("[%d]Illegal parameters!", __LINE__);
            return -1;
        }
        if(port == 0) port = 80;
        
        phost = gethostbyname(host);
        if(phost == NULL) {
            log_error("name resolved failed!");
            return -1;
        }
        addr = *(unsigned long*)(phost->h_addr_list[0]);
        
#if defined(_OS_MAC_) //fix ssl session error under macos
        int fkmac = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
        if((sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
            log_error("Line %d: create tcp socket failed!", __LINE__);
#if defined(_OS_MAC_)
            if (fkmac != -1) close(fkmac);
#endif
            return -1;
        }
#if defined(_OS_MAC_)
        if (fkmac != -1) close(fkmac);
#endif
        
        optval = 1;
        setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval));
        
        if(rtm) {
            optval = rtm;
            setsockopt(sk, SOL_SOCKET, SO_SNDTIMEO, (char *)&optval, sizeof(optval));
        }
        if(ttm) {
            optval = ttm;
            setsockopt(sk, SOL_SOCKET, SO_RCVTIMEO, (char *)&optval, sizeof(optval));
        }
        
        int set = 1;
        setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (void *)&set, sizeof(int));
        
        memset((char *)&ska, 0, sizeof(struct sockaddr_in));
        ska.sin_family = AF_INET;
        ska.sin_addr.s_addr = (in_addr_t)addr;
        ska.sin_port = htons(port);
        
        if(connect(sk, (struct sockaddr *)&ska, sizeof(struct sockaddr_in)) < 0) {
            char errbuf[256];
            strcpy(errbuf, inet_ntoa(*(in_addr*)&(ska.sin_addr.s_addr)));
            log_error("Line %d: conntec to %s failed!", __LINE__, errbuf);
            goto FAILED;
        }
        
        return sk;
        
    FAILED:
        if(sk != -1) {
            close(sk);
        }
        
        return -1;
    }
    
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
#endif
    
    static int http_trans(bool isHttps, int& sk, const std::string& data, std::string& rsp, int& rcode/*[out]*/, int* phlen = nullptr, std::map<std::string, std::string>* rspCookie = nullptr) {
        int len = 0, xsnd = 0, xrcv = 0, xbytes = 0;
        int cxtlen = 0;
        int chunksz = 0;
        char    rcvbuf[2048];
        char*           eptr = NULL;
        const char*     pos = NULL;
        const char*     posb = NULL;
        const char*     pose = NULL;
        std::string     piece;
        std::string     trancode;
        std::string     encode;
        SSL *ssl = NULL;
#if defined(USE_OPENSSL)
        SSL_CTX *ctx = __ssl_cxt_holder.ctx;
        
        if (isHttps) {
            ssl = SSL_new (ctx);
            if (!ssl) {
                log_error("Error creating SSL.");
                goto failed;
            }
            SSL_set_fd(ssl, sk);
            int err = SSL_connect(ssl);
            if (err <= 0) {
                log_error("Error creating SSL connection.  err=%x", err);
                goto failed;
            }
            log_debug("SSL connection using %s\n", SSL_get_cipher (ssl));
        }
#endif
        
        len = (int)data.size();
        while (xsnd < len) {
            int xb = 0;
            int xbttl = ((len - xsnd) > 1024) ? 1024 : (len - xsnd);
            
#if defined(USE_OPENSSL)
            if (isHttps) {
                if((xb = SSL_write(ssl, data.c_str() + xsnd, xbttl)) <= 0) {
                    close(sk);
                    log_error("Line %d: call send failed, size to send = %d! err=%d", __LINE__, len, SSL_get_error(ssl, len));
                    goto failed;
                }
            }
            else {
#endif
                if((xb = (int)send(sk, data.c_str() + xsnd, xbttl, 0)) <= 0) {
                    close(sk);
                    log_error("Line %d: call send failed, size to send = %d!", __LINE__, len);
                    goto failed;
                }
#if defined(USE_OPENSSL)
            }
#endif
            
            xsnd += xb;
        }
        
        //recv
        xrcv = 0;
        while(xrcv < 12) {
#if defined(USE_OPENSSL)
            if (isHttps) {
                if( ( xbytes = (int)SSL_read(ssl, (char*)rcvbuf + xrcv, 12 - xrcv)) <= 0) {
                    close(sk);
                    log_error("Line %d: call recv failed!err=%d", __LINE__, SSL_get_error(ssl, len));
                    goto failed;
                }
            }
            else {
#endif
                if( ( xbytes = (int)recv(sk, (char*)rcvbuf + xrcv, 12 - xrcv, 0)) <= 0) {
                    log_error("Line %d: call recv failed! errno=%d", __LINE__, errno);
                    close(sk);
                    goto failed;
                }
#if defined(USE_OPENSSL)
            }
#endif
            xrcv += xbytes;
        }
        
        rcode = atoi(((char*)rcvbuf) + 9);
        
        /* check http version */
        if(strncmp(rcvbuf, "HTTP/1.", 7) != 0 || (rcvbuf[7] != '0' && rcvbuf[7] != '1' )) {
            goto failed;
        }
        
        if (rcode <= 100 || rcode >= 400) {
            return 0;
        }
        
        piece.clear();
        piece.append((char *)rcvbuf, xrcv);
        
        /*reveice headers*/
        xrcv = 0;
        memset(rcvbuf, 0, 1028);
        while(1) {
#if defined(USE_OPENSSL)
            if (isHttps) {
                if( ( xbytes = (int)SSL_read(ssl, (char*)rcvbuf + xrcv, 1024 - xrcv)) <= 0) {
                    close(sk);
                    log_error("Line %d: call recv failed!err=%d", __LINE__, SSL_get_error(ssl, len));
                    goto failed;
                }
            }
            else {
#endif
                if( ( xbytes = (int)recv(sk, (char*)rcvbuf + xrcv, 1024 - xrcv, 0)) <= 0) {
                    goto failed;
                }
#if defined(USE_OPENSSL)
            }
#endif
            xrcv += xbytes;
            rcvbuf[xrcv] = 0;
            pos = strstr(rcvbuf, "\r\n\r\n");
            if(pos == NULL) {
                pos = strrchr(rcvbuf, '\r');
                if(pos == NULL)
                {
                    piece.append((char *)rcvbuf, xrcv);
                    rcvbuf[1024] = 0;
                    xrcv = 0;
                    continue;
                }
                else if( ( xbytes = int(xrcv - (pos - rcvbuf) - 1)) >= 4) {
                    piece.append((char *)rcvbuf, xrcv);
                    rcvbuf[1024] = 0;
                    xrcv = 0;
                    continue;
                }
                else if(strncmp(pos, "\r\n\r\n", xbytes) != 0) {
                    piece.append((char *)rcvbuf, xrcv);
                    rcvbuf[1024] = 0;
                    xrcv = 0;
                    continue;
                }
                else {
                    if(rcvbuf != pos){piece.append((char *)rcvbuf, pos - rcvbuf); memmove(rcvbuf, pos, xbytes);}
                    xrcv = xbytes;
                }
            }
            else {
                piece.append((char *)rcvbuf, pos - rcvbuf + 4);
                break;
            }
        }
        
        cxtlen = -1;
        posb = (const char*)piece.c_str();
        /*parser headers*/
        while((pose = strstr(posb, "\r\n")) != NULL) {
            if(pose == posb){posb += 2; continue;}
            std::string sline(posb, pose - posb);
            const char* psp = strchr(sline.c_str(), ':');
            if(psp) {
                std::string hname = std::string(sline.c_str(), psp - sline.c_str());
                psp += 2;
                while(*psp == ' ')psp++;
                if(*psp == 0){posb += 2; continue;}
                
                if(hname == "Content-Length") {
                    cxtlen = atoi(psp);
                }
                else if(hname == "Transfer-Encoding") {
                    trancode = psp;
                }
                else if(hname == "Accept-Encoding") {
                    encode = psp;
                }
                else if(rspCookie && hname == "Set-Cookie") {
                    const char* eq = strchr(psp, '='), *cam = nullptr;
                    while (eq) {
                        std::string knm(psp, eq - psp);
                        cam = strchr(eq, ';');
                        if (cam) {
                            (*rspCookie)[knm] = eq + 1;
                            eq = nullptr;
                        }
                        else {
                            std::string val(eq + 1, cam - eq - 1);
                            (*rspCookie)[knm] = val;
                            psp = cam + 1;
                            while(*psp == ' ')psp++;
                            eq = strchr(psp, '=');
                        }
                    }
                }
            }
            posb = pose + 2;
        }
        
        if(phlen) phlen[0] = (int)piece.size();
        
        if(cxtlen > 0) {
            pos += 4;
            xrcv -= (pos - rcvbuf);
            rsp.append((char *)pos, xrcv);
#if defined(USE_OPENSSL)
            if (isHttps) {
                while(xrcv < cxtlen && (xbytes = (int)SSL_read(ssl, rcvbuf, ((cxtlen - xrcv) > 1024) ? 1024 : (cxtlen - xrcv))) > 0) {
                    rsp.append((char *)rcvbuf, xbytes);
                    xrcv += xbytes;
                }
            }
            else {
#endif
                while(xrcv < cxtlen && (xbytes = (int)recv(sk, rcvbuf, ((cxtlen - xrcv) > 1024) ? 1024 : (cxtlen - xrcv), 0)) > 0) {
                    rsp.append((char *)rcvbuf, xbytes);
                    xrcv += xbytes;
                }
#if defined(USE_OPENSSL)
            }
#endif
            if(xbytes <= 0) goto failed;
        }
        else if(trancode == "chunked") {
            pos += 4;
            xrcv -= (pos - rcvbuf);
            memmove(rcvbuf, pos, xrcv);
            rcvbuf[xrcv] = 0;
            xbytes = xrcv;
            xrcv = 0;
            while(1) {
                len = read_until(ssl, sk, "\r\n", piece, (char*)rcvbuf, xbytes, sizeof(rcvbuf));
                if(len <= 0) goto failed;
                pose = posb = (const char*)piece.c_str();
                while(*pose && (*pose != ';' && *pose != '\r'))pose++;
                if(*pose == 0) goto failed;
                chunksz = (int)strtol(posb, &eptr, 16);
                if(chunksz < 0) goto failed;
                else if(chunksz == 0) {
                    len = read_until(ssl, sk, "\r\n", piece, (char*)rcvbuf, xbytes, sizeof(rcvbuf));
                    if(len <= 0) goto failed;
                    break;
                }
                xrcv += chunksz;
                if(xbytes > chunksz) {
                    rsp.append((char *)rcvbuf, chunksz);
                    xbytes -= chunksz;
                    memmove(rcvbuf, (char*)rcvbuf + chunksz, xbytes);
                    rcvbuf[xbytes] = 0;
                }
                else {
                    rsp.append((char *)rcvbuf, xbytes);
                    chunksz -= xbytes;
                    len = 0;
#if defined(USE_OPENSSL)
                    if (isHttps) {
                        while(len < chunksz && (xbytes = (int)SSL_read(ssl, rcvbuf, ((chunksz - len) > 1024) ? 1024 : (chunksz - len))) > 0) {
                            rsp.append((char *)rcvbuf, xbytes);
                            len += xbytes;
                        }
                    }
                    else {
#endif
                        while(len < chunksz && (xbytes = (int)recv(sk, rcvbuf, ((chunksz - len) > 1024) ? 1024 : (chunksz - len), 0)) > 0) {
                            rsp.append((char *)rcvbuf, xbytes);
                            len += xbytes;
                        }
#if defined(USE_OPENSSL)
                    }
#endif
                    if(xbytes <= 0) goto failed;
                    xbytes = 0;
                    rcvbuf[0] = 0;
                }
                /*skip 2 bytes \r\n*/
                if(xbytes >= 2) {
                    xbytes -= 2;
                    if(xbytes)memmove(rcvbuf, (char*)rcvbuf + chunksz, xbytes);
                    rcvbuf[xbytes] = 0;
                }
                else {
                    if(xbytes)rsp.append((char *)rcvbuf, xbytes);
#if defined(USE_OPENSSL)
                    if (isHttps) {
                        if((xbytes = (int)SSL_read(ssl, rcvbuf, 2)) != 2) {
                            goto failed;
                        }
                    }
                    else {
#endif
                        if((xbytes = (int)recv(sk, rcvbuf, 2, 0)) != 2) {
                            goto failed;
                        }
#if defined(USE_OPENSSL)
                    }
#endif
                    xbytes = 0;
                    rcvbuf[0] = 0;
                }
            }
        }
        else if(cxtlen == -1) {
            pos += 4;
            xrcv -= (pos - rcvbuf);
            rsp.append((char *)pos, xrcv);
#if defined(USE_OPENSSL)
            if (isHttps) {
                while((xbytes = (int)SSL_read(ssl, rcvbuf, 1024)) > 0) {
                    rsp.append((char *)rcvbuf, xbytes);
                    xrcv += xbytes;
                }
            }
            else {
#endif
                while((xbytes = (int)recv(sk, rcvbuf, 1024, 0)) > 0) {
                    rsp.append((char *)rcvbuf, xbytes);
                    xrcv += xbytes;
                }
#if defined(USE_OPENSSL)
            }
#endif
        }
        else xrcv = 0;
        
#if defined(USE_OPENSSL)
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
#endif
        
        return xrcv;
        
    failed:
#if defined(USE_OPENSSL)
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
#endif
        return -1;
    }
    
    int request(const char* url, const char* method, std::string& rspBody, const char* contentType, const std::string* body, std::map<std::string, std::string>* cookie, std::map<std::string, std::string>* rspCookie) {
        int sk = -1;
        int xrcv = 0;
        int rcode = 0, phlen = 0;
        const char* p = url;
        const char* token = url;
        
        if(url == NULL) {
            log_error("[%d]Illegal parameters!", __LINE__);
            return -1;
        }
        
        while (*p && *p != ':') {
            p++;
        }
        std::string sProtocol(token, p - token);
        
        bool isHttps = strnicmp(sProtocol.c_str(), "https", 5) == 0;
        
#if !defined(USE_OPENSSL)
        if (isHttps) {
            log_error("[%d]unsupported protocol https!", __LINE__);
            return -1;
        }
#endif
        
        p += 3;
        token = p;
        
        while (*p && *p != ':' && *p != '/') {
            p++;
        }
        std::string sHost(token, p - token);
        uint16_t port = uint16_t(*p == ':' ? atoi(p + 1) : (isHttps?443:80));
        
        if (*p == ':') {
            while (*p && *p != '/') {
                p++;
            }
        }
        
        std::string sUri(p), reqcxt, sConLen, sConType, sCookie;
        if (body && body->size() && contentType) {
            ts::string::format(sConLen, "Content-Length:%d\r\n", body->size());
            ts::string::format(sConType, "Content-Type:%s\r\n", contentType);
        }
        if (cookie && cookie->size()) {
            sCookie = "Cookie:";
            for (auto& it : *cookie) {
                sCookie += it.first + "=" + it.second + "; ";
            }
            sCookie.replace(sCookie.length() - 2, 2, "\r\n");
        }
        
        ts::string::format(reqcxt, "%s %s HTTP/1.1\r\n"
                           "Accept:*/*\r\n"
                           "Accept-Encoding:deflate\r\n"
                           "Cache-Control:max-age=0\r\n"
                           "%s"
                           "%s"
                           "%s"
                           "%s"
                           "User-Agent:WBRU/1.0 (compatible; MSIE 6.0; Windows NT 5.1; Linux; Vxworks)\r\n"
                           "Host:%s\r\n"
                           "\r\n"
                           , method,
                           sUri.c_str(),
                           sConLen.length() ? sConLen.c_str() : "",
                           sConType.length() ? sConType.c_str() : "",
                           sCookie.length() ? sCookie.c_str() : "",
                           isHttps?"Upgrade-Insecure-Requests:1\r\n" : "",
                           sHost.c_str());
        
        if (body && body->size()) {
            reqcxt += *body;
        }
        
        sk = http_socket_rttm(sHost.c_str(), port);
        if(sk == -1) goto FAILED;
        
        xrcv = http_trans(isHttps, sk, reqcxt, rspBody, rcode, &phlen, rspCookie);
        
        if(sk != -1) {
            close(sk);
        }
        
        return xrcv;
        
    FAILED:
        if(sk != -1) {
            close(sk);
        }
        
        return -1;
    }

}};

_TS_NAMESPACE_END
