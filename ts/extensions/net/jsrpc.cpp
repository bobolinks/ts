#include <ts/json.h>
#include <ts/log.h>
#include <ts/string.h>
#include "jsrpc.h"

_TS_NAMESPACE_USING
_TS_NAMESPACE_BEGIN

#if FD_SETSIZE > 1024
#    define FD_SETMAX           1024
#else
#    define FD_SETMAX           FD_SETSIZE
#endif

#define SIZE_ALL_CONN           FD_SETMAX
#define SIZE_ACTIVE_CONN        (SIZE_ALL_CONN - 1)

#define ONE_SECOND              (1)
#define ONE_MINUTE              (60)

#define TIMEOUT_DELAY_CLOSE     (4 * ONE_SECOND)
#define TIMEOUT_KEEPALIVE       (2 * ONE_MINUTE)

namespace net { namespace jsrpc {
    server::server(runnable const& host, apiTable& table, uint16_t concurrent) : net::http::server(host, concurrent, TIMEOUT_KEEPALIVE), _callTable(table) {
    }

    server::~server(void) {
    }

    void server::onSessionRequest(std::shared_ptr<net::connection> pconn, const std::string url, std::shared_ptr<std::string> headers, const std::string contentType, std::shared_ptr<std::string> body) {
        net::connection& conn = *pconn.get();
        
        const char* q = strchr(url.c_str(), '?');
        std::string sUri(url.c_str(), q ? q - url.c_str() : url.size());
        apiTable::iterator itapi = _callTable.find(sUri.c_str() + 1); //skip 1 byte '/'
        if (itapi == _callTable.end()) { //not found
            log_debug("clear bad session[session:{fd:%d,url:'%s'}]!", conn.id(), url.c_str());
            conn.close();
            return;
        }

        int rsCode = 0;
        ts::pie rs;
        try {
            if (strncasecmp(contentType.c_str(), "application/json", contentType.size()) == 0) {
                ts::pie vars;
                if (json::parse(vars, body->c_str())) {
                    ts::pie* _args = nullptr;
                    std::map<std::string, ts::pie>::const_iterator it = vars.find("args");
                    if (it == vars.map().end()) {
                        _args = &vars;
                    }
                    else {
                        _args = const_cast<ts::pie*>(&it->second);
                    }
                    itapi->second->invoke(*_args, rs);
                }
                else {
                    rs = std::map<std::string, ts::pie>{{"code", rsCode = -1}, {"message", "invalid argument!"}};
                }
            }
            else if (contentType.size()){
                ts::pie vars(body->c_str());
                itapi->second->invoke(vars, rs);
            }
            else {
                rs = std::map<std::string, ts::pie>{{"code", rsCode = -1}, {"message", "unkonwn content type!"}};
            }
        }
        catch(std::exception& e) {
            rs = std::map<std::string, ts::pie>{{"code", rsCode = -1}, {"message", e.what()}};
        }
        
        std::shared_ptr<std::string> sResponse(new std::string());
        json::format(rs, *sResponse.get());
        
        commit(pconn, sResponse);
    }
};};

_TS_NAMESPACE_END

