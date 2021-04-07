#include <sys/types.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/sleep.hh>
#include <iostream>
#include <boost/iterator/counting_iterator.hpp>

seastar::future<> Philosopher(int id, seastar::semaphore& left, seastar::semaphore& right) {
	using namespace std::chrono_literals;
	
	return seastar::sleep(std::chrono::seconds(id)).then([&left, &right, id]{
    		std::cerr << "Philospher " << id << " waits on " << (&left) << "... waiters:" << left.waiters() << std::endl;
		return left.wait(1).then([&left, &right, id] {
    			std::cerr << "Philosopher " << id << " waits on" << (&right) << "... waiters: " << right.waiters() << std::endl;
    			return right.wait(1).then([&left, &right, id] {
	    			std::cerr << "Philosopher " << id << " finishes." << std::endl;
	    			return seastar::make_ready_future<>();
	    		});
	    	});
    	});
}

seastar::future<> EatInLoop(int id, seastar::semaphore& left, seastar::semaphore& right) {
	return seastar::do_for_each(boost::counting_iterator(0), boost::counting_iterator(1), [&left, &right, id] (int) {
		return Philosopher(id, left, right).finally([&left, &right] {
			    left.signal(1);
	    		right.signal(1);
		});
	});
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
    	static thread_local seastar::semaphore fst_chopstick(1);
    	static thread_local seastar::semaphore snd_chopstick(1);
    	static thread_local seastar::semaphore thrd_chopstick(1);
    	
		return seastar::when_all(
			EatInLoop(0, fst_chopstick, snd_chopstick), 
			EatInLoop(1, snd_chopstick, thrd_chopstick),
			EatInLoop(2, thrd_chopstick, fst_chopstick)).discard_result();
    });
}
