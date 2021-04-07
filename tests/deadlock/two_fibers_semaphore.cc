/*
 * Deadlocks
 */
#include <sys/types.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/sleep.hh>
#include <iostream>
#include <boost/iterator/counting_iterator.hpp>

seastar::future<> WorkingFiber(int id, seastar::semaphore& limit) {
    using namespace std::chrono_literals;
    if (id == 2) {
        return seastar::sleep(2s).then([&limit, id] {
            std::cerr << "Fiber " << id << " waits on " << (&limit) << "... waiters:" << limit.waiters() << std::endl;
            return limit.wait(3).then([&limit, id] {
                //limit.signal(1);
                std::cerr << "Fiber " << id << " sleeps: " << (&limit) << " waiters:" << limit.waiters() << std::endl;
                return seastar::sleep(1ms).then([&limit, id] {
                    std::cerr << "Fiber " << id << " waits again..." << (&limit) << " :" << limit.waiters() << std::endl;
                    return limit.wait(3).then([id] {
                        std::cerr << "Fiber " << id << " finishes." << std::endl;
                        return seastar::make_ready_future<>();
                    });
                });
            });
        });
    }
    std::cerr << "Fiber " << id << " waits on " << (&limit) << "... waiters:" << limit.waiters() << std::endl;
    return limit.wait(3).then([&limit, id] {
        //limit.signal(1);
        std::cerr << "Fiber " << id << " sleeps: " << (&limit) << " waiters:" << limit.waiters() << std::endl;
        return seastar::sleep(1ms).then([&limit, id] {
            std::cerr << "Fiber " << id << " waits again..." << (&limit) << " :" << limit.waiters() << std::endl;
            return limit.wait(3).then([id] {
                std::cerr << "Fiber " << id << " finishes." << std::endl;
                return seastar::make_ready_future<>();
            });
        });
    });
}

seastar::future<> WorkInLoop(int id, seastar::semaphore& limit) {
    return seastar::do_for_each(boost::counting_iterator(0), boost::counting_iterator(1), [&limit, id] (int) {
        return WorkingFiber(id, limit).finally([&limit] {
            limit.signal(6);
        });
    });
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(seastar::semaphore(6), [] (auto& limit) {
            std::cerr << "tid: " << gettid() << "\n";
            std::cerr << "semaphore address: " << (void*)(&limit) << "\n";
            return seastar::when_all(WorkInLoop(1, limit), WorkInLoop(2, limit)).discard_result();
        });
    });
}
