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
#if !defined(_TS_ASYN_INC_)
#define _TS_ASYN_INC_
#pragma once

#include <memory>
#include <type_traits>
#include <tuple>
#include <functional>
#include <thread>
#include <ts/tss.h>

_TS_NAMESPACE_BEGIN

#define __attr_threading(...)

struct life {
    life(void) : _this(this) {}
    virtual ~life(void) {}
    inline std::shared_ptr<life> clone(void) const {
        return _this;
    }
    
    //fly away and release itself
    inline void fly(void) {
        _this.reset();
    }
    //is flied away ?
    inline bool flied(void) const {
        return _this.get() == nullptr;
    }
private:
    mutable std::shared_ptr<life> _this;
};

struct runnable : public life {
    struct bind_base_t {
    public:
        virtual ~bind_base_t(void){}
        virtual void    invoke(void) = 0;
        virtual void*   owner(void) = 0;
    };
    struct listener {
        virtual void onRecv(int fd, size_t size) = 0;
        virtual void onClose(int fd) = 0;
        virtual void onWritable(int fd) = 0;
    };

    typedef int64_t task_id;
    static constexpr task_id invalid_task_id = -1;
    
public:
    runnable(const char* name);
protected:
    template<class _Tp> friend class std::shared_ptr;
    template<class _Tp> friend struct std::default_delete;
    virtual ~runnable(void);

public:
    /*get the curretn run thread task */
    static runnable*    current(void);
    
    /*push task to current|specified thread*/
    static task_id  push(std::shared_ptr<bind_base_t> ca, int64_t miliseconds = 0, int64_t count = 1, runnable* target = nullptr);
    /*cancel task from current|specified thread*/
    static void     cancel(task_id id, runnable* target = nullptr);
    /*cancel task by owner*/
    static void     cancelOwner(void* owner, runnable* target = nullptr);
    
    /*we don't care lis is threadsafe or not*/
    static void     addListener(listener* lis, int fd, const runnable* target = nullptr);
    /*remove listener from current|specified thread*/
    static void     removeListener(int fd, const runnable* target = nullptr);
    /*mark file as sendable listening*/
    static void     wantWritable(int fd, bool want = true, const runnable* target = nullptr);
    
    /*store|get value to|from current thread*/
    static bool     setValue(uint64_t key, void*);
    static void*    getValue(uint64_t key);
    
    static void     background(std::shared_ptr<bind_base_t> ca);

    bool    start(void);
    std::unique_ptr<std::thread>    stop(void);     //stop and clear scene
    void    reset(void);    //clear scene only
    void    join(void);
    bool    running(void) const;
    bool    verify(bool log = true) const;
    
private:
    virtual void    loop(void);
    
private:
    void    expansion_commit(void);
    int64_t excute(void);
    void    wait(int64_t);
    void    loop_join(void);
    
private:
    std::unique_ptr<struct runnable_bridge> _bridge;
};

struct parasite {//no life
    parasite(runnable const& host) : _host(host) {}
    bool    verify(bool log = true);
    //life proxy
    inline std::shared_ptr<life> clone(void) const {
        return _host.clone();
    }
protected:
    runnable const& _host;
};

//asyn model
template<typename O, typename F, typename Tuple, size_t ...S >
void apply_tuple_impl(O p, F&& fn, Tuple&& t, std::__tuple_indices<S...>) {
    return (p->*fn)(std::get<S>(std::forward<Tuple>(t))...);
}

template <class T, typename... Args>
struct bind_t : public runnable::bind_base_t {
protected:
    typedef typename std::shared_ptr<ts::life> _Od;
    typedef T* _Cp;
    typedef void (T::*_Fp)(Args...);
    typedef typename std::decay<_Fp>::type _Fd;
    typedef std::tuple<typename std::decay<Args>::type...> _Td;
    typedef typename std::__make_tuple_indices<sizeof...(Args)>::type __indices;
private:
    _Od __o_;
    _Cp __p_;
    _Fd __f_;
    _Td __bound_args_;
public:
    explicit bind_t(_Cp& __p, _Fp& __f, Args&& ...__bound_args) : __o_(__p->clone()), __p_(__p), __f_(__f), __bound_args_(std::forward<Args>(__bound_args)...) {}
private:
    void invoke(void) {
        apply_tuple_impl(__p_, __f_, __bound_args_, __indices());
    }
    void* owner(void) {return __o_.get();}
};

template <class T, typename... Args>
std::shared_ptr<runnable::bind_base_t> make_bind(T* ptr, void (T::*f)(Args... args), Args... args) {
    return std::shared_ptr<runnable::bind_base_t>(new bind_t<T, Args...>(ptr, f, std::forward<Args>(args)...));
}

template <class T, typename... Args>
runnable::task_id repeat(int64_t miliseconds, int64_t count, T* ptr, void (T::*f)(Args... args), Args... args) {
    std::shared_ptr<runnable::bind_base_t> bind(new bind_t<T, Args...>(ptr, f, std::forward<Args>(args)...));
    return runnable::push(bind, miliseconds, count);
}

template <class T, typename... Args>
runnable::task_id repeat2(std::shared_ptr<runnable> target, int64_t miliseconds, int64_t count, T* ptr, void (T::*f)(Args... args), Args... args) {
    std::shared_ptr<runnable::bind_base_t> bind(new bind_t<T, Args...>(ptr, f, std::forward<Args>(args)...));
    return runnable::push(bind, miliseconds, count, target.get());
}

template <class T, typename... Args>
runnable::task_id delay(int64_t miliseconds, T* ptr, void (T::*f)(Args... args), Args... args) {
    return repeat(miliseconds, 1, ptr, f, std::forward<Args>(args)...);
}

template <class T, typename... Args>
runnable::task_id delay2(std::shared_ptr<runnable> target, int64_t miliseconds, T* ptr, void (T::*f)(Args... args), Args... args) {
    return repeat2(target, miliseconds, 1, ptr, f, std::forward<Args>(args)...);
}

template <class T, typename... Args>
runnable::task_id asyn(T* ptr, void (T::*f)(Args... args), Args... args) {
    return repeat(0, 1, ptr, f, std::forward<Args>(args)...);
}

template <class T, typename... Args>
runnable::task_id asyn2(std::shared_ptr<runnable> target, T* ptr, void (T::*f)(Args... args), Args... args) {
    return repeat2(target, 0, 1, ptr, f, std::forward<Args>(args)...);
}

template <class T, typename... Args>
void slide(T* ptr, void (T::*f)(Args... args), Args... args) {
    std::shared_ptr<runnable::bind_base_t> bind(new bind_t<T, Args...>(ptr, f, std::forward<Args>(args)...));
    return runnable::background(bind);
}

int64_t     getUptimeInMilliseconds(void);
uint64_t    getThreadId(void);

_TS_NAMESPACE_END

#endif /*_TS_ASYN_INC_*/
