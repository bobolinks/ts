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
#if !defined(_TS_PIE_INC_)
#define _TS_PIE_INC_
#pragma once

#include <typeindex>
#include <type_traits>
#include <tuple>
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <ts/types.h>

_TS_NAMESPACE_BEGIN

template<typename... Args>
struct ___pie_helper;
template<typename T, typename... Args>
struct ___pie_helper<T, Args...> {
    inline static void destroy(std::type_index id, void * data) {
        if (id == std::type_index(typeid(T)))
            reinterpret_cast<T*>(data)->~T();
        else
            ___pie_helper<Args...>::destroy(id, data);
    }
    
    inline static void move(std::type_index old_t, void * old_v, void * new_v) {
        if (old_t == std::type_index(typeid(T)))
            new (new_v) T(std::move(*reinterpret_cast<T*>(old_v)));
        else
            ___pie_helper<Args...>::move(old_t, old_v, new_v);
    }
    
    inline static void copy(std::type_index old_t, const void * old_v, void * new_v) {
        if (old_t == std::type_index(typeid(T)))
            new (new_v) T(*reinterpret_cast<const T*>(old_v));
        else
            ___pie_helper<Args...>::copy(old_t, old_v, new_v);
    }
};
template<> struct ___pie_helper<> {
    inline static void destroy(std::type_index id, void * data) {  }
    inline static void move(std::type_index old_t, void * old_v, void * new_v) { }
    inline static void copy(std::type_index old_t, const void * old_v, void * new_v) { }
};

struct pie ;
template < typename T >
struct ___mapper_t {typedef int64_t type;};
template < > struct ___mapper_t <float> {typedef double type;};
template < > struct ___mapper_t <double> {typedef double type;};
template < > struct ___mapper_t <long double> {typedef double type;};
template < > struct ___mapper_t <std::string> {typedef std::string type;};
template < > struct ___mapper_t <const char*> {typedef std::string type;};
template < > struct ___mapper_t <std::vector<pie>> {typedef std::vector<pie> type;};
template < > struct ___mapper_t <std::map<std::string,pie>> {typedef std::map<std::string,pie> type;};

struct pie final {
    template < typename T, typename... List >
    struct ___contains_t : std::true_type {};
    
    template < typename T, typename Head, typename... Rest >
    struct ___contains_t<T, Head, Rest...>
    : std::conditional< std::is_same<T, Head>::value, std::true_type, ___contains_t<T,Rest... >> ::type{};
    
    template < typename T >
    struct ___contains_t<T> : std::false_type{};

    template <typename... Args>
    struct ___data_t {
        static constexpr int __size = types::static_max<sizeof(Args)...>::value;
        static constexpr int __align= types::static_max<alignof(Args)...>::value;
        using type = typename std::aligned_storage<__size, __align>::type;
        typedef std::tuple<typename std::decay<Args>::type...> _Td;
        typedef ___pie_helper<Args...> helper_t;
        template <typename T>
        struct contains {
            static constexpr bool value = std::is_integral<T>{} || std::is_floating_point<T>{} || ___contains_t<typename std::remove_reference<T>::type, Args...>::value;
        };
    };
    typedef ___data_t<int64_t, double, std::string, std::vector<pie>, std::map<std::string,pie>> data_t;
    typedef data_t::helper_t   helper_t;
    
    ~pie(void) {
        helper_t::destroy(_type, &_data);
    }
    
    explicit pie(void) : _type(typeid(void)) { }
    
    explicit pie(pie&& old) : _type(old._type) {
        helper_t::move(old._type, &old._data, &_data);
        _flags = old._flags;
    }
    
    explicit pie(pie const& old) : _type(old._type)  {
        helper_t::copy(old._type, &old._data, &_data);
        _flags = old._flags;
    }
    
