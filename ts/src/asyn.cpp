#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <tuple>
#include <chrono>
#include <list>
#include <map>
#include <algorithm>
#include <ts/asyn.h>
#include <ts/json.h>
#include <ts/log.h>
#if defined(__APPLE__) || defined(__MACH__)
# include <mach/mach_time.h>
#endif

_TS_NAMESPACE_USING
_TS_NAMESPACE_BEGIN

typedef std::map<int, std::pair<ts::runnable::listener*, bool>>    mapListener;
typedef std::map<uint64_t, void*> mapKeyValue;
typedef runnable::task_id   task_id_t;

static thread_local runnable*           _local_this = nullptr;

void renameThread(const char* name) {
#if defined(PR_SET_NAME)
    // Only the first 15 characters are used (16 - NUL terminator)
    ::prctl(PR_SET_NAME, name, 0, 0, 0);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
    pthread_set_name_np(pthread_self(), name);
#elif defined(__APPLE__) || defined(__MACH__)
    pthread_setname_np(name);
#else
    // Prevent warnings for unused parameters...
    (void)name;
#endif
}

//for time
//_______________________________________________________________________________________________________________
int64_t getUptimeInMilliseconds(void) {
#if defined(_WIN32) || defined(_WIN64)
    return GetTickCount();
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)(t.tv_sec)*1000LL + (t.tv_nsec/1000000);
#elif defined(__APPLE__) || defined(__MACH__)
    static mach_timebase_info_data_t s_timebase_info;
    if (s_timebase_info.denom == 0) {
        (void) mach_timebase_info(&s_timebase_info);
    }
    // mach_absolute_time() returns billionth of seconds,
    // so divide by one million to get milliseconds
    return (int64_t)((mach_absolute_time() * s_timebase_info.numer) / (1000000 * s_timebase_info.denom));
#endif
}

uint64_t getThreadId(void) {
    pthread_t tid = pthread_self();
    uint64_t thread_id = 0;
    memcpy(&thread_id, &tid, std::min(sizeof(thread_id), sizeof(tid)));
    return thread_id;
}

/*check fd is valid or not*/
static inline bool fd_isvalid(int fd) {
    struct    timeval to = {0, 1};
    fd_set    fdrset;
    FD_ZERO(&fdrset);
    FD_SET(fd, &fdrset);
    return select(fd + 1, &fdrset, NULL, NULL, &to) >= 0;
}

struct listAction {
    struct action_t {
        enum {trap,push,cancel,listen,unlisten,markWritable} mode;
        std::shared_ptr<runnable::bind_base_t> call;
        task_id_t   id; //or fd
        int64_t     period;     /*period value in milliseconds*/
        int64_t     timeout;    /*timeout value in milliseconds*/
        int64_t     count;      /*loop count*/
        bool        consumed;
        action_t*   prev;
        action_t*   next;
        static action_t* zero(void) { return new action_t{trap, nullptr, runnable::invalid_task_id, 0, 0, 0, false, nullptr, nullptr};}
    };
    
    action_t    _head;
    action_t*   _tail;
    
    listAction(void) : _head{action_t::trap, nullptr, runnable::invalid_task_id, 0, 0, 0} {_tail = &_head;}
    ~listAction(void) {
        clear();
    }
    
    void clear(void) {
        action_t* it = _head.next;
        while (it) {
            action_t* itrm = it;
            it = it->next;
            delete itrm;
        }
        _head.next = nullptr;
        _tail = &_head;
    }
    
    void insert(action_t* one) {
        if (_head.next) {
            action_t* it = _head.next;
            while (it) {
                if (it->timeout > one->timeout) {
                    it->prev->next = one;
                    one->prev = it->prev;
                    one->next = it;
                    it->prev = one;
                    break;
                }
                it = it->next;
            }
            if (it == nullptr) {//add to tail
                _tail->next = one;
                one->prev = _tail;
                one->next = nullptr;
                _tail = one;
            }
        }
        else {
            _head.next = one;
            one->prev = &_head;
            one->next = nullptr;
            _tail = one;
        }
    }
    
    void addToTail(action_t* one) {
        _tail->next = one;
        one->prev = _tail;
        one->next = nullptr;
        _tail = one;
    }
    
