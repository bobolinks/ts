#include <ts/tss.h>
#if defined(_OS_WIN_)
# include <winsock2.h>
# pragma comment(lib, "ws2_32.lib")
#else
# ifdef _OS_ANDROID_
# else
#  include <ifaddrs.h>
# endif
# include <sys/types.h>
# include <fcntl.h>
# include <errno.h>
# include <unistd.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <netinet/ip.h>
# include <netinet/tcp.h>
# include <netdb.h>
# include <sys/utsname.h>
# include <net/if.h>
# include <netinet/in.h>
#endif
#include <map>
#include <ts/string.h>
#include <ts/net.h>
#include <ts/log.h>

_TS_NAMESPACE_BEGIN

namespace net {
    static constexpr int invalid_sock = -1;

    //
    //______________________________________________________________________
    struct address_impl_t : public address_t {
        union {
            struct sockaddr     vx;
            struct sockaddr_in  v4;
            struct sockaddr_in6 v6;
        };
        
        explicit address_impl_t(void){}
        address_impl_t(const address_t& a) {
            *(address_t*)this = a;
        }
        
        bool localize(net::family_t fa = net::FAMILY_UNKNOWN) {
            address_impl_t& out = *this;
            struct ifconf conf;
            struct ifreq *ifr, ifrcopy;
            char buff[BUFSIZ] = { 0 };
            
            uint32_t  len = 0;
            bool    found = false;
            
            char *ptr = nullptr;
            
            conf.ifc_len = BUFSIZ;
            conf.ifc_buf = buff;
            
            int familys[2] = {AF_INET,  AF_INET6};
            
            for (int i = 0; found == false && i < 2; i++) {
                if (fa != net::FAMILY_UNKNOWN && familys[i] != fa) continue;
                
                int s = socket(familys[i], SOCK_DGRAM, 0);
                
                ioctl((int)s, SIOCGIFCONF, &conf);
                
                ifr = conf.ifc_req;
                for (ptr = buff; ptr < buff + conf.ifc_len; ) {
                    ifr = (struct ifreq *)ptr;
#ifdef _OS_ANDROID_
                    ptr += sizeof(struct ifreq);
#elif defined(_OS_LINUX_)
                    len = sizeof(ifreq);
                    ptr += sizeof(ifr->ifr_name) + len; // for next one in buffer
#else
#define max(a,b) ((a) > (b) ? (a) : (b))
                    len = max(sizeof(struct sockaddr), ifr->ifr_addr.sa_len);
                    ptr += sizeof(ifr->ifr_name) + len; // for next one in buffer
#endif
                    
                    if (ifr->ifr_addr.sa_family != familys[i]) {
                        continue; // ignore if not desired address family
                    }
                    
                    ifrcopy = *ifr;
                    ioctl((int)s, SIOCGIFFLAGS, &ifrcopy);
                    
                    if ( (ifrcopy.ifr_flags & IFF_UP) == 0 || (ifr->ifr_flags & IFF_LOOPBACK)) {
                        continue; // ignore if interface not up or loopback
                    }
                    
                    if (strstr("lo;vmnet;vnic;usb", ifr->ifr_name)) {
                        continue;
                    }
                    
                    found = true;
                    memcpy(&out.vx, &ifr->ifr_addr, ifr->ifr_addr.sa_len);
                }
                
                close(s);
            }
            
            if (found == false) {
                struct ifaddrs* ifap;
                struct ifaddrs* currentifap;
                int32_t result = 0;
                
                result = getifaddrs(&ifap);
                
                if (result == 0 && ifap) {
                    currentifap = ifap;
                    while( currentifap != nullptr ) {
                        if (currentifap->ifa_addr->sa_family == AF_INET || currentifap->ifa_addr->sa_family == AF_INET6) {
                            if (fa != net::FAMILY_UNKNOWN && currentifap->ifa_addr->sa_family != fa) continue;
                            if ( ! ((currentifap->ifa_flags & IFF_UP) == 0 || (currentifap->ifa_flags & IFF_LOOPBACK)) ) {
                                if (strstr("lo;vmnet;vnic;usb", currentifap->ifa_name)) {
                                    found = true;
                                    memcpy(&out.vx, &currentifap->ifa_addr, currentifap->ifa_addr->sa_len);
                                    break;
                                }
                            }
                        }
                        currentifap = currentifap->ifa_next;
                    }
                }
                if (ifap) freeifaddrs(ifap);
            }
            if (found) {
                char tmpBuf[64];
                this->family = (net::family_t)out.vx.sa_family;
                this->addr = inet_ntop(out.vx.sa_family, out.vx.sa_family == AF_INET ? ((void*)&out.v4.sin_addr) : ((void*)&out.v6.sin6_addr), tmpBuf, sizeof(tmpBuf));
            }
            
            log_debug("local address:%s", toString().c_str());

            return found;
        }
        
