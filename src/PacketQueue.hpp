#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <cstdint>
#include <vector>
namespace netgate {
struct Packet {
    uint32_t id;
    uint32_t size;    
    uint64_t timestamp_ns; 
};
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue(size_t max_capacity = 1000) 
        : max_capacity_(max_capacity) {}
    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_not_full_.wait(lock, [this]() {
            return queue_.size() < max_capacity_;
        });
        queue_.push(std::move(item));
        cv_not_empty_.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_not_empty_.wait(lock, [this]() {
            return !queue_.empty();
        });
        T item = std::move(queue_.front());
        queue_.pop();
        cv_not_full_.notify_one();
        return item;
    }
private:
    std::queue<T> queue_;
    size_t max_capacity_;
    std::mutex mutex_;
    std::condition_variable cv_not_empty_; 
    std::condition_variable cv_not_full_;  
};
} 
