/*
 * Deadlocks
 */
#include "seastar/core/smp.hh"
#include <iostream>
#include <functional>

#include <seastar/core/app-template.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/tmp_file.hh>
#include <seastar/core/reactor.hh>

using namespace std::chrono_literals;

static seastar::future<> simulate_work(size_t milliseconds) {
    if (milliseconds == 0) {
        return seastar::make_ready_future<>();
    }
    size_t val = rand() % (milliseconds + 1);
    milliseconds -= val;
    return seastar::sleep(val * 1ms).then([milliseconds]() mutable {
        size_t val = rand() % (milliseconds / 10 + 1);
        milliseconds -= val;
        std::this_thread::sleep_for(val * 1ms);
        return simulate_work(milliseconds);
    });
}

static thread_local seastar::semaphore semaphore(1);

seastar::future<> block_globally() {
    seastar::shard_id id = seastar::this_shard_id();
    return seastar::sleep(id*1s).then([id] {
        return seastar::with_semaphore(semaphore, 1, [id] {
            std::cerr << "Locked myself " << id << std::endl;
            return simulate_work(10).then([id] {
                return seastar::smp::invoke_on_others(id, [id] {
                    std::cerr << "Locked " << seastar::this_shard_id() << " from " << id << std::endl;
                    return semaphore.wait();
                });
            }).then([id] {
                std::cerr << "Starting work " << id << std::endl;
                return simulate_work(10);
            }).then([id] {
                std::cerr << "Finished work " << id << std::endl;
            }).then([id] {
                return seastar::smp::invoke_on_others(id, [id] {
                                                          std::cout << "Signaled " << seastar::this_shard_id() << " from " << id << std::endl;
                                                          return semaphore.signal();
                                                      }
                );
            });
        });
    });
}

int main(int ac, char **av) {
    namespace bpo = boost::program_options;
    seastar::app_template app;
    return app.run(ac, av, [] {
        return seastar::smp::invoke_on_all(block_globally);
    });
}