    void remove(action_t* one) {
        if (&_head == one) return;
        if (one == _tail) _tail = one->prev;
        one->prev->next = one->next;
        if (one->next) {
            one->next->prev = one->prev;
        }
        delete one;
    }
    
    void moveToTail(action_t* one) {
        if (&_head == one) return;
        action_t* pos = one;
        action_t* next = one->next;
        while (next) {
            if (next->timeout > one->timeout) {
                break;
            }
            pos = next;
            next = next->next;
        }
        if (pos != one) {
            one->prev->next = one->next;
            one->next->prev = one->prev;
            
            if (pos->next) pos->next->prev = one;
            one->next = pos->next;
            pos->next = one;
            one->prev = pos;
        }
        if (one->next == nullptr) _tail = one;
    }
};

struct bind_owner_t : public runnable::bind_base_t {
    std::shared_ptr<life> _life;
    void* _owner;
    void invoke(void) {}
    void* owner(void) {return _owner;}
};

//for runnable bridge
//_______________________________________________________________________________________________________________
struct runnable_bridge {
    std::unique_ptr<std::thread> _thread;
    uint64_t    _creator;
    uint64_t    _self;
    const char* _name;
    bool        _running;
    bool        _going;
    bool        _reset;
    int         _signals[2]; /*read and write*/
    task_id_t   _idNext;
    mapListener _listeners;
    mapKeyValue _keyValues;
    listAction* _waitings;  /*critical area*/
    listAction  _waitings_cache[2];   /*critical area*/
    listAction  _realtimes;
    listAction  _delays;
    std::mutex  _lock;
};

runnable::runnable(const char* name) : _bridge(new runnable_bridge{nullptr, getThreadId(), 0, name, false, true, false, {-1,-1}, 0, mapListener(), mapKeyValue()}) {
    _bridge->_waitings_cache[0].addToTail(listAction::action_t::zero());
    _bridge->_waitings_cache[1].addToTail(listAction::action_t::zero());
    _bridge->_waitings = &_bridge->_waitings_cache[0];

    if (pipe(_bridge->_signals) == -1) {
        log_notice("failed to create pipe!");
        throw std::runtime_error("failed to create pipe!");
    }
    else {
        int flags = fcntl(_bridge->_signals[0], F_GETFL, 0);
        fcntl(_bridge->_signals[0], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(_bridge->_signals[1], F_GETFL, 0);
        fcntl(_bridge->_signals[1], F_SETFL, flags | O_NONBLOCK);
    }
}

runnable::~runnable(void) {
    close(_bridge->_signals[0]);
    close(_bridge->_signals[1]);
}

//static functions
runnable*    runnable::current(void) {
    return _local_this;
}

task_id_t   runnable::push(std::shared_ptr<runnable::bind_base_t> ca, int64_t miliseconds, int64_t count, runnable* target) {
    if (count == 0) {
        log_error("illegal argment!");
        return runnable::invalid_task_id;
    }
    if (target == nullptr) target = _local_this;
    if (target == nullptr) {
        log_error("not valid runnable object found!");
        return runnable::invalid_task_id;
    }

    runnable_bridge* bridge = target->_bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);

    bridge->_waitings->_tail->mode   = listAction::action_t::push;
    bridge->_waitings->_tail->call   = ca;
    bridge->_waitings->_tail->period = miliseconds;
    bridge->_waitings->_tail->count  = count;
    bridge->_waitings->_tail->timeout= getUptimeInMilliseconds() + miliseconds;
    task_id id = bridge->_idNext++;
    bridge->_waitings->_tail->id     = id;
    
    target->expansion_commit();
    
    return id;
}

void     runnable::cancel(task_id id, runnable* target) {
    if (id == runnable::invalid_task_id) {
        log_warning("illegal argment!");
        return;
    }
    if (target == nullptr) target = _local_this;
    if (target == nullptr) {
        log_error("not valid runnable object found!");
        return;
    }

    runnable_bridge* bridge = target->_bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    
    bridge->_waitings->_tail->mode   = listAction::action_t::cancel;
    bridge->_waitings->_tail->id     = id;
    
    target->expansion_commit();
}

