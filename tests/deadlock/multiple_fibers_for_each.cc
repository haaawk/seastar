#include <iostream>
#include <chrono>
#include <random>

#include <seastar/core/app-template.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/tmp_file.hh>
#include <seastar/core/reactor.hh>

using namespace std::chrono_literals;
static thread_local seastar::semaphore limit_concurrent(5);


template<typename T>
T random(T min, T max) {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return std::uniform_int_distribution<T>{min, max}(gen);
}

// Simulates work mixed of synchronous and asynchronous sleep.
static seastar::future<> simulate_work(size_t milliseconds) {
    if (milliseconds == 0) {
        return seastar::make_ready_future<>();
    }
    size_t val = random<size_t>(0, milliseconds);
    milliseconds -= val;
    return seastar::sleep(val * 1ms).then([milliseconds]() mutable {
        size_t val = random<size_t>(0, milliseconds / 10);
        milliseconds -= val;
        std::this_thread::sleep_for(val * 1ms);
        return simulate_work(milliseconds);
    });
}

// Runs second stage of computation, taking lock again (allowing for deadlock).
static seastar::future<> run_2() {
    return seastar::with_semaphore(limit_concurrent, 1, [] {
        return simulate_work(10);
    });
}

// Runs first stage of computation, taking lock.
static seastar::future<> run_1(int i) {
    return seastar::with_semaphore(limit_concurrent, 1, [i] {
        return simulate_work(10).then([i] {
            std::cout << i << " finished first work" << std::endl;
            return run_2().then([i] {
                std::cout << i << " finished second work" << std::endl;
            });
        });
    });
}

// Test that deadlocks.
static seastar::future<> test() {
    return seastar::parallel_for_each(boost::irange(5), run_1);
}

int main(int ac, char** av) {
    namespace bpo = boost::program_options;
    seastar::app_template app;
    return app.run(ac, av, test);
}
