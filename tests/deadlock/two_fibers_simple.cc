/*
 * Deadlocks
 */

#include <iostream>

#include <seastar/core/app-template.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/loop.hh>
#include <seastar/util/tmp_file.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>

using namespace std::chrono_literals;

static thread_local seastar::semaphore semaphore(1);

seastar::future<> stop() {
    std::cout << "First fiber enters" << std::endl;
    return seastar::sleep(2s).then([] {
        std::cout << "First fiber waits" << std::endl;
        return semaphore.wait().then([] {
            return semaphore.wait().then([] {
                std::cout << "First fiber exits" << std::endl;

                return seastar::make_ready_future();
            });
        });
    });
}

seastar::future<> stop_and_start() {
    std::cout << "Second fiber enters" << std::endl;
    return seastar::sleep(1s).then([] {
        std::cout << "Second fiber waits" << std::endl;
        return semaphore.wait().finally([] {
            std::cout << "Second fiber exits" << std::endl;
            return semaphore.signal(2);
        });
    });
}

seastar::future<> test() {
    return seastar::when_all_succeed(stop(), stop_and_start()).discard_result();
}

int main(int ac, char **av) {
    namespace bpo = boost::program_options;
    seastar::app_template app;
    return app.run(ac, av, [] {
        return test();
    });
}