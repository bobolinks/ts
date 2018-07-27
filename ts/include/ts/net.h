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
#if !defined(_TS_NET_INC_)
#define _TS_NET_INC_
#pragma once

#include <ts/tss.h>
#include <ts/asyn.h>
#include <queue>
#include <string>

_TS_NAMESPACE_BEGIN

namespace net {
    typedef enum {PROTO_UNKNOWN = 0, TCP = 6 /*IPPROTO_TCP*/, UDP = 17 /*IPPROTO_UDP*/} proto_t;
    typedef enum {FAMILY_UNKNOWN = 0, V4 = 2 /*AF_INET*/, V6 = 30 /*AF_INET6*/} family_t;

    struct address_t {
        family_t    family;
        proto_t     proto;
        uint16_t    port;
        std::string addr;
        
        address_t(const char* ad = nullptr, uint16_t pt = 0, net::proto_t pro = net::TCP, net::family_t fa = net::V4) : family(fa), proto(pro), port(pt), addr(ad ? ad : "") {}
        
        inline bool isValid(void) const {
            return (port && addr.size()) ? true : false;
        }
        
        inline bool operator == (const net::address_t& b) const {
            return family == b.family && proto == b.proto && port == b.port && addr == b.addr;
        }
        
        inline bool operator < (const net::address_t& b) const {
            if (family < b.family) return true; if (family > b.family) return false;
            if (proto < b.proto) return true; if (proto > b.proto) return false;
            if (port < b.port) return true; if (port > b.port) return false;
            return addr < b.addr;
        }
        
        std::string toString(void) const;
    };
    
    //
    //______________________________________________________________________
    struct package_t {
        std::shared_ptr<std::string> data;
        int consumed;
    };
    
    //
    //______________________________________________________________________
    struct connection_patch {
        virtual ~connection_patch(void){}
    };
    struct connection;
    struct connection_io {
        virtual ~connection_io(void){}
        virtual size_t send(connection& conn, const uint8_t* data, size_t size) = 0;
        virtual size_t recv(connection& conn, std::shared_ptr<std::string>& packet, size_t size) = 0;
    };
    struct connection __attr_threading("unsafe") {
        constexpr static const int MaxPatchSize = 6;

        connection(address_t& l, address_t& r, int f) : _local(l), _peer(r), _fd(f) {_size_queuing = 0;};
        virtual ~connection(void) {}
        
        //return _size_queuing, return -1 if an error occurs
        virtual const int   send(std::shared_ptr<std::string>& packet);
        void                close(void);
        
        const address_t&    local(void) const {return _local;}
        const address_t&    peer(void) const {return _local;}
        const int           id(void) const {return _fd;}
        
        void                setStreamer(std::shared_ptr<connection_io> st) {_streamer = st;}
        std::shared_ptr<connection_io>&  getStreamer(void) {return _streamer;}

        void                setPatch(int index, std::shared_ptr<connection_patch> pa) {_patchs[index] = pa;}
        std::shared_ptr<connection_patch>&  getPatch(int index) {return _patchs[index];}

    protected:
        friend struct server;
        friend struct connector;

        //send more data in queue,
        //return _size_queuing, return -1 if an error occurs
        const int           flush(void);
        
        const size_t        recv(std::shared_ptr<std::string>& packet, size_t size);
        
    protected:
        std::shared_ptr<connection_io>      _streamer;
        std::shared_ptr<connection_patch>   _patchs[MaxPatchSize];
        std::queue<std::shared_ptr<package_t>>  _packages;
        int         _size_queuing; /*in byte*/
        int         _fd;
        address_t&  _local;
        address_t&  _peer;
    };
    
    //
    //______________________________________________________________________
    struct server : public runnable::listener, parasite {
        explicit server(runnable const& host, uint16_t concurrent = 128);
        ~server(void);
        
        bool    bind(const address_t& local);
        bool    close(void);
        int     id(void) const;
        
        //return true if successfully, TCP aways return false
        bool    sendto(const address_t& to, const uint8_t* data, uint32_t len);
        
        const address_t&    local(void) const;
        
    private:
        //runnable::listener
        void    onRecv(int fd, size_t size) final;
        void    onClose(int fd) final;
        void    onWritable(int fd) final;
        
        //memory funcational
        virtual std::shared_ptr<std::string> onPacketAlloc(size_t size);
        
        //socket functional
        virtual void    onConnectionRecv(std::shared_ptr<connection>&& conn, const address_t& from, std::shared_ptr<std::string>& packet) = 0;
        virtual void    onConnectionClose(std::shared_ptr<connection>& conn) = 0;
        virtual void    onConnectionComming(std::shared_ptr<connection>&& conn) = 0;
        virtual bool    onConnectionSync(std::shared_ptr<connection>&& conn) {return false;} //is writable, return false is no more data to send

    protected:
        struct server_cxt*  _cxt;
    };
    
    //TCP only connector
    //______________________________________________________________________
    struct connector : public runnable::listener, parasite {
        explicit connector(runnable const& host);
        ~connector(void);
        
        bool    connect(const address_t& target, uint64_t timeout = 0 /*in seconds*/, bool nonblock = true);
        bool    close(void);

        int     send(std::shared_ptr<std::string>& packet);
        
        std::shared_ptr<connection> get(void);
        
        bool    nonblock(bool enable);

    private:
        //runnable::listener
        void    onRecv(int fd, size_t size) final;
        void    onClose(int fd) final;
        void    onWritable(int fd) final;
        
        void    stateConnection(int fd);
        
        //memory funcational
        virtual std::shared_ptr<std::string> onPacketAlloc(size_t size);
        
        //socket functional
        virtual void    onConnectionRecv(std::shared_ptr<connection>& conn, const address_t& from, std::shared_ptr<std::string>& packet) = 0;
        virtual void    onConnectionClose(std::shared_ptr<connection>& conn) = 0;
        virtual void    onConnectionConnected(std::shared_ptr<connection>& conn) = 0;
        virtual bool    onConnectionSync(std::shared_ptr<connection>&& conn) {return false;} //is writable, return false is no more data to send
        
    protected:
        struct connector_cxt*  _cxt;
    };
}

_TS_NAMESPACE_END

#endif /*_TS_NET_INC_*/
