/*
 * Deadlocks
 */
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/condition-variable.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>

using namespace std::chrono_literals;

seastar::future<> exchange(int id, int count, bool start, seastar::condition_variable& cvar) {
    return seastar::do_with(start, count, [&cvar, id](auto& start, auto& count) {
        return seastar::do_until([&count] { return count == 0; }, [id, &count, &cvar, &start] {
            count--;
            std::cerr << "Fiber no " << id << ", start=" << start << "\n";
            start = !start;
            if (start) {
                return cvar.wait();
            }
            return seastar::sleep(0s).then([&cvar] {
                cvar.signal();
                return seastar::make_ready_future<>();
            });
        }).discard_result();
    });
}

int main(int argc, char* argv[]) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(seastar::condition_variable(), [](auto& cvar) {
            return seastar::when_all(exchange(1, 3, false, cvar), exchange(2, 3, true, cvar));
        }).discard_result();
    });
}