    template <class T, typename U = typename ___mapper_t<T>::type, class = typename std::enable_if<data_t::contains<T>::value>::type>
    pie(T&& value) : _type(typeid(U)) {
        new(&_data) U(std::forward<T>(value));
        _flags = 0;
    }
    
    pie(int value) : _type(typeid(int64_t)) {
        new(&_data) int64_t(value);
        _flags = 0;
    }
    
    pie(const char* value) : _type(typeid(std::string)) {
        new(&_data) std::string(value);
        _flags = 0;
    }

    pie& operator = (pie&& old) {
        helper_t::destroy(_type, &_data);
        helper_t::move(old._type, &old._data, &_data);
        _type = old._type;
        _flags = old._flags;
        return *this;
    }
    
    pie& operator = (pie const& old) {
        helper_t::destroy(_type, &_data);
        helper_t::copy(old._type, &old._data, &_data);
        _type = old._type;
        _flags = old._flags;
        return *this;
    }
    
    pie& move(pie& to) {
        helper_t::destroy(to._type, &to._data);
        to._type = _type;
        memcpy(&to._data, &_data, sizeof(_data));
        to._flags = _flags;
        new(&_data) int64_t(0);
        _type = std::type_index(typeid(int64_t));
        _flags = 0;
        return *this;
    }
    
    template <class T, typename U = typename ___mapper_t<typename std::remove_reference<T>::type>::type, class = typename std::enable_if<data_t::contains<T>::value>::type>
    pie& operator = (T&& value) {
        helper_t::destroy(_type, &_data);
        new (&_data) U(value);
        _type = std::type_index(typeid(U));
        _flags = 0;
        return *this;
    }
    
    pie& operator = (const int value) {
        helper_t::destroy(_type, &_data);
        new (&_data) int64_t(value);
        _type = std::type_index(typeid(int64_t));
        _flags = 0;
        return *this;
    }

    pie& operator = (const char* value) {
        helper_t::destroy(_type, &_data);
        new (&_data) std::string(value);
        _type = std::type_index(typeid(std::string));
        _flags = 0;
        return *this;
    }

    template<typename T, class = typename std::enable_if<data_t::contains<T>::value>::type>
    inline bool is(void) const {
        return (_type == std::type_index(typeid(T)));
    }
    
    inline bool isNumber(void) const {
        return (_type == std::type_index(typeid(int64_t))) || (_type == std::type_index(typeid(double)));
    }
    
    inline bool isString(void) const {
        return (_type == std::type_index(typeid(std::string)));
    }

    inline bool isMap(void) const {
        return (_type == std::type_index(typeid(std::map<std::string,pie>)));
    }
    
    inline bool isArray(void) const {
        return (_type == std::type_index(typeid(std::vector<pie>)));
    }

    inline bool empty(void) const {
        return _type == std::type_index(typeid(void));
    }
    
    inline std::type_index type(void) const {
        return _type;
    }
    
    template<typename T, typename U = typename ___mapper_t<T>::type, class = typename std::enable_if<data_t::contains<T>::value>::type>
    typename std::decay<U>::type& get(void) {
        if (!is<U>()) {
            throw std::bad_cast();
        }
        return *(U*) (&_data);
    }
    
    template<typename T, typename U = typename ___mapper_t<T>::type, class = typename std::enable_if<data_t::contains<T>::value>::type>
    typename std::decay<U>::type const& get(void) const {
        if (!is<U>()) {
            static constexpr bool isn = std::is_integral<T>{} || std::is_floating_point<T>{};
            throw std::bad_cast();
        }
        return *(const U*) (&_data);
    }

    inline std::map<std::string,pie>& map(void) {
        return get<std::map<std::string,pie>>();
    }

    inline std::map<std::string,pie> const& map(void) const {
        return get<std::map<std::string,pie>>();
    }

    inline std::vector<pie>& array(void) {
        return get<std::vector<pie>>();
    }
    
    inline std::vector<pie> const& array(void) const {
        return get<std::vector<pie>>();
    }
    
