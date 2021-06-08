/*
 * Deadlock happens when simulate_work is given an argument >> 0
 */
#include <iostream>
#include <chrono>
#include <random>

#include <boost/range/irange.hpp>

#include <seastar/core/app-template.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/sleep.hh>

static thread_local seastar::semaphore limit_concurrent(5);

template <typename T>
T random(T min, T max) {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return std::uniform_int_distribution<T>{min, max}(gen);
}

// Runs second stage of computation, taking lock again (allowing for deadlock).
static seastar::future<> run_2() {
    return seastar::with_semaphore(limit_concurrent, 1, [] {
        return seastar::sleep(std::chrono::microseconds(1));
    });
}

// Runs first stage of computation, taking lock.
static seastar::future<> run_1(int i) {
    return seastar::sleep(std::chrono::milliseconds(i)).then([i] {
        return seastar::with_semaphore(limit_concurrent, 1, [i] {
            std::cout << i << " finished first work" << std::endl;
            return seastar::sleep(std::chrono::microseconds(1)).then([] {
                return run_2();
            }).then([i] {
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
    seastar::app_template app;
    return app.run(ac, av, test);
}
