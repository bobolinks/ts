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
#if !defined(_TS_JSRPC_INC_)
#define _TS_JSRPC_INC_
#pragma once

#include <typeinfo>
#include <functional>
#include <initializer_list>
#include <ts/types.h>
#include <ts/json.h>
#include <net/http.h>

_TS_NAMESPACE_BEGIN

namespace net { namespace jsrpc {
    template <typename T>
    struct arg {
        using   _T    = T;
    };
    
#define __arg_attributes(name_, default_, desc_)    \
    constexpr static const char*    _nm(){return #name_;}\
    constexpr static const bool     _re = required_;\
    constexpr static const _T       _vd(){return default_;}\
    constexpr static const char*    _ds(){return desc_;}

    struct api_base_t {
    public:
        virtual ~api_base_t(void){}
        virtual void invoke(const ts::pie& value, ts::pie& out) = 0;
        virtual void serialize(ts::pie& value) = 0;
    };
    using apiTable = std::map<std::string, std::unique_ptr<api_base_t>>;
    
#define __api_attributes(name_, desc_)    \
constexpr static const char*    _nm(){return #name_;}\
constexpr static const char*    _ds(){return desc_;}

    template <typename VT, typename VS>
    inline void __vset(VT& v, const VS& s) {
        v = (VT)s;
    }
    
    template <>
    inline void __vset(const char*& v, const std::string& s) {
        v = s.c_str();
    }
    
    template <std::size_t I, class _Td, class... Types >
    typename std::tuple_element<I, std::tuple<Types...> >::type::_T&&
    __get(const ts::pie& value, _Td&& td, std::tuple<Types...>&& t ) {
        using _IT = typename std::tuple_element<I, std::tuple<Types...> >::type;
        using _DT = typename _IT::_T;
        using _RT = typename ts::___mapper_t<_DT>::type;
        static const char* name = _IT::_nm(); //skip "N4chip"
        auto& v = std::get<I>(std::forward<_Td>(td));
        if (value.isArray()) {
            const _RT& ref = value[I].get<_RT>();
            __vset(v, ref);
        }
        else {
            std::map<std::string,ts::pie>::const_iterator it = value.find(name);
            if (it == value.map().end()) {
                if (_IT::_re) {//is required but not assigned
                    throw std::invalid_argument(std::string(name) + " not dound!");
                }
            }
            else {
                const _RT& ref = it->second.get<_RT>();
                __vset(v, ref);
            }
        }
        return std::forward<_DT>(v);
    }
    
    template<typename O, typename F, typename _Td, typename _Ti, size_t ...S >
    void apply_invoke(O p, F&& fn, const ts::pie& value, ts::pie& rs, _Td&& td, _Ti&& ti, types::__index_t<S...>) {
        rs = (p->*fn)(__get<S>(value, td, std::forward<_Ti>(ti))...);
    }
    
    template <typename VT>
    inline std::string __tostr(const VT& s) {
        return std::to_string(s);
    }
    template <>
    inline std::string __tostr(const char *const& s) {
        return s;
    }
    template <>
    inline std::string __tostr(const std::string& s) {
        return s;
    }

    template <std::size_t I, class... Types >
    int __doc(ts::pie& value, std::tuple<Types...>&& t) {
        using _IT = typename std::tuple_element<I, std::tuple<Types...> >::type;
        using _DT = typename _IT::_T;
        ts::pie& arg = value[_IT::_nm()];
        arg = std::map<std::string, ts::pie>{{"type",typeid(_DT).name()}, {"desc",_IT::_ds()}, {"default", __tostr(_IT::_vd())}, {"required", _IT::_re ? "yes" : "no"}};
        return 0;
    }
    
    inline void __eat(...) {}
    template<typename _Ti, size_t ...S >
    void apply_document(ts::pie& value, _Ti&& ti, types::__index_t<S...>) {
        __eat(__doc<S>(value, std::forward<_Ti>(ti))...);
    }
    
    template <typename A, typename T, typename... Args>
    struct api : public api_base_t {
    public:
        typedef std::tuple<typename std::decay<Args>::type...> _Ti;
        typedef std::tuple<typename std::decay<Args>::type::_T...> _Td;
        typedef typename types::make_indices<sizeof...(Args)>::type __indices;
        typedef T* _Cp;
        typedef ts::pie (T::*_Fp)(typename Args::_T...);
    public:
        _Cp     __p_;
        _Fp     __f_;
    public:
        void invoke(const ts::pie& value, ts::pie& out) final {
            if (!value.isMap() && !value.isArray()) {
                throw std::invalid_argument("invalid argument format!");
            }
            if (value.isArray() && value.array().size() != sizeof...(Args)) {
                throw std::invalid_argument("argument count mismatched!");
            }
            apply_invoke(__p_, __f_, value, out, _Td(), _Ti(), __indices());
        }
        void serialize(ts::pie& value) final {
            value = std::map<std::string, ts::pie>{{"desc", A::_ds()}};
            ts::pie& args = value["args"]; args = std::map<std::string, ts::pie>{};
            apply_document(args, _Ti(), __indices());
        }
        static void bind(apiTable& table, _Cp __p, _Fp __f) {
            A* _api = new A;
            _api->__p_ = __p;
            _api->__f_ = __f;
            table[A::_nm()] = std::unique_ptr<api_base_t>(_api);
        }
    };
    
    struct server : public net::http::server {
    public:
        server(runnable const& host, apiTable& table, uint16_t concurrent = 128);
        ~server(void);
        
    private:
        void onSessionRequest(std::shared_ptr<net::connection> pconn, const std::string url, std::shared_ptr<std::string> headers, const std::string contentType, std::shared_ptr<std::string> body);

    private:
        apiTable&   _callTable;
    };
    
    struct invoker_exception : public std::exception {
        template <typename T>
        invoker_exception(const T v) : var(v) {};
        ts::pie var;
    };
};};

_TS_NAMESPACE_END

#endif /*_TS_JSRPC_INC_*/
