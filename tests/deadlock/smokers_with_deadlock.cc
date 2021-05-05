/*
 * Deadlock
 */
#include <iostream>

#include <seastar/core/app-template.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>

using namespace std::chrono_literals;

seastar::future<seastar::stop_iteration>
agentA(seastar::semaphore& agentSem, seastar::semaphore& tobacco, seastar::semaphore& paper, seastar::semaphore& matches) {
    return seastar::sleep(10ms).then([&agentSem] {
        return agentSem.wait();
    }).then([&tobacco, &paper] {
        std::cout << "AgentA giving" << std::endl;
        tobacco.signal();
        paper.signal();
        return seastar::stop_iteration::yes;
    });
}

seastar::future<seastar::stop_iteration>
agentB(seastar::semaphore& agentSem, seastar::semaphore& tobacco, seastar::semaphore& paper, seastar::semaphore& matches) {
    return seastar::sleep(11ms).then([&agentSem] {
        return agentSem.wait();
    }).then([&paper, &matches] {
        std::cout << "AgentB giving" << std::endl;
        paper.signal();
        matches.signal();
        return seastar::stop_iteration::yes;
    });
}

seastar::future<seastar::stop_iteration>
agentC(seastar::semaphore& agentSem, seastar::semaphore& tobacco, seastar::semaphore& paper, seastar::semaphore& matches) {
    return seastar::sleep(12ms).then([&agentSem] {
        return agentSem.wait();
    }).then([&matches, &tobacco] {
        std::cout << "AgentC giving" << std::endl;
        matches.signal();
        tobacco.signal();
        return seastar::stop_iteration::yes;
    });
}

seastar::future<seastar::stop_iteration>
matchesSmoker(seastar::semaphore& agentSem, seastar::semaphore& tobacco, seastar::semaphore& paper, seastar::semaphore& matches) {
    return seastar::sleep(11ms).then([&tobacco] {
        return tobacco.wait();
    }).then([] {
        std::cout << "matches has tobacco" << std::endl;
        return seastar::sleep(0s);
    }).then([&paper] {
        return paper.wait();
    }).then([&agentSem] {
        std::cout << "matches has paper" << std::endl;
        agentSem.signal();
        return seastar::stop_iteration::yes;
    });
}


seastar::future<seastar::stop_iteration>
tobaccoSmoker(seastar::semaphore& agentSem, seastar::semaphore& tobacco, seastar::semaphore& paper, seastar::semaphore& matches) {
    return seastar::sleep(12ms).then([&paper] {
        return paper.wait();
    }).then([] {
        std::cout << "tobacco has paper" << std::endl;
        return seastar::sleep(0s);
    }).then([&matches] {
        return matches.wait();
    }).then([&agentSem] {
        std::cout << "tobacco has matches" << std::endl;
        agentSem.signal();
        return seastar::stop_iteration::yes;
    });
}

seastar::future<seastar::stop_iteration>
paperSmoker(seastar::semaphore& agentSem, seastar::semaphore& tobacco, seastar::semaphore& paper, seastar::semaphore& matches) {
    return seastar::sleep(13ms).then([&matches] {
        return matches.wait();
    }).then([] {
        std::cout << "paper has matches" << std::endl;
        return seastar::sleep(0s);
    }).then([&tobacco] {
        return tobacco.wait();
    }).then([&agentSem] {
        std::cout << "paper has tobacco" << std::endl;
        agentSem.signal();
        return seastar::stop_iteration::yes;
    });
}

template <typename Func, typename... Args>
seastar::future<> Repeat(Func&& func, Args&& ... args) {
    return seastar::repeat([&func, &args...]() {
        return func(args...);
    });
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, [] {
        return seastar::do_with(seastar::semaphore(1), seastar::semaphore(0), seastar::semaphore(0), seastar::semaphore(0),
                [](auto& agentSem, auto& tobacco, auto& paper, auto& match) {
                    return seastar::when_all(Repeat(agentA, agentSem, tobacco, paper, match),
                            Repeat(agentB, agentSem, tobacco, paper, match),
                            Repeat(agentC, agentSem, tobacco, paper, match),
                            Repeat(matchesSmoker, agentSem, tobacco, paper, match),
                            Repeat(tobaccoSmoker, agentSem, tobacco, paper, match),
                            Repeat(paperSmoker, agentSem, tobacco, paper, match)).discard_result();
                });
    });
}
