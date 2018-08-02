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
#if !defined(_TS_TYPES_INC_)
#define _TS_TYPES_INC_
#pragma once

#include <typeinfo>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include <ts/tss.h>

_TS_NAMESPACE_BEGIN

namespace types {
    using stream = std::vector<uint8_t>;
    
    struct slider {
        const uint8_t* ptr;
        int size;
        int pos;
    };
    
    //type functions
    template <typename T, typename TU, int I>
    struct type_find {
        using type = typename std::tuple_element<I, TU>::type;
        constexpr static const int index = std::is_same<type, T>::value ? I : type_find<T, TU, I-1>::index;
    };
    template <typename T, typename TU>
    struct type_find<T, TU, 0> {
        using type = typename std::tuple_element<0, TU>::type;
        constexpr static const int index = std::is_same<type, T>::value ? 0 : -1;
    };
    
    //type detecting
    typedef enum {_is_number, _is_string, _is_vector, _is_message} type_t;
    template <typename T>
    struct is_number {constexpr static const bool value = std::is_integral<T>::value || std::is_floating_point<T>::value;};
    template <typename T>
    struct is_string {constexpr static const bool value = std::is_same<T, std::string>::value;};
    template <typename T>
    struct is_vector {constexpr static const bool value = false;};
    template <typename T>
    struct is_vector <typename std::vector<T>> {constexpr static const bool value = true;};
    struct message_t;
    template <typename T>
    struct is_message {constexpr static const bool value = std::is_base_of<message_t, T>::value;};
    struct leading_void {char _zero[0];};
    template <typename T>
    struct is_leading {constexpr static const bool value = std::is_same<T,leading_void>::value || (std::is_integral<T>::value&&std::is_unsigned<T>::value);};

    template <typename T, int flags = types::is_number<T>::value | types::is_string<T>::value<<1 | types::is_vector<T>::value<<2 | types::is_message<T>::value<<3>
    struct type_detecting {};
    template <typename T> struct type_detecting <T,1> {constexpr static const type_t value = _is_number;};
    template <typename T> struct type_detecting <T,2> {constexpr static const type_t value = _is_string;};
    template <typename T> struct type_detecting <T,4> {constexpr static const type_t value = _is_vector;};
    template <typename T> struct type_detecting <T,8> {constexpr static const type_t value = _is_message;};

    //data unit type
    template <typename T, typename Leading = leading_void, class = typename std::enable_if<!std::is_pointer<T>::value && types::is_leading<Leading>::value>>
    struct span {
        using type = T;
        using leading_type = Leading;
    };
    
    //tool functions
    inline void order_number_copy(uint8_t* des, const uint8_t* src, int len) {
#if defined(__LITTLE_ENDIAN__)
        memcpy(des, src, len);
#elif defined(__BIG_ENDIAN__)
        src += len;
        while (len-- > 0) {
            *des++ = *(--src);
        }
#else
        memcpy(des, src, len);
#endif
    }
    template <typename T>
    struct leading_copy {
        static size_t read(const uint8_t* src) {
            T v = 0;
            order_number_copy((uint8_t*)&v, src, sizeof(T));
            return (size_t)v;
        }
        static void write(uint8_t* des, size_t sz) {
            T v = sz;
            return order_number_copy(des, (const uint8_t*)&v, sizeof(T));
        }
    };
    template <> struct leading_copy <leading_void> {
        static size_t read(const uint8_t* src) { return 0;}
        static void write(uint8_t* des, size_t sz) {}
    };

    using function_read_t = void (*)(void*, slider&);
    using function_write_t = void (*)(void*, types::stream& out);
    template <typename TD, typename TS>
    inline TD __convert(TS v) {
        union {
            TS _v;
            TD _d;
        } __c = {v};
        return __c._d;
    }