        bool standardize(void) {
            if (!family) {
                log_error("family has not been specified!");
                return false;
            }
            this->vx.sa_family = family;
            if (!proto) {
                log_error("proto has not been specified!");
                return false;
            }
            this->vx.sa_len = family == net::V6 ? sizeof(this->v6) : sizeof(this->v4);
            if (family == net::V4) {
                this->v4.sin_port = htons(this->port);
                inet_pton(AF_INET, addr.c_str(), &this->v4.sin_addr);
            }
            else {
                this->v6.sin6_port = htons(this->port);
                inet_pton(AF_INET6, addr.c_str(), &this->v6.sin6_addr);
            }
            return true;
        }
    };
    
    std::string address_t::toString(void) const {
        const char* name = proto == net::TCP ? "TCP" : (proto == net::UDP) ? "UDP" : "UNKNOWN";
        return ts::string::format("%s:%s:%d", name, addr.c_str(), port);
    }

    //
    //______________________________________________________________________
    struct connection_t : connection {
        connection_t(const runnable* s, int fd = -1) : connection(__local, __peer, fd), __s(s) {}
        const runnable* __s;
        address_impl_t  __local;
        address_impl_t  __peer;
        
        const int   send(std::shared_ptr<std::string> &packet) {
            if (__s->verify() == false) {
                return -1;
            }
            return connection::send(packet);
        }

    };
    typedef std::map<int, std::shared_ptr<connection_t>> mapConnection;
    
    //
    //______________________________________________________________________
    const int connection::send(std::shared_ptr<std::string> &packet) {
        std::shared_ptr<package_t> pa(new package_t());
        pa->data = packet;
        pa->consumed = 0;
        _size_queuing += pa->data->length();
        _packages.push(pa);
        return flush();
    }

    const size_t connection::recv(std::shared_ptr<std::string>& packet, size_t size) {
        return _streamer.get() ? _streamer->recv(*this, packet, size) : ::recv(_fd, const_cast<char*>(packet->c_str()), size, 0);
    }

    void    connection::close(void) {
        log_warning("connection[%d] closed!", _fd);
        ::close(_fd);
        _fd = net::invalid_sock;
    }

    const int connection::flush(void) {
        if (_packages.size() == 0) {
            return 0;
        }

        std::shared_ptr<package_t> &pa = _packages.front();
        int left = (int)pa->data->length() - pa->consumed;
        ssize_t tx = _streamer.get() ? _streamer->send(*this, (const uint8_t*)pa->data.get()->c_str() + pa->consumed, left) : ::send(_fd, pa->data.get()->c_str() + pa->consumed, left, 0);
        if (tx < 0) {
            return (int)tx;
        }
        else if (tx == 0) {//busy, try next time
            return _size_queuing;
        }
        pa->consumed += tx;
        _size_queuing -= tx;
        if (tx == left) {//done
            _packages.pop();
        }
        return _size_queuing;
    }

    //
    //______________________________________________________________________
    struct server_cxt {
        int             _sock;
        address_impl_t  _local;
        uint32_t        _concurrent;
        mapConnection   _connections;
    };
    
    server::server(runnable const& host, uint16_t concurrent) : parasite(host) {
        _cxt = new server_cxt();
        _cxt->_sock = net::invalid_sock;
        _cxt->_concurrent = concurrent + 1;
    }
    server::~server(void) {
        if (_cxt->_sock != invalid_sock) {
            runnable::cancelOwner(this);
            ::close(_cxt->_sock);
            _cxt->_sock = invalid_sock;
        }
        delete _cxt;
    }
    
