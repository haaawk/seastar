#include <sys/types.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/sleep.hh>
#include <iostream>
#include <boost/iterator/counting_iterator.hpp>
#include <random>

seastar::future<> execute(int id, std::vector<seastar::semaphore>& semaphores, std::vector<std::pair<int, int>>::iterator operations,
        std::vector<std::pair<int, int>>::iterator end) {
    using namespace std::chrono_literals;
    if (operations == end) {
        return seastar::make_ready_future<>();
    }

    return seastar::sleep(1ms).then([&semaphores, operations, end, id] {
        std::pair operation = *operations;
        if (operation.second == 0) {
            return semaphores[operation.first].wait().then([id, &semaphores, operations, end] {
                return execute(id, semaphores, operations + 1, end);
            });
        } else {
            semaphores[operation.first].signal();
            return execute(id, semaphores, operations + 1, end);
        }
    });
}

int main(int argc, char** argv) {
    static thread_local int fibers_cnt, fibers_len, semaphore_cnt;
    static thread_local std::default_random_engine generator(4);

    std::cin >> fibers_cnt >> fibers_len >> semaphore_cnt;

    static thread_local std::uniform_int_distribution<int> sem_dist(0, semaphore_cnt - 1);
    static thread_local std::uniform_int_distribution<int> op_dist(0, 1);

    static thread_local auto choose_sem = std::bind(sem_dist, generator);
    static thread_local auto choose_op = std::bind(op_dist, generator);

    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(std::vector<seastar::semaphore>(), std::vector<std::vector<std::pair<int, int>>>(), std::vector<seastar::future<>>(),
                [](auto& semaphores, auto& operations, auto& to_do) {
                    for (int i = 0; i < semaphore_cnt; i++) {
                        semaphores.push_back(seastar::semaphore(fibers_cnt * fibers_len + 6));
                    }


                    for (int i = 0; i < fibers_cnt; i++) {
                        std::vector<std::pair<int, int>> fiber;
                        for (int j = 0; j < fibers_len; j++) {
                            fiber.push_back(std::make_pair(choose_sem(), choose_op()));
                        }

                        operations.push_back(fiber);
                    }

                    for (int i = 0; i < fibers_cnt; i++) {
                        to_do.push_back(execute(i, semaphores, operations[i].begin(), operations[i].end()));
                    }

                    return seastar::when_all(
                            to_do.begin(), to_do.end()).discard_result();
                });
    });
}
