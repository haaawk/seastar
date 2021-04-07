/*
 * No deadlock
 */
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/semaphore.hh>
#include <iostream>
#include <experimental/source_location>

void xd() {
    static int i;
    std::cout << "xd" << " " << i++ << "\n";
}

template <typename Func>
seastar::future<> Repeat(std::size_t times, Func&& func) {
    if (times == 0)
        return seastar::make_ready_future<>();
    return seastar::futurize_invoke(func).then([times, func] {
        return Repeat(times-1, func);
    } );
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(seastar::semaphore(6), [] (auto& limit) {
            std::cerr << "tid:" << gettid() << "\n";
            std::cerr << "semaphore address: " << (void*)(&limit) << "\n";
            return seastar::when_all(Repeat(5, xd), Repeat(8, xd)).discard_result();
        });
    });
}