    bool    server::bind(const address_t& local) {
        if (verify() == false) {
            log_error("failed to verify before calling %s!", __FUNCTION__);
            return false;
        }
        
        if (_cxt->_sock != invalid_sock) {
            log_error("illegal call!");
            return false;
        }
        
        if (local.isValid()) {
            *(address_t*)(&_cxt->_local) = local;
        }
        else {
            if (_cxt->_local.localize() == false) {
                log_error("not valid address!");
                return false;
            }
        }
        
        if (_cxt->_local.standardize() == false) {
            log_error("valid address!");
            return false;
        }
        
        _cxt->_sock = ::socket(_cxt->_local.family, _cxt->_local.proto == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, _cxt->_local.proto);
        if (_cxt->_sock == net::invalid_sock) {
            log_error("failed to create socket!err=%s", strerror(errno));
            return false;
        }
        
#ifdef __APPLE__
        int set = 1;
        setsockopt(_cxt->_sock, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
#endif
        
        if( _cxt->_local.proto != IPPROTO_TCP ) {
            int optval = 1;
            setsockopt(_cxt->_sock, SOL_SOCKET, SO_BROADCAST, (char *)&optval, sizeof(optval));
        }
        else {
            set = 1;
            setsockopt(_cxt->_sock, IPPROTO_TCP, TCP_NODELAY, (void *)&set, sizeof(int));
        }
        
        set = 1;
        setsockopt(_cxt->_sock, SOL_SOCKET, SO_REUSEADDR, (void *)&set, sizeof(int));

        if (::bind(_cxt->_sock, &_cxt->_local.vx, _cxt->_local.vx.sa_len) != 0) {
            log_error("failed to bind to %s, err=%s", _cxt->_local.toString().c_str(), strerror(errno));
            ::close(_cxt->_sock);
            _cxt->_sock = net::invalid_sock;
            return false;
        }
        if( _cxt->_local.proto == IPPROTO_TCP && ::listen(_cxt->_sock, _cxt->_concurrent) != 0) {
            log_error("failed to listen to %s, err=%s", _cxt->_local.toString().c_str(), strerror(errno));
            ::close(_cxt->_sock);
            _cxt->_sock = net::invalid_sock;
            return false;
        }

        {
            //set async mode
            bool setnonblock = false;
            
#ifdef _OS_WIN_
            u_long iMode = 1;
            setnonblock = (0 == ioctlsocket(_cxt->_sock, FIONBIO, &iMode));
#else
            int flags = fcntl(_cxt->_sock, F_GETFL, 0);
            setnonblock = ( -1 != fcntl(_cxt->_sock, F_SETFL, flags|O_NONBLOCK));
#endif
            if( !setnonblock )  {
                log_error("failed to bind to %s, err=%s", _cxt->_local.toString().c_str(), strerror(errno));
                ::close(_cxt->_sock);
                _cxt->_sock = invalid_sock;
                return false;
            }
        }
        
        log_notice("bind %s successfully! fd=%d", _cxt->_local.toString().c_str(), _cxt->_sock);

        runnable::addListener(this, _cxt->_sock, &_host);
        
        return true;
    }
    
    bool    server::close(void) {
        if (verify() == false) {
            log_error("failed to verify before calling %s!", __FUNCTION__);
            return false;
        }
        
        if (_cxt->_sock != invalid_sock) {
            runnable::cancelOwner(this, const_cast<runnable*>(&_host));
            ::close(_cxt->_sock);
            log_notice("close %s successfully! fd=%d", _cxt->_local.toString().c_str(), _cxt->_sock);
            _cxt->_sock = invalid_sock;
        }
        else {
            log_warning("%s aready closed!", _cxt->_local.toString().c_str());
        }
        return true;
    }

    //return true if successfully, TCP aways return false
    bool    server::sendto(const address_t& to, const uint8_t* data, uint32_t len) {
        if (verify() == false) {
            return false;
        }
        
        if (_cxt->_local.proto == net::TCP) {
            log_error("sendto is unsupported to TCP!");
            return false;
        }
        
        address_impl_t tar = to;
    
        if (tar.standardize() == false) {
            log_error("valid address!");
            return false;
        }

        return ::sendto(_cxt->_sock, data, len, 0,  &tar.vx, tar.vx.sa_len) == len;
    }
    
    void    server::onWritable(int fd) {
        if (fd == _cxt->_sock || _cxt->_local.proto == net::UDP) {
            log_error("unexcepted event received on connection[%d]!", fd);
            runnable::wantWritable(fd, false);
            return;
        }
        mapConnection::iterator it = _cxt->_connections.find(fd);
        if (it == _cxt->_connections.end()) {
            log_error("connection[%d] not found!", fd);
            runnable::removeListener(fd);
            return;
        }
        int tx = it->second->flush();
        if (tx == 0) {
            onConnectionSync(std::dynamic_pointer_cast<net::connection>(it->second));
        }
        else if (tx < 0) {//error occurs
            log_warning("connection[%d] closed!", fd);
            ::close(fd);
            std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(it->second);
            _cxt->_connections.erase(it);
            onConnectionClose(cnn);
        }
    }
    
    void    server::onRecv(int fd, size_t size) {
        if (fd == _cxt->_sock && _cxt->_local.proto == net::TCP && size) {
            log_error("unexcepted event received on connection[%d]!", fd);
            return;
        }
        
        if (_cxt->_local.proto == net::TCP && fd == _cxt->_sock) { //new connection is coming
            address_impl_t from;
            socklen_t len = sizeof(from);
            int fdnew = accept(_cxt->_sock, &from.vx, &len);
            if (fdnew >= 0) {
                if (_cxt->_connections.size() < _cxt->_concurrent) {
                    std::shared_ptr<connection_t> con = std::shared_ptr<connection_t>(new connection_t(&this->_host, fdnew));
                    con->__local = _cxt->_local;
                    con->__peer = from;
                    con->__local.standardize();
                    _cxt->_connections[fdnew] = con;
                    
                    runnable::addListener(this, fdnew);
                    onConnectionComming(con);
                }
                else {
                    ::close(fdnew);
                    from.standardize();
                    log_debug("new connection[%s,fd:%d] has been refused!", from.toString().c_str(), fdnew);
                }
            }
            else {
                log_notice("failed to accept new connection! error=%s", strerror(errno));
                return;
            }
        }
        else {
            mapConnection::iterator it = _cxt->_connections.find(fd);
            if (it == _cxt->_connections.end()) {
                log_error("connection[%d] not found!", fd);
                runnable::removeListener(fd);
                return;
            }

            std::shared_ptr<std::string> packet = onPacketAlloc(size);
            packet->resize(size);
            if (_cxt->_local.proto == net::TCP) {
                size_t sz = it->second->recv(packet, size);
                if (sz > 0) {
                    if (size != sz) packet->resize(sz);
                    onConnectionRecv(std::dynamic_pointer_cast<net::connection>(it->second), it->second->__peer, packet);
                }
                else {
                    log_warning("connection[%d] error!", fd);
                    ::close(fd);
                    std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(it->second);
                    _cxt->_connections.erase(it);
                    onConnectionClose(cnn);
                }
            }
            else {
                address_impl_t from;
                socklen_t len = sizeof(from);
                ssize_t sz = ::recvfrom(fd, const_cast<char*>(packet->c_str()), size, 0, &from.vx, &len);
                if (sz > 0) {
                    if (size != sz) packet->resize(sz);
                    onConnectionRecv(std::dynamic_pointer_cast<net::connection>(it->second), it->second->__peer, packet);
                }
                else {
                    log_warning("connection[%d] error!", fd);
                    ::close(fd);
                    std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(it->second);
                    _cxt->_connections.erase(it);
                    onConnectionClose(cnn);
                }
            }
        }
    }
    
    void    server::onClose(int fd) {
        mapConnection::iterator it = _cxt->_connections.find(fd);
        if (it == _cxt->_connections.end()) {//may be close itself
            return;
        }
        
        log_warning("connection[%d] closed!", fd);
        std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(it->second);
        _cxt->_connections.erase(it);
        onConnectionClose(cnn);
    }
    
    std::shared_ptr<std::string> server::onPacketAlloc(size_t size) {
        return std::shared_ptr<std::string>(new std::string);
    }
    
    
    //TCP only connector
    //______________________________________________________________________

    struct connector_cxt {
        int             _sock;
        address_impl_t  _peer;
        bool            _connected;
        std::shared_ptr<connection_t>   _connection;
    };
    
    connector::connector(runnable const& host) : parasite(host) {
        _cxt = new connector_cxt();
        _cxt->_sock = net::invalid_sock;
        _cxt->_connected = false;
        _cxt->_connection = std::shared_ptr<connection_t>(new connection_t(&this->_host));
    }
    connector::~connector(void) {
        if (_cxt->_sock != invalid_sock) {
            runnable::cancelOwner(this);
            ::close(_cxt->_sock);
            _cxt->_sock = invalid_sock;
        }
        delete _cxt;
    }
    
    bool    connector::connect(const address_t& target, uint64_t timeout, bool nonblock) {
        if (verify() == false) {
            log_error("failed to verify before calling %s!", __FUNCTION__);
            return false;
        }
        
        if (_cxt->_sock != invalid_sock) {
            log_error("illegal call!");
            return false;
        }
        else if (target.proto != net::TCP) {
            log_error("only for tcp!");
            return false;
        }
        
        if (target.isValid() == false) {
            log_error("not valid address!");
            return false;
        }

        *(address_t*)(&_cxt->_peer) = target;

        if (_cxt->_peer.standardize() == false) {
            log_error("valid address!");
            return false;
        }
        
        _cxt->_sock = ::socket(_cxt->_peer.family, SOCK_STREAM, IPPROTO_TCP);
        if (_cxt->_sock == net::invalid_sock) {
            log_error("failed to create socket!err=%s", strerror(errno));
            return false;
        }
        
#ifdef __APPLE__
        int set = 1;
        setsockopt(_cxt->_sock, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));
#endif
        
        set = 1;
        setsockopt(_cxt->_sock, IPPROTO_TCP, TCP_NODELAY, (void *)&set, sizeof(int));

        if (nonblock) {
            //set async mode
            bool setnonblock = false;
            
#ifdef _OS_WIN_
            u_long iMode = 1;
            setnonblock = (0 == ioctlsocket(_cxt->_sock, FIONBIO, &iMode));
#else
            int flags = fcntl(_cxt->_sock, F_GETFL, 0);
            setnonblock = ( -1 != fcntl(_cxt->_sock, F_SETFL, flags|O_NONBLOCK));
#endif
            if( !setnonblock )  {
                log_error("failed to connect to %s, err=%s", _cxt->_peer.toString().c_str(), strerror(errno));
                ::close(_cxt->_sock);
                _cxt->_sock = invalid_sock;
                return false;
            }
        }

        log_notice("connecting to %s:%d! fd=%d", _cxt->_peer.toString().c_str(), _cxt->_peer.port, _cxt->_sock);

        if (::connect(_cxt->_sock, &_cxt->_peer.vx, _cxt->_peer.vx.sa_len) != 0) {
            int er = errno;
            if (er != EINPROGRESS) {
                log_error("failed to connect to %s, err=%d:%s", _cxt->_peer.toString().c_str(), er, strerror(er));
                ::close(_cxt->_sock);
                _cxt->_sock = net::invalid_sock;
                return false;
            }
        }
        
        _cxt->_connection->_fd = _cxt->_sock;
        
        if (nonblock) {
            log_notice("connecting %s! fd=%d", _cxt->_peer.toString().c_str(), _cxt->_sock);
        }
        else {
            _cxt->_connected = true;
        }
        
        runnable::addListener(this, _cxt->_sock, &_host);
        
        if (timeout) {
            ts::delay(timeout, this, &connector::stateConnection, _cxt->_sock);
        }

        return true;
    }
    
