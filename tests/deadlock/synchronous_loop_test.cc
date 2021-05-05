#include <iostream>

#include <boost/iterator/counting_iterator.hpp>

#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/sleep.hh>

seastar::future<> take_twice(seastar::semaphore& sem, int count, bool do_sleep) {
    using namespace std::chrono_literals;

    std::function<seastar::future<void>()> sleep = [do_sleep]() {
        if (do_sleep) {
            return seastar::sleep(1ms);
        } else {
            return seastar::make_ready_future<>();
        }
    };

    return sleep().then([&sem, count] {
        return sem.wait(count);
    }).then([sleep] {
        return sleep();
    }).then([&sem, count] {
        return sem.wait(count);
    }).then([sleep] {
        return sleep();
    }).then([&sem, sleep, count] {
        sem.signal(count * 2);
        return sleep();
    });
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        static thread_local seastar::semaphore sem(100);

        using counting_iterator = boost::counting_iterator<size_t>;

        return seastar::make_ready_future<>().then([] {
            return seastar::do_for_each(counting_iterator(0), counting_iterator(3), [sem_ptr = &sem](size_t) {
                return take_twice(*sem_ptr, 40, true);
            });
        }).then([] {
            return seastar::do_for_each(counting_iterator(0), counting_iterator(3), [sem_ptr = &sem](size_t) {
                return take_twice(*sem_ptr, 41, false);
            });
        }).then([] {
            return seastar::do_with(0, [](auto& counter) {
                return seastar::repeat([sem_ptr = &sem, &counter]() {
                    return take_twice(*sem_ptr, 42, true).then([&counter] {
                        counter++;
                        if (counter == 3) {
                            return seastar::stop_iteration::yes;
                        } else {
                            return seastar::stop_iteration::no;
                        }
                    });
                });
            });
        }).then([] {
            return seastar::do_with(0, [](auto& counter) {
                return seastar::repeat([sem_ptr = &sem, &counter]() {
                    return take_twice(*sem_ptr, 43, false).then([&counter] {
                        counter++;
                        if (counter == 3) {
                            return seastar::stop_iteration::yes;
                        } else {
                            return seastar::stop_iteration::no;
                        }
                    });
                });
            });
        }).then([] {
            return seastar::do_with(0, [](auto& counter) {
                return seastar::do_until([&counter] { return counter++ == 3; }, [sem_ptr = &sem]() {
                    return take_twice(*sem_ptr, 44, true);
                });
            });
        }).then([] {
            return seastar::do_with(0, [](auto& counter) {
                return seastar::do_until([&counter] { return counter++ == 3; }, [sem_ptr = &sem]() {
                    return take_twice(*sem_ptr, 45, false);
                });
            });
        });
    });
}