void     runnable::cancelOwner(void* owner, runnable* target) {
    if (owner == nullptr) {
        log_warning("illegal argment!");
        return;
    }
    if (target == nullptr) target = _local_this;
    if (target == nullptr) {
        log_error("not valid runnable object found!");
        return;
    }
    
    bind_owner_t* bind = new bind_owner_t();
    bind->_owner = owner;

    runnable_bridge* bridge = target->_bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    
    bridge->_waitings->_tail->mode   = listAction::action_t::cancel;
    bridge->_waitings->_tail->call   = std::shared_ptr<runnable::bind_base_t>(bind);
    bridge->_waitings->_tail->id     = runnable::invalid_task_id;
    
    target->expansion_commit();
}

void     runnable::addListener(listener* lis, int fd, const runnable* target) {
    if (fd < 0 || lis == nullptr) {
        log_warning("illegal argment!");
        return;
    }
    if (target == nullptr) target = _local_this;
    if (target == nullptr) {
        log_error("not valid runnable object found!");
        return;
    }
    
    bind_owner_t* bind = new bind_owner_t();
    bind->_life = nullptr;
    bind->_owner= lis;

    runnable_bridge* bridge = target->_bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    
    bridge->_waitings->_tail->mode  = listAction::action_t::listen;
    bridge->_waitings->_tail->call  = std::shared_ptr<runnable::bind_base_t>(bind);
    bridge->_waitings->_tail->id    = fd;

    const_cast<runnable*>(target)->expansion_commit();
}

void     runnable::removeListener(int fd, const runnable* target) {
    if (fd == -1) {
        log_warning("illegal argment!");
        return;
    }
    if (target == nullptr) target = _local_this;
    if (target == nullptr) {
        log_error("not valid runnable object found!");
        return;
    }
    
    bind_owner_t* bind = new bind_owner_t();
    bind->_life = nullptr;
    bind->_owner= nullptr;
    
    runnable_bridge* bridge = target->_bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    
    bridge->_waitings->_tail->mode  = listAction::action_t::unlisten;
    bridge->_waitings->_tail->call  = std::shared_ptr<runnable::bind_base_t>(bind);
    bridge->_waitings->_tail->id    = fd;
    
    const_cast<runnable*>(target)->expansion_commit();
}

void     runnable::wantWritable(int fd, bool want, const runnable* target) {
    if (fd < 0) {
        log_warning("illegal argment!");
        return;
    }
    if (target == nullptr) target = _local_this;
    if (target == nullptr) {
        log_error("not valid runnable object found!");
        return;
    }
    
    runnable_bridge* bridge = target->_bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    
    bridge->_waitings->_tail->mode   = listAction::action_t::markWritable;
    bridge->_waitings->_tail->id     = fd;
    bridge->_waitings->_tail->count  = want;
    
    const_cast<runnable*>(target)->expansion_commit();
}

bool    runnable::setValue(uint64_t key, void* value) {
    runnable* ra = nullptr;
    if ( (ra = current()) == nullptr) {
        log_error("not valid runnable object found!");
        return false;
    }
    runnable_bridge* bridge = ra->_bridge.get();
    bridge->_keyValues[key] = value;
    return true;
}

void*   runnable::getValue(uint64_t key) {
    runnable* ra = nullptr;
    if ( (ra = current()) == nullptr) {
        log_error("not valid runnable object found!");
        return nullptr;
    }
    runnable_bridge* bridge = ra->_bridge.get();
    mapKeyValue::iterator it = bridge->_keyValues.find(key);
    if (it != bridge->_keyValues.end()) {
        return it->second;
    }
    return nullptr;
}

//dynamic functions
bool    runnable::start(void) {
    runnable_bridge* bridge = _bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    if (bridge->_running) {
        log_error("aready started!");
        return false;
    }
    bridge->_running = true;
    bridge->_thread = std::unique_ptr<std::thread>(new std::thread(&runnable::loop_join, this));
    return bridge->_running;
}

std::unique_ptr<std::thread>    runnable::stop(void) {
    runnable_bridge* bridge = _bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    if (bridge->_running == false) {
        log_error("aready stoped!");
        return nullptr;
    }
    std::unique_ptr<std::thread> r(_bridge->_thread.release());
    bridge->_going = false;
    write(bridge->_signals[1], "X", 1);
    return r;
}