    bool    connector::nonblock(bool enable) {
        bool setnonblock = false;
        
#ifdef _OS_WIN_
        u_long iMode = enable;
        setnonblock = (0 == ioctlsocket(_cxt->_sock, FIONBIO, &iMode));
#else
        int flags = fcntl(_cxt->_sock, F_GETFL, 0);
        if (enable) flags |= O_NONBLOCK;
        else flags &= ~O_NONBLOCK;
        setnonblock = ( -1 != fcntl(_cxt->_sock, F_SETFL, flags));
#endif
        if( !setnonblock )  {
            log_error("failed to connect to %s, err=%s", _cxt->_peer.toString().c_str(), strerror(errno));
            ::close(_cxt->_sock);
            _cxt->_sock = invalid_sock;
            return false;
        }
        return true;
    }

    bool    connector::close(void) {
        if (verify() == false) {
            log_error("failed to verify before calling %s!", __FUNCTION__);
            return false;
        }
        
        if (_cxt->_sock != invalid_sock) {
            runnable::cancelOwner(this, const_cast<runnable*>(&_host));
            ::close(_cxt->_sock);
            log_notice("close %s successfully! fd=%d", _cxt->_peer.toString().c_str(), _cxt->_sock);
            _cxt->_sock = invalid_sock;
            _cxt->_connected = false;
        }
        else {
            log_warning("%s aready closed!", _cxt->_peer.toString().c_str());
        }
        return true;
    }
    