    template <typename SPAN, type_t t = type_detecting<typename SPAN::type>::value>
    struct var_op { };
    template <typename SPAN>
    struct var_op <SPAN, _is_number>{ // for for integral and float
        using T = typename SPAN::type;
        static void read(T& v, slider& sl) {
            if(sl.size < (sl.pos + sizeof(T))) { throw std::out_of_range(""); }
            order_number_copy((uint8_t*)&v, sl.ptr + sl.pos, sizeof(T));
            sl.pos += sizeof(T);
        }
        static void write(const T& v, types::stream& out) {
            int szold = out.size(); out.resize(szold + sizeof(T));
            uint8_t* po = &out[szold];
            order_number_copy(po, (const uint8_t*)&v, sizeof(T));
        }
    };
    template <typename SPAN>
    struct var_op <SPAN, _is_string> { //for string
        using T = typename SPAN::type;
        using TL = typename SPAN::leading_type;
        static void* read(T& v, slider& sl) {
            size_t szleading = sizeof(TL);
            size_t lnleading = 0;
            if (szleading) {
                if(sl.size < (sl.pos + szleading)) { throw std::out_of_range(""); }
                lnleading = types::leading_copy<TL>::read(sl.ptr + sl.pos);
                sl.pos += szleading;
            }
            else {
                lnleading = strlen((const char *)(sl.ptr + sl.pos));
            }
            if (lnleading) {
                if(sl.size < (sl.pos + lnleading)) { throw std::out_of_range(""); }
                v.assign((const char*)(sl.ptr + sl.pos), lnleading);
                sl.pos += lnleading;
            }
            if (!szleading && sl.size > sl.pos) {sl.pos++;} //skip a \0
        }
        static void write(const T& v, types::stream& out) {
            size_t szleading = sizeof(TL);
            size_t szold = out.size();
            size_t lnleading = v.size();
            size_t sznew = szleading + lnleading + szold;
            out.resize(sznew);
            uint8_t* po = &out[szold];
            if (szleading) {
                types::leading_copy<TL>::write(po, lnleading);
                po += szleading;
            }
            if (lnleading) memcpy(po, v.c_str(), lnleading);
            if (!szleading) { //append a \0
                out.push_back(0);
            }
        }
    };
    
    template <typename SPAN>
    struct var_op <SPAN, _is_vector> { //for vector<>
        using T = typename SPAN::type;
        using TL = typename SPAN::leading_type;
        using TSUB = typename T::value_type;
        static void read(T& v, slider& sl) {
            size_t szleading = sizeof(TL);
            size_t lnleading = 0;
            if (szleading) {
                if(sl.size < (sl.pos + szleading)) { throw std::out_of_range(""); }
                lnleading = types::leading_copy<TL>::read(sl.ptr + sl.pos);
                sl.pos += szleading;
                while (lnleading--) {
                    TSUB& vsub = *v.insert(v.end(), TSUB());
                    var_op<span<TSUB>>::read(vsub, sl);
                }
            }
            else {//until touch the end
                while (sl.size > sl.pos) {
                    TSUB& vsub = *v.insert(v.end(), TSUB());
                    var_op<span<TSUB>>::read(vsub, sl);
                }
            }
        }
        static void write(const T& v, types::stream& out) {
            size_t szleading = sizeof(TL);
            size_t szold = out.size();
            size_t lnleading = v.size();
            if (szleading) {
                size_t sznew = szleading + szold;
                out.resize(sznew);
                uint8_t* po = &out[szold];
                types::leading_copy<TL>::write(po, lnleading);
            }
            for(typename T::const_iterator it = v.begin(); it != v.end(); it++) {
                const TSUB& vsub = *it;
                var_op<span<TSUB>>::write(vsub, out);
            }
        }
    };