void    runnable::reset(void) {
    runnable_bridge* bridge = _bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    bridge->_reset = true;
    write(bridge->_signals[1], "X", 1);
}

void    runnable::join(void) {
    runnable_bridge* bridge = _bridge.get();
    bridge->_thread->join();
}

bool    runnable::running(void) const {
    runnable_bridge* bridge = _bridge.get();
    std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
    return bridge->_running;
}

bool    runnable::verify(bool log) const {
    if (!running()) {
        log_error("worker not found!");
        return false;
    }
    if (this != current()) {
        if (log) log_error("worker missmatched!");
        return false;
    }
    return true;
}

void    runnable::expansion_commit(void) {
    _bridge->_waitings->addToTail(listAction::action_t::zero());
    write(_bridge->_signals[1], "X", 1);
}

void    runnable::loop_join(void) {
    runnable_bridge* bridge = _bridge.get();
    {//init
        std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
        _local_this = this;
        renameThread(bridge->_name);
        bridge->_self = getThreadId();
    }
    
    loop();
    
    {//shutdown
        std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
        bridge->_running = false;
    }
    
    fly();
}

void    runnable::loop(void) {
    runnable_bridge* bridge = _bridge.get();
    while (bridge->_going) {
        if (bridge->_reset) {
            std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
            bridge->_reset = false;
            bridge->_waitings->clear();
            bridge->_waitings->addToTail(listAction::action_t::zero());
            bridge->_realtimes.clear();
            bridge->_delays.clear();
            bridge->_listeners.clear();
        }
        int64_t ms = excute();
        wait(ms ? ms : 1000);
    }
}

int64_t runnable::excute(void) {
    runnable_bridge* bridge = _bridge.get();
    {//deal with waiting queue
        listAction* queue = NULL;
        {
            std::lock_guard<std::mutex> _auto_lock(bridge->_lock);
            queue = bridge->_waitings;
            bridge->_waitings = bridge->_waitings == &bridge->_waitings_cache[0] ? &bridge->_waitings_cache[1] : &bridge->_waitings_cache[0];
        }
        listAction::action_t* it = queue->_head.next;
        while (it->next) {
            listAction::action_t* one = it;
            it = it->next;
            switch (one->mode) {
                case listAction::action_t::push: {
                    if (one->period) { //delay
                        bridge->_delays.insert(one);
                    }
                    else {
                        bridge->_realtimes.addToTail(one);
                    }
                } break;
                    
                case listAction::action_t::cancel : {
                    if (one->id != runnable::invalid_task_id) {//by idb
                        bool found = false;
                        {//find it from realtimes
                            listAction::action_t* prev = &bridge->_realtimes._head, *it = bridge->_realtimes._head.next;
                            while (it) {
                                if (it->id == one->id) {//found
                                    prev->next = it->next;
                                    if (prev->next == NULL) { // touch the end
                                        bridge->_realtimes._tail = prev;
                                    }
                                    delete it;
                                    found = true;
                                    break;
                                }
                                prev = it;
                                it = it->next;
                            }
                        }
                        if (!found) {//find it from delays
                            listAction::action_t *it = bridge->_delays._head.next;
                            while (it) {
                                if (it->id == one->id) {//found
                                    bridge->_delays.remove(it);
                                    break;
                                }
                                it = it->next;
                            }
                        }
                    }
                    else if (one->call->owner()) {//by owner
                        void* own = one->call->owner();
                        {//find it from realtimes
                            listAction::action_t* prev = &bridge->_realtimes._head, *it = bridge->_realtimes._head.next;
                            while (it) {
                                if (it->call->owner() == own) {//found
                                    prev->next = it->next;
                                    if (prev->next == NULL) { // touch the end
                                        bridge->_realtimes._tail = prev;
                                    }
                                    delete it;
                                    it = prev->next;
                                }
                                else {
                                    prev = it;
                                    it = it->next;
                                }
                            }
                        }
                        {//find it from delays
                            listAction::action_t* prev = &bridge->_delays._head, *it = bridge->_delays._head.next;
                            while (it) {
                                if (it->call->owner() == own) {//found
                                    prev->next = it->next;
                                    if (prev->next == NULL) { // touch the end
                                        bridge->_delays._tail = prev;
                                    }
                                    delete it;
                                    it = prev->next;
                                }
                                else {
                                    prev = it;
                                    it = it->next;
                                }
                            }
                        }
                        {//find it from listeners
                            mapListener::iterator it = bridge->_listeners.begin();
                            while (it != bridge->_listeners.end()) {
                                if (it->second.first == own) {
                                    close(it->first);
#if __cplusplus > 199711L
                                    it = bridge->_listeners.erase(it);
#else
                                    bridge->sockets.erase(it++);
#endif
                                }
                            }
                        }
                    }
                    else {
                        //error
                        log_error("logic error");
                    }
                    delete one;
                } break;
                    
                case listAction::action_t::listen : {
                    bridge->_listeners[(int)one->id] = std::make_pair(reinterpret_cast<listener*>(one->call->owner()), false);
                    delete one;
                } break;
                    
                case listAction::action_t::unlisten : {
                    mapListener::iterator it = bridge->_listeners.find((int)one->id);
                    if (it != bridge->_listeners.end()) {
                        bridge->_listeners.erase(it);
                    }
                    delete one;
                } break;

                case listAction::action_t::markWritable : {
                    mapListener::iterator it = bridge->_listeners.find((int)one->id);
                    if (it != bridge->_listeners.end()) {
                        it->second.second = one->count ? true : false;
                    }
                    delete one;
                } break;
                    
                default: {
                    delete one;
                } break;
            }
        }
        queue->_head.next = it;
    }
    if (bridge->_realtimes._head.next) {//deal with realtime queue
        listAction::action_t* it = bridge->_realtimes._head.next;
        while (it) {
            listAction::action_t* itrm = it;
            itrm->call->invoke();
            it = it->next;
            delete itrm;
        }
        bridge->_realtimes._head.next = NULL;
        bridge->_realtimes._tail = &bridge->_realtimes._head;
    }
    if (bridge->_delays._head.next) {//deal with delay queue
        listAction::action_t* it = bridge->_delays._head.next;
        while (it) {
            int64_t tv = getUptimeInMilliseconds();
            if (it->timeout > tv) {
                break;
            }
            listAction::action_t* one = it;
            one->call->invoke();
            it = it->next;
            if (--(one->count) == 0) {
                bridge->_delays.remove(one);
            }
            else {
                one->timeout = getUptimeInMilliseconds() + one->period;
                bridge->_delays.moveToTail(one);
            }
        }
    }
    return 0;
}