    inline pie& operator[](int i) {
        return array()[i];
    }

    inline const pie& operator[](int i) const {
        return array()[i];
    }

    inline pie& operator[](const char* key) {
        return map()[key];
    }
    
    inline pie& got(const char* path) {
        if ( (*path == '[' && !isArray()) || (*path != '[' &&!isMap())) {
            throw std::bad_cast();
        }
        const char* p = path;
        const char* dot = strchr(p, '.');
        ts::pie* pnode = nullptr;
        if (dot) {
            const char* pp = dot + 1;
            if (*p == '[') { //array
                if ( *(p + 1) == '+') { // add new
                    array().push_back(0);
                    pnode = &*array().rbegin();
                    if (*pp == '[') {
                        *pnode = std::vector<pie>{};
                    }
                    else {
                        *pnode = std::map<std::string, pie>{};
                    }
                }
                else {
                    int index = atoi(p + 1);
                    if (index < 0 || index >= array().size()) {
                        throw std::out_of_range("");
                    }
                    pnode = &array()[index];
                }
            }
            else { //map
                std::string name(p, dot - p);
                auto it = map().find(name);
                if (it == map().end()) {
                    pnode = &map()[name];
                    if (*pp == '[') {
                        *pnode = std::vector<pie>{};
                    }
                    else {
                        *pnode = std::map<std::string, pie>{};
                    }
                }
                else pnode = &it->second;
            }
            return pnode->got(pp);
        }
        else {
            if (*p == '[') { //array
                if ( *(p + 1) == '+') { // add new
                    array().push_back(0);
                    pnode = &*array().rbegin();
                }
                else {
                    int index = atoi(p + 1);
                    if (index < 0 || index >= array().size()) {
                        throw std::out_of_range("");
                    }
                    pnode = &array()[index];
                }
            }
            else { //map
                return map()[p];
            }
        }
    }
    
    inline std::map<std::string,pie>::const_iterator find(const char* key) const {
        return map().find(key);
    }
    
    inline bool has(const char* key) const {
        return isMap() && map().find(key) != map().end();
    }

    bool each(std::function<bool(pie& parent, const char* name, pie& item, int level)> func, int level = 0){
        if (isMap()) {
            for (auto& it : map()) {
                if (func(*this, it.first.c_str(), it.second, level) == false) {
                    return false;
                }
                if (it.second.each(func, level + 1) == false) {
                    return false;
                }
            }
        }
        else if (isArray()) {
            const char* name_base = nullptr;
            for (auto& it : array()) {
                if (func(*this, name_base + 1, it, level) == false) {
                    return false;
                }
                if (it.each(func, level + 1) == false) {
                    return false;
                }
            }
        }
        return true;
    }
    
    bool each(std::function<bool(pie& parent, const char* path, const char* name, pie& item, int level)> func, const char* path, int level = 0){
        if (isMap()) {
            for (auto& it : map()) {
                std::string subpath = std::string(path ? path : "") + "/" + it.first.c_str();
                if (func(*this, subpath.c_str(), it.first.c_str(), it.second, level) == false) {
                    return false;
                }
                if (it.second.each(func, subpath.c_str(), level + 1) == false) {
                    return false;
                }
            }
        }
        else if (isArray()) {
            char szidx[32];
            int idx = 0;
            const char* name_base = nullptr;
            for (auto& it : array()) {
                snprintf(szidx, sizeof(szidx), "[%d]", idx);
                idx++;
                std::string subpath = std::string(path ? path : "") + szidx;
                if (func(*this, subpath.c_str(), szidx, it, level) == false) {
                    return false;
                }
                if (it.each(func, subpath.c_str(), level + 1) == false) {
                    return false;
                }
            }
        }
        return true;
    }
public:
    int _flags; /*custom flags*/
    
private:
    data_t::type _data;
    std::type_index _type;
};

_TS_NAMESPACE_END

#endif /*_TS_PIE_INC_*/