    //return _size_queuing, return -1 if an error occurs
    int     connector::send(std::shared_ptr<std::string>& packet) {
        if (verify() == false) {
            log_error("failed to verify before calling %s!", __FUNCTION__);
            return -1;
        }
        return _cxt->_connection->send(packet);
    }

    std::shared_ptr<connection> connector::get(void) {
        return _cxt->_connection;
    }

    void    connector::onWritable(int fd) {
        if (fd != _cxt->_sock) {
            log_error("unexcepted event received on connection[%d]!", fd);
            runnable::wantWritable(fd, false);
            return;
        }
        
        int tx = _cxt->_connection->flush();
        if (tx == 0) {
            onConnectionSync(std::dynamic_pointer_cast<net::connection>(_cxt->_connection));
        }
        else if (tx < 0) {//error occurs
            log_warning("connection[%d] closed!", fd);
            ::close(fd);
            _cxt->_connected = false;
            std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(_cxt->_connection);
            onConnectionClose(cnn);
        }
    }
    
    void    connector::stateConnection(int fd) {
        if (fd != _cxt->_sock) {
            log_error("unexcepted event received on connection[%d]!", fd);
            return;
        }

        if (_cxt->_connected) {
            return;
        }
        else {
            log_warning("connection[%d] timeout!", fd);
            ::close(fd);
            std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(_cxt->_connection);
            onConnectionClose(cnn);
        }
    }
    