void    runnable::wait(int64_t milliseconds) {
    fd_set    fdrset, fdwset, fdeset;
#if defined(__APPLE__)
    typedef __darwin_time_t __second_t;
    typedef __darwin_suseconds_t  __suseconds_t;
#else
    typedef time_t __second_t;
    typedef long  __suseconds_t;
#endif
    struct    timeval to = {static_cast<__second_t>(milliseconds / 1000), static_cast<__suseconds_t>((milliseconds % 1000) * 1000)};
    runnable_bridge* bridge = _bridge.get();

    FD_ZERO(&fdrset);
    FD_ZERO(&fdwset);
    FD_ZERO(&fdeset);
    
    int fd = 0;
    
    if (bridge->_listeners.size()) {
        for (mapListener::iterator it = bridge->_listeners.begin(); it != bridge->_listeners.end(); it++) {
            FD_SET(it->first, &fdrset);
            FD_SET(it->first, &fdeset);
            if (it->second.second) {
                FD_SET(it->first, &fdwset);
            }
        }
        fd = (--bridge->_listeners.end())->first;
    }
    if (bridge->_signals[0] > fd) {
        fd = bridge->_signals[0];
    }
    if (bridge->_signals[1] > fd) {
        fd = bridge->_signals[1];
    }
    FD_SET(bridge->_signals[0], &fdrset);
    FD_SET(bridge->_signals[0], &fdeset);
    FD_SET(bridge->_signals[1], &fdrset);
    FD_SET(bridge->_signals[1], &fdeset);
    
    int r = select((int)fd + 1, &fdrset, &fdwset, &fdeset, &to);
    
    if (r == 0) {//nothing happen
        return;
    }
    else if ( r == -1) {
        if (EBADF == errno || ERANGE == errno) {
            for (mapListener::iterator it = bridge->_listeners.begin(); it != bridge->_listeners.end(); ) {
                if (!fd_isvalid(it->first)) { //except
                    it->second.first->onClose(it->first);
#if __cplusplus > 199711L
                    it = bridge->_listeners.erase(it);
#else
                    bridge->_listeners.erase(it++);
#endif
                }
                else {
                    ++it;
                }
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::duration<long double, std::milli>(10));
        }
    }
    else if (r == 1 && FD_ISSET(bridge->_signals[0], &fdrset)) { /*signal coming*/
        unsigned char buf[1024];
        ssize_t rx = 0;
        while((rx = read(bridge->_signals[0], buf, sizeof(buf))) > 0);
    }
    else {
        if (FD_ISSET(bridge->_signals[0], &fdrset)) {
            unsigned char buf[1024];
            ssize_t rx = 0;
            while((rx = read(bridge->_signals[0], buf, sizeof(buf))) > 0);
        }
        for (mapListener::iterator it = bridge->_listeners.begin(); it != bridge->_listeners.end(); it++) {
            if (FD_ISSET(it->first, &fdrset)) {//data coming ?
                size_t  count = 0;
#ifdef _OS_WIN_
                u_long lcount = 0;
                ioctlsocket(it->first, FIONREAD, &lcount);
                count = lcount;
#else
                ioctl(it->first, FIONREAD, &count);
#endif
                it->second.first->onRecv(it->first, count);
            }
            else if (FD_ISSET(it->first, &fdwset)) {//writable ?
                it->second.second = false;
                it->second.first->onWritable(it->first);
            }
        }
    }
}

