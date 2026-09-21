#include <catch2/catch_test_macros.hpp>
#include "Common/SafeQueue.hpp"

#include <thread>
#include <chrono>
#include <atomic>

using namespace std::chrono_literals;

TEST_CASE("push then pop returns the same item", "[safequeue]") {
    SafeQueue<int> q;
    q.push(42);

    int item = 0;
    bool ok = q.pop(item);

    REQUIRE(ok == true);
    REQUIRE(item == 42);
}

TEST_CASE("items come out in FIFO order", "[safequeue]") {
    SafeQueue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);

    int a = 0, b = 0, c = 0;
    q.pop(a);
    q.pop(b);
    q.pop(c);

    REQUIRE(a == 1);
    REQUIRE(b == 2);
    REQUIRE(c == 3);
}

TEST_CASE("pop blocks until an item is pushed", "[safequeue]") {
    SafeQueue<int> q;
    std::atomic<bool> popReturned{false};
    int item = 0;

    std::thread consumer([&]{
        q.pop(item);
        popReturned.store(true);
    });

    // give the consumer thread time to reach cv_.wait and go to sleep
    std::this_thread::sleep_for(50ms);
    REQUIRE(popReturned.load() == false); // still asleep, nothing pushed yet

    q.push(99);
    consumer.join();

    REQUIRE(popReturned.load() == true);
    REQUIRE(item == 99);
}

TEST_CASE("stop unblocks a waiting pop and returns false when empty", "[safequeue]") {
    SafeQueue<int> q;
    bool result = true; // start true so we can prove pop() actually set it to false

    std::thread consumer([&]{
        int item = 0;
        result = q.pop(item);
    });

    std::this_thread::sleep_for(50ms);
    q.stop();
    consumer.join();

    REQUIRE(result == false);
}

TEST_CASE("stop still lets pop drain a remaining item before quitting", "[safequeue]") {
    SafeQueue<int> q;
    q.push(7);
    q.stop();

    int item = 0;
    bool first = q.pop(item);  
    REQUIRE(first == true);
    REQUIRE(item == 7);

    bool second = q.pop(item);  
    REQUIRE(second == false);
}
