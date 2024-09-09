#ifndef TCP_KIT_THREAD_POOL_TEST_H
#define TCP_KIT_THREAD_POOL_TEST_H

#include <iostream>
#include <gtest/gtest.h>
#include <thread/thread_pool.h>
#include <logger/logger.h>

TEST(thread_pool_tests, execute) {
    blocking_queue<runnable> wq(10);
    thread_pool tp(4, 4, 1000, &wq);
    tp.execute([] {
//        GTEST_LOG_(INFO) << "TASK RUNNING";
    });
}

TEST(thread_pool_tests, exectue_fill_task) {
    blocking_queue<function<void()>> wq(10);
    thread_pool tp(1, 1, 1000, &wq);
    atomic<bool> condition;
    atomic<unsigned> count(0);
    for(int i = 0; i < 10; ++i) {
        tp.execute([&condition, &count] {
            while(!condition.load());
            log_info("count: %d\n", ++count);
        });
    }
    condition = true;
    while(count.load() != 10);
    log_info("%d\n", count.load());
}

namespace tcp_kit {

    namespace thread_pool_test {

        using namespace std;

        void t1() {
            blocking_queue<function<void()>> wq(10);
            thread_pool tp(4, 4, 1000, &wq);
            atomic<bool> condition;
            atomic<unsigned> count(0);
            for(int i = 0; i < 10; ++i) {
                tp.execute([&condition, &count] {
                    while(!condition.load());
                    log_info("count: %d\n", ++count);
                });
            }
            condition = true;
            while(count.load() != 10);
            log_info("%d\n", count.load());
        }

    }

}


#endif