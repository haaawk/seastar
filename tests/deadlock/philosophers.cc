#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/semaphore.hh>
#include <iostream>

seastar::future<> philosopher(int id, seastar::semaphore& left, seastar::semaphore& right) {
    return seastar::sleep(std::chrono::seconds(id)).then([&left, id] {
        std::cerr << "Philosopher " << id << " waits on " << (&left);
        std::cerr << "... waiters:" << left.waiters() << std::endl;
        return left.wait(1);
    }).then([&right] {
        return right.wait(1);
    });
}

seastar::future<> eat_in_loop(int id, seastar::semaphore& left, seastar::semaphore& right) {
    return philosopher(id, left, right).finally([&left, &right] {
        left.signal(1);
        right.signal(1);
    });
}

int main(int argc, char** argv) {
    static thread_local int philosopher_cnt;

    std::cin >> philosopher_cnt;

    seastar::app_template().run(argc, argv, [] {
        return seastar::do_with(std::vector<seastar::semaphore>(), std::vector<seastar::future<>>(),
                [](auto& chopsticks, auto& philosophers) {
                    for (int i = 0; i < philosopher_cnt; i++) {
                        // Each chopstick can be taken just by one philosopher.
                        chopsticks.emplace_back(1);
                    }

                    for (int i = 0; i < philosopher_cnt; i++) {
                        auto fut = eat_in_loop(i, chopsticks[i], chopsticks[(i + 1) % philosopher_cnt]);
                        philosophers.emplace_back(std::move(fut));
                    }

                    return seastar::when_all_succeed(philosophers.begin(), philosophers.end());
                });
    });
}
