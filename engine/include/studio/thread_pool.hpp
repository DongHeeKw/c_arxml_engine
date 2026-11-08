#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace studio {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t worker_count) {
        if (worker_count == 0) {
            throw std::invalid_argument("ThreadPool requires at least one worker");
        }
        workers_.reserve(worker_count);
        try {
            for (std::size_t index = 0; index < worker_count; ++index) {
                workers_.emplace_back([this] { run(); });
            }
        } catch (...) {
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
            }
            ready_.notify_all();
            for (auto& worker : workers_) {
                if (worker.joinable()) worker.join();
            }
            throw;
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    template <typename Function>
    auto submit(Function&& function) -> std::future<std::invoke_result_t<Function>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::forward<Function>(function));
        auto result = task->get_future();
        {
            std::lock_guard lock(mutex_);
            if (stopping_) throw std::runtime_error("Cannot submit work to a stopped ThreadPool");
            tasks_.emplace([task] { (*task)(); });
        }
        ready_.notify_one();
        return result;
    }

private:
    void run() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};

} // namespace studio
