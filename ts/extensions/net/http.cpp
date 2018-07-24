#include <unistd.h>
#include <string>
#include <mutex>
#include <list>
#include <algorithm>
#include <ts/log.h>
#include <ts/string.h>
#include "http.h"

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
}};

_TS_NAMESPACE_END
