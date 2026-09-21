#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

template<typename T>
class SafeQueue{
    std::queue<T> queue;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic_bool shutdown_{false};

public:
    void push(T item){
        std::lock_guard<std::mutex> lock(mtx_);
        queue.push(std::move(item));
        cv_.notify_one();
    };
    bool pop(T& item){
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]{return !queue.empty() || shutdown_.load();});
        if(shutdown_ && queue.empty()) return false;
        item = std::move(queue.front());
        queue.pop();
        return true;
    };
    void stop(){
        std::lock_guard<std::mutex> lock(mtx_);
        shutdown_.store(true);
        cv_.notify_all();
    };
};