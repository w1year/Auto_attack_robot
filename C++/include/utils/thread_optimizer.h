#pragma once

#include <thread>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <string>

namespace rm_auto_attack {

class ThreadOptimizer {
public:
    // 设置线程优先级和CPU亲和性
    static bool setThreadPriority(std::thread& thread, int priority, int cpuCore = -1);
    
    // 设置当前线程优先级
    static bool setCurrentThreadPriority(int priority, int cpuCore = -1);
    
    // 获取CPU核心数
    static int getCPUCoreCount();
    
    // 设置线程名称（用于调试）
    static void setThreadName(const std::string& name);
    
    // 获取线程优先级范围
    static int getMinPriority();
    static int getMaxPriority();
    
    // 推荐的优先级设置
    enum Priority {
        LOW = 0,
        NORMAL = 0,
        HIGH = -10,
        REALTIME = -20
    };
};

} // namespace rm_auto_attack


