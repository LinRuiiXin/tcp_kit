#ifndef TCP_KIT_THREAD_POOL_H
#define TCP_KIT_THREAD_POOL_H

#include <stdint.h>
#include <chrono>
#include <vector>
#include <utility>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <functional>
#include <concurrent/blocking_queue.hpp>
#include <thread/interruptible_thread.h>

#define COUNT_BITS 29

namespace tcp_kit {

    using namespace std;

    using runnable = function<void()>;
//    typedef void (*runnable)();

    class thread_pool {

    public:
        explicit thread_pool(uint32_t core_pool_size,
                             uint32_t max_pool_size,
                             uint64_t keepalive_time,
                             unique_ptr<blocking_queue<runnable>> work_queue);
        template <typename Func, typename... Args> void execute(Func&& first_task, Args&&... args);
        void execute(runnable first_task);
        void await_termination();
        template<typename Duration> void await_termination(Duration duration);
        void shutdown();
        bool is_shutdown();
        bool is_terminating();
        bool is_terminated();
        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;

    private:
        class worker {
        public:
            volatile uint64_t completed_tasks;
            shared_ptr<interruptible_thread> thread;

            explicit worker(thread_pool* tp, runnable first_task);

            bool try_lock();
            void lock();
            void unlock();
            bool locked() const;
            bool held_exclusive() const;
            void erase_exclusive_owner_thread();
            void set_exclusive_owner_thread(thread::id thread_id);

            ~worker();

            worker(const worker&) = delete;
            worker& operator=(const worker&) = delete;

        private:
            thread_pool*             _tp;
            mutex                    _mutex;
            volatile int8_t          _state;
            thread::id*              _exclusive_owner_thread;
            runnable                 _first_task;
            friend thread_pool;

        };

        static const int32_t CAPACITY   = (1 << COUNT_BITS) - 1;
        static const int32_t RUNNING    = -1 << COUNT_BITS;
        static const int32_t SHUTDOWN   =  0 << COUNT_BITS;
        static const int32_t STOP       =  1 << COUNT_BITS;
        static const int32_t TIDYING    =  2 << COUNT_BITS;
        static const int32_t TERMINATED =  3 << COUNT_BITS;
        atomic<int32_t>      _ctl;
        static const bool    ONLY_ONE = true;

        recursive_mutex  _mutex;
        const uint32_t   _core_pool_size;
        const uint32_t   _max_pool_size;
        const uint64_t   _keepalive_time;
        const bool       _allow_core_thread_timeout = false; // TODO
        uint32_t         _largest_pool_size;
        uint64_t         _completed_task_count;
        condition_variable_any                _termination;
        unordered_set<shared_ptr<worker>>     _workers; // TODO should be thread safe
        unique_ptr<blocking_queue<runnable>>  _work_queue;

        static int32_t run_state_of(int32_t c);
        static int32_t worker_count_of(int32_t ctl);
        static int32_t ctl_of(int32_t rs, int32_t wc);
        static bool run_state_at_least(int32_t c, int32_t s);
        static bool run_state_less_than(int32_t c, int32_t s);
        static bool is_running(int32_t c);
        bool add_worker(runnable first_task, bool core);
        void run_worker(const shared_ptr<worker>& w);
        virtual void before_execute(shared_ptr<interruptible_thread>& t, runnable r);
        virtual void after_execute(shared_ptr<interruptible_thread>& t, const exception_ptr& exp);
        virtual void terminated();
        virtual void on_shutdown();
        runnable get_task();
        void interrupt_idle_workers();
        void interrupt_idle_workers(bool only_one);
        void process_worker_exit(const shared_ptr<worker>& w, bool completed_abruptly);
        void decrement_worker_count();
        bool compare_and_decrement_worker_count(int32_t expect);
        void add_worker_failed(const shared_ptr<worker>& w);
        bool remove(runnable task);
        void reject(runnable task);
        void advance_run_state(uint32_t target_state);
        void try_terminate();
        void check_shutdown_access();
    };

    template <typename Func, typename... Args>
    void thread_pool::execute(Func&& first_task, Args&&... args) {
        auto bound_func = std::bind(std::forward<Func>(first_task), std::forward<Args>(args)...);
        execute((runnable) bound_func);
    }

    template<typename Duration>
    void thread_pool::await_termination(Duration duration) {
        unique_lock<recursive_mutex> main_lock(_mutex);
        if(run_state_less_than(_ctl.load(), TERMINATED))
            interruptible_wait_for(_termination, main_lock, duration);
    }

}

#endif
