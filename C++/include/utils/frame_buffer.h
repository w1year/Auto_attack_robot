#pragma once

#include <opencv2/opencv.hpp>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace rm_auto_attack {

// 线程安全的帧缓冲区（用于流水线处理）
template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(item);
        m_condition.notify_one();
    }
    
    bool tryPop(T& item, int timeoutMs = 0) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (timeoutMs > 0) {
            if (!m_condition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                      [this] { return !m_queue.empty() || !m_running; })) {
                return false;
            }
        } else {
            m_condition.wait(lock, [this] { return !m_queue.empty() || !m_running; });
        }
        
        if (m_queue.empty() || !m_running) {
            return false;
        }
        
        item = m_queue.front();
        m_queue.pop();
        return true;
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        m_condition.notify_all();
    }
    
    void start() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = true;
    }

private:
    mutable std::mutex m_mutex;
    std::queue<T> m_queue;
    std::condition_variable m_condition;
    std::atomic<bool> m_running{true};
};

// 帧数据结构
struct FrameData {
    cv::Mat frame;
    std::chrono::steady_clock::time_point timestamp;
    
    FrameData() : timestamp(std::chrono::steady_clock::now()) {}
    FrameData(const cv::Mat& img) : frame(img.clone()), timestamp(std::chrono::steady_clock::now()) {}
};

} // namespace rm_auto_attack

