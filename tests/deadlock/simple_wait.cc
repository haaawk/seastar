/*
 * Doesn't deadlock
 */
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/semaphore.hh>
#include <iostream>

seastar::future<> Example(seastar::semaphore& limit) {
    using namespace std::chrono_literals;
    return limit.wait(3).then([&limit] () mutable {
        std::cerr << "waited 1\n";
        return seastar::sleep(1ms).then([&limit] () mutable {
            return limit.wait(3).then([&limit] {
                std::cerr << "waited 2\n";
                limit.signal(6);
                return seastar::make_ready_future<>();
            });
        });
    });
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(seastar::semaphore(6), [] (auto& limit) {
            std::cerr << "tid: " << gettid() << "\n";
            std::cerr << "semaphore address: " << (void*)(&limit) << "\n";
            return Example(limit);
        });
    });
}