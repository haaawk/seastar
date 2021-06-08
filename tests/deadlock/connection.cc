/*
 * Deadlocks
 */
#include <iostream>
#include "seastar/core/do_with.hh"
#include "seastar/core/future.hh"
#include <seastar/core/app-template.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/condition-variable.hh>
#include <seastar/core/sleep.hh>
#include <boost/iterator/counting_iterator.hpp>

using namespace std::chrono_literals;

seastar::future<> maybe_sleep(bool sleep) {
    if (sleep) {
        return seastar::sleep(10ms);
    } else {
        return seastar::make_ready_future<>();
    }
}

seastar::future<> test(seastar::semaphore& limit) {
    return limit.wait().then([&limit] {
        return limit.wait();
    }).then([&limit] {
        limit.signal(2);
        return maybe_sleep(true);
    });
}

int main(int argc, char* argv[]) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(seastar::semaphore(2), [](auto& limit) {
            return seastar::when_all(seastar::do_for_each(boost::counting_iterator(0), boost::counting_iterator(3), [&limit](int) {
                return test(limit).then([&limit] {
                    return seastar::do_with(0, [&limit](int) {
                        return test(limit);
                    });
                });
            }));
        }).discard_result();
    });
}
