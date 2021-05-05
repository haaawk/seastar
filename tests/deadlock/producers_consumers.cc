#include <sys/types.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/sleep.hh>
#include <iostream>
#include <boost/iterator/counting_iterator.hpp>

#define CONSUMERS 2
#define PRODUCERS 2

seastar::future<> Produce(seastar::semaphore& mutex, seastar::semaphore& items) {
    using namespace std::chrono_literals;

    return seastar::sleep(1s).then([&mutex, &items] {
        std::cerr << "Producer waits for access to buffer" << (&mutex) << "... waiters:" << mutex.waiters() << std::endl;
        return seastar::get_units(mutex, 1).then([&items](auto units) {
            std::cerr << "Producer puts an item" << (&items) << "... waiters:" << items.waiters() << std::endl;
            items.signal(1);
            std::cerr << "New item is available." << std::endl;
            return seastar::make_ready_future<>();
        });
    });
}

seastar::future<> Consume(seastar::semaphore& mutex, seastar::semaphore& items) {
    using namespace std::chrono_literals;

    return seastar::sleep(2s).then([&mutex, &items] {
        std::cerr << "Consumer waits for access to buffer" << (&mutex) << "... waiters:" << mutex.waiters() << std::endl;
        return seastar::get_units(mutex, 1).then([&items](auto units) {
            std::cerr << "Consumer waits for item" << (&items) << "... waiters:" << items.waiters() << std::endl;
            return items.wait(1).finally([units = std::move(units)] {
                std::cerr << "Consumer ready to consume" << std::endl;
                // Units should be destroyed before sleep
            });
        });
    });
}

seastar::future<> Consumers(seastar::semaphore& mutex, seastar::semaphore& items) {
    return seastar::parallel_for_each(boost::counting_iterator(0), boost::counting_iterator(CONSUMERS), [&mutex, &items](int) {
        return seastar::do_for_each(boost::counting_iterator(0), boost::counting_iterator(2), [&mutex, &items](int) {
            return Consume(mutex, items);
        });
    });
}

seastar::future<> Producers(seastar::semaphore& mutex, seastar::semaphore& items) {
    return seastar::parallel_for_each(boost::counting_iterator(0), boost::counting_iterator(PRODUCERS), [&mutex, &items](int) {
        return seastar::do_for_each(boost::counting_iterator(0), boost::counting_iterator(2), [&mutex, &items](int) {
            return Produce(mutex, items);
        });
    });
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        static thread_local seastar::semaphore mutex(1);
        static thread_local seastar::semaphore items(0);

        return seastar::when_all(
                Consumers(mutex, items),
                Producers(mutex, items)).discard_result();
    });
}