//background mode
struct  background : public ts::life {
private:
    std::mutex  _lock;
    std::list<std::shared_ptr<ts::runnable>> _threads;
    std::unique_ptr<ts::runnable>   _cleaner;
public:
    background(void) {
        _cleaner.reset(new ts::runnable("tsCleaner"));
        _cleaner->start();
    }
    
    std::shared_ptr<ts::runnable> pop(void) {
        std::lock_guard<std::mutex> _auto_lock(_lock);
        if (_threads.size() == 0) {
            ts::runnable* ra = new ts::runnable("tsBackground");
            if (ra->start() == false) {
                ra->fly();
                throw std::runtime_error("failed to start thread!");
            }
            return std::static_pointer_cast<ts::runnable>(ra->clone());
        }
        std::shared_ptr<ts::runnable> it = _threads.front(); _threads.pop_front();
        it->reset();
        return it;
    }

    void push(std::shared_ptr<ts::runnable>& ra) {
        std::lock_guard<std::mutex> _auto_lock(_lock);
        _threads.push_back(ra);
        
        runnable::push(ts::make_bind(this, &background::shutdown, ra), 3000, 1, ra.get()); //30 seconds later
    }
    
    void shutdown(std::shared_ptr<ts::runnable> ra) {
        std::lock_guard<std::mutex> _auto_lock(_lock);
        std::list<std::shared_ptr<ts::runnable>>::iterator it = std::find(_threads.begin(), _threads.end(), ra);
        if (it == _threads.end()) {//ite must be poped up by runnable::background
            return;
        }
        _threads.remove(*it);
        runnable::push(ts::make_bind(this, &background::clean, ra), 0, 1, _cleaner.get());
    }
    
    void clean(std::shared_ptr<ts::runnable> ra) {
        ra->stop()->join();
    }

    void run(std::shared_ptr<ts::runnable> ra, std::shared_ptr<runnable::bind_base_t> c) {
        c->invoke();
        push(ra);
    }
};

void     runnable::background(std::shared_ptr<bind_base_t> ca) {
    static std::shared_ptr<struct background> s_bg(new struct background());
    std::shared_ptr<ts::runnable> ra = s_bg->pop();
    runnable::push(ts::make_bind(s_bg.get(), &background::run, ra, ca), 0, 1, ra.get());
}

//for explicitThreaded
//_______________________________________________________________________________________________________________
bool    parasite::verify(bool log) {
    if (!_host.running()) {
        if (log) log_error("worker not found!");
        return false;
    }
    if (&_host != runnable::current()) {
        if (log) log_error("worker missmatched!");
        return false;
    }
    return true;
}

_TS_NAMESPACE_END