    void    connector::onRecv(int fd, size_t size) {
        if (fd != _cxt->_sock) {
            log_error("unexcepted event received on connection[%d]!", fd);
            return;
        }
        
        std::shared_ptr<connection> con = std::dynamic_pointer_cast<net::connection>(_cxt->_connection);

        if (_cxt->_connected == false) {
            _cxt->_connected = true;
            onConnectionConnected(con);
        }
        {
            std::shared_ptr<std::string> packet = onPacketAlloc(size);
            packet->resize(size);
            ssize_t sz = _cxt->_connection->recv(packet, size);
            if (sz > 0) {
                if (size != sz) packet->resize(sz);
                onConnectionRecv(con, _cxt->_peer, packet);
            }
            else {
                log_warning("connection[%d] error!", fd);
                ::close(fd);
                _cxt->_connected = false;
                std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(_cxt->_connection);
                onConnectionClose(cnn);
            }
        }
    }
    
    void    connector::onClose(int fd) {
        if (fd != _cxt->_sock) {
            log_error("unexcepted event received on connection[%d]!", fd);
            return;
        }

        log_warning("connection[%d] closed!", fd);
        _cxt->_connected = false;
        std::shared_ptr<connection> cnn = std::dynamic_pointer_cast<net::connection>(_cxt->_connection);
        onConnectionClose(cnn);
    }
    
    std::shared_ptr<std::string> connector::onPacketAlloc(size_t size) {
        return std::shared_ptr<std::string>(new std::string);
    }
    
};

_TS_NAMESPACE_END
