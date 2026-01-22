#include "utils/thread_optimizer.h"
#include <sys/resource.h>
#include <sys/syscall.h>
#include <cstring>

namespace rm_auto_attack {

int ThreadOptimizer::getCPUCoreCount() {
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

int ThreadOptimizer::getMinPriority() {
    return sched_get_priority_min(SCHED_FIFO);
}

int ThreadOptimizer::getMaxPriority() {
    return sched_get_priority_max(SCHED_FIFO);
}

bool ThreadOptimizer::setCurrentThreadPriority(int priority, int cpuCore) {
    // 设置CPU亲和性
    if (cpuCore >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuCore, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
            return false;
        }
    }
    
    // 设置调度策略和优先级
    struct sched_param param;
    param.sched_priority = priority;
    
    // 尝试使用实时调度（需要root权限）
    if (sched_setscheduler(0, SCHED_FIFO, &param) == 0) {
        return true;
    }
    
    // 如果实时调度失败，使用普通优先级设置
    if (setpriority(PRIO_PROCESS, 0, priority) == 0) {
        return true;
    }
    
    return false;
}

bool ThreadOptimizer::setThreadPriority(std::thread& thread, int priority, int cpuCore) {
    // 获取线程的native handle
    pthread_t handle = thread.native_handle();
    
    // 设置CPU亲和性
    if (cpuCore >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuCore, &cpuset);
        if (pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset) != 0) {
            return false;
        }
    }
    
    // 设置调度策略和优先级
    struct sched_param param;
    param.sched_priority = priority;
    
    // 尝试使用实时调度
    if (pthread_setschedparam(handle, SCHED_FIFO, &param) == 0) {
        return true;
    }
    
    // 如果实时调度失败，使用普通优先级设置
    // 注意：pthread没有直接的setpriority，需要通过其他方式
    return false;
}

void ThreadOptimizer::setThreadName(const std::string& name) {
    // Linux特定的线程命名（最多16字符）
    std::string shortName = name.substr(0, 15);
    pthread_setname_np(pthread_self(), shortName.c_str());
}

} // namespace rm_auto_attack