    template <typename SPAN>
    struct var_op <SPAN, _is_message> { //for message
        using T = typename SPAN::type;
        using TL = typename SPAN::leading_type;
        static void read(T& v, slider& sl) {
            size_t szleading = sizeof(TL);
            size_t lnleading = 0;
            if (szleading) {
                if(sl.size < (sl.pos + szleading)) { throw std::out_of_range(""); }
                lnleading = types::leading_copy<TL>::read(sl.ptr + sl.pos);
                sl.pos += szleading;
                slider slsub = {sl.ptr + sl.pos, sl.size - sl.pos, 0};
                v << slsub;
                if (sl.pos != sl.size) {
                    throw std::out_of_range("");
                }
                sl.pos += slsub.pos;
            }
            else {
                v << sl;
            }
        }
        static void write(const T& v, types::stream& out) {
            size_t szleading = sizeof(TL);
            size_t szold = out.size();
            if (szleading) {
                size_t sznew = szleading + szold;
                out.resize(sznew);
            }
            size_t posold = out.size();
            v >> out;
            if (szleading) {
                uint8_t* po = &out[szold];
                types::leading_copy<TL>::write(po, posold);
            }
        }
    };

    //special span with packaging attribute and tag
    typedef enum {_optinal, _required} package_attr_t;
    template <typename SPAN, package_attr_t _re = _required, int _ta = -1>
    struct noun {
        using type = typename SPAN::type;
        using leading_type = typename SPAN::leading_type;
        constexpr static const package_attr_t required = _re;
        constexpr static const int tag = _ta;
    };
    
    //message type
    struct message_t {};
    template <typename... Spans>
    struct message : public message_t {
    public:
        typedef std::tuple<typename std::decay<Spans>::type...> _TuSpans;
        typedef std::tuple<typename std::decay<Spans>::type::type...> _TuVars;
        typedef typename std::__make_tuple_indices<sizeof...(Spans)>::type __indices;
        constexpr static const int size = sizeof...(Spans);
    protected:
        static function_read_t reader(int i) {
            static const function_read_t __reader_fns_[size] = {__convert<function_read_t>(&var_op<Spans>::read)...};
            return __reader_fns_[i];
        }
        static function_write_t writer(int i) {
            static const function_write_t __writer_fns_[size] = {__convert<function_write_t>(&var_op<Spans>::write)...};
            return __writer_fns_[i];
        }
        template<size_t ..._Indx>
        static const void* offset(int i, std::__tuple_indices<_Indx...>) {
            static const void* __offsets_[size] = {&std::get<_Indx>(*(_TuVars*)nullptr)...};
            return __offsets_[i];
        }
        static unsigned long offset(int i) {
            static __indices __indices_;
            return (unsigned long)message::offset(i, __indices_);
        }
    private:
        _TuVars __vars_;
    public:
        message& operator << (slider& sl) {
            for(int i = 0; i < message::size; i++) {
                (*reader(i))(reinterpret_cast<void*>(((unsigned long)&__vars_) + message::offset(i)), sl);
            }
            return *this;
        }
        stream& operator >> (stream& out) const {
            for(int i = 0; i < message::size; i++) {
                (*writer(i))(reinterpret_cast<void*>(((unsigned long)&__vars_) + message::offset(i)), out);
            }
            return out;
        }
        template <typename T>
        typename T::type& get(void) {
            constexpr static const int index = type_find<T, _TuSpans, sizeof...(Spans) - 1>::index;
            return std::get<index>(__vars_);
        }
        template <typename T>
        const typename T::type& get(void) const {
            constexpr static const int index = type_find<T, _TuSpans, sizeof...(Spans) - 1>::index;
            return std::get<index>(__vars_);
        }
        
        template <size_t _Jp>
        typename std::tuple_element<_Jp, _TuVars>::type& get(void) {
            return std::get<_Jp>(__vars_);
        }
        template <size_t _Jp>
        const typename std::tuple_element<_Jp, _TuVars>::type& get(void) const {
            return std::get<_Jp>(__vars_);
        }
    };
};

_TS_NAMESPACE_END

#endif /*_TS_TYPES_INC_*/

