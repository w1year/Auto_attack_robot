#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>
#include <vector>
#include <fstream>
#include <opencv2/opencv.hpp>

#include "camera/mvs_camera.h"
#include "detection/yolo_detector_tensorrt.h"
#include "gimbal/gimbal_controller.h"
#include "utils/logger.h"
#include "utils/config.h"
#include "utils/thread_optimizer.h"
#include "utils/frame_buffer.h"

using namespace rm_auto_attack;

// 全局变量
std::atomic<bool> g_running(true);
std::atomic<bool> g_patrolEnabled(true);
std::atomic<bool> g_targetLock(false);
std::atomic<bool> g_shooting(false);
std::mutex g_gimbalMutex;

// 当前偏航角度
std::atomic<int> g_currentYawAngle(15000);

// 目标类型 ("red" 或 "blue")
std::string g_targetType = "red";

// 映射表 - 标签到俯仰角度的映射 (根据Python代码中的ELEVATION_MAPPING)
const std::map<std::string, int> ELEVATION_MAPPING = {
    // blue目标的俯仰角映射
    {"blue100", 6000},
    {"blue200", 8500},
    {"blue300", 9500},
    {"blue400", 10000},
    {"blue500", 14500},
    
    // red目标的俯仰角映射
    {"red100", 8000},
    {"red200", 10000},
    {"red300", 14000},
    {"red400", 18000},
    {"red500", 20000}
};

// 选择目标类型
void selectTargetType() {
    std::cout << "\n=== 目标类型设置 ===" << std::endl;
    std::cout << "1. 红方目标" << std::endl;
    std::cout << "2. 蓝方目标" << std::endl;
    std::cout << "请选择目标类型 (1/2): ";
    
    int choice;
    std::cin >> choice;
    
    if (choice == 1) {
        g_targetType = "red";
        LOG_INFO("已选择红方目标模式");
    } else if (choice == 2) {
        g_targetType = "blue";
        LOG_INFO("已选择蓝方目标模式");
    } else {
        std::cout << "无效选择，使用默认值: 红方" << std::endl;
        g_targetType = "red";
    }
}

// 巡航线程
void patrolThread(GimbalController* gimbal) {
    // 设置线程优先级和名称（巡航线程使用正常优先级）
    ThreadOptimizer::setThreadName("PatrolThread");
    ThreadOptimizer::setCurrentThreadPriority(ThreadOptimizer::NORMAL, -1);
    
    LOG_INFO("启动云台巡航线程");
    
    // 巡航参数
    int centerAngle = 15000;  // 中心位置
    int patrolRange = 13000;  // 巡航范围
    int leftLimit = centerAngle + patrolRange;   // 左侧限制 (28000)
    int rightLimit = centerAngle - patrolRange;  // 右侧限制 (2000)
    
    // 巡航速度参数
    int patrolSpeed = 50;     // 每次移动步长
    double patrolDelay = 0.03; // 延迟时间（秒）
    
    // 平滑移动参数
    int accelerationZone = 500;  // 加减速区域
    int minSpeed = 30;           // 最小速度
    
    int currentAngle = centerAngle;
    int direction = 1;  // 1:向左, -1:向右
    
    // 角度输出控制（每秒输出一次）
    auto lastAngleLogTime = std::chrono::steady_clock::now();
    const auto angleLogInterval = std::chrono::seconds(1);
    
    while (g_running.load()) {
        try {
            // 只有在巡航启用且未锁定目标时才进行巡航
            if (g_patrolEnabled.load() && !g_targetLock.load()) {
                std::lock_guard<std::mutex> lock(g_gimbalMutex);
                
                // 计算距离边界的距离
                int distanceToLeft = leftLimit - currentAngle;
                int distanceToRight = currentAngle - rightLimit;
                
                // 根据接近边界的距离动态调整速度
                int currentSpeed = patrolSpeed;
                if (direction > 0 && distanceToLeft < accelerationZone) {
                    // 接近左边界，减速
                    double speedFactor = std::max(0.1, static_cast<double>(distanceToLeft) / accelerationZone);
                    currentSpeed = std::max(minSpeed, static_cast<int>(patrolSpeed * speedFactor));
                } else if (direction < 0 && distanceToRight < accelerationZone) {
                    // 接近右边界，减速
                    double speedFactor = std::max(0.1, static_cast<double>(distanceToRight) / accelerationZone);
                    currentSpeed = std::max(minSpeed, static_cast<int>(patrolSpeed * speedFactor));
                }
                
                // 计算新角度
                currentAngle += currentSpeed * direction;
                
                // 检查是否到达边界
                if (currentAngle >= leftLimit) {
                    currentAngle = leftLimit;
                    direction = -1;
                    // 移除边界日志，减少输出
                } else if (currentAngle <= rightLimit) {
                    currentAngle = rightLimit;
                    direction = 1;
                    // 移除边界日志，减少输出
                }
                
                // 设置新角度
                gimbal->setYawAngle(currentAngle);
                g_currentYawAngle = currentAngle;
                
                // 每秒输出一次角度信息（巡航时）
                auto now = std::chrono::steady_clock::now();
                int currentPicAngle = gimbal->getCurrentPicAngle();
                if (now - lastAngleLogTime >= angleLogInterval) {
                    // 每秒都输出角度信息
                    LOG_INFO("云台角度 - 俯仰角: " + std::to_string(currentPicAngle) + 
                            ", 偏航角: " + std::to_string(currentAngle));
                    lastAngleLogTime = now;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(patrolDelay * 1000)));
            
        } catch (const std::exception& e) {
            LOG_ERROR("巡航线程异常: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("云台巡航线程已停止");
}

// 检测线程
void detectionThread(MVSCamera* camera, YOLODetectorTensorRT* detector, GimbalController* gimbal) {
    // 设置线程优先级和名称（检测线程需要高优先级）
    ThreadOptimizer::setThreadName("DetectionThread");
    ThreadOptimizer::setCurrentThreadPriority(ThreadOptimizer::HIGH, -1); // 高优先级，不绑定CPU
    
    LOG_INFO("启动目标检测线程，当前目标类型：" + g_targetType);
    
    // 添加锁定稳定期
    auto targetLostTime = std::chrono::steady_clock::now();
    const double targetLostTimeout = 1.0;  // 目标丢失超时时间（秒）
    
    // 射击控制参数
    const double shootingDelay = 0.0;  // 锁定后开始射击的延迟（秒）
    auto lockStartTime = std::chrono::steady_clock::now();
    const double shootPulseOn = 0.4;   // 射击脉冲开启时间（秒）
    const double shootPulseOff = 0.2;  // 射击脉冲关闭时间（秒）
    auto lastPulseSwitch = std::chrono::steady_clock::now();
    
    // 创建窗口显示检测结果
    cv::namedWindow("Detection Results", cv::WINDOW_NORMAL);
    cv::resizeWindow("Detection Results", 1280, 720);
    
    // 从相机获取参数并计算图像中心
    CameraParams cameraParams = camera->getParams();
    int imageCenterX = cameraParams.width / 2;  // 从相机参数获取图像中心
    if (imageCenterX == 0) {
        // 如果相机参数未初始化，使用默认值（会在获取第一帧后更新）
        imageCenterX = 640;
        LOG_WARNING("相机参数未初始化，使用默认图像中心: 640");
    } else {
        LOG_INFO("从相机获取图像中心: " + std::to_string(imageCenterX) + 
                " (图像宽度: " + std::to_string(cameraParams.width) + ")");
    }
    
    // 跟踪参数
    int stepSize = 25;       // 基本步长
    int centerMargin = 100;  // 中心区域范围
    
    // FPS计算和控制
    const double targetFPS = 60.0;  // 目标帧率：60 FPS
    const double targetFrameTime = 1000.0 / targetFPS;  // 每帧目标时间（毫秒）：16.67ms
    const std::chrono::microseconds targetFrameDuration(
        static_cast<int>(targetFrameTime * 1000));  // 转换为微秒
    
    int frameCount = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();
    auto lastFrameTime = std::chrono::steady_clock::now();
    double fps = 0.0;
    
    // 推理时间统计
    double avgDetectTime = 0.0;
    
    // 性能测量统计（指数移动平均）
    double avgFrameAcquireTime = 0.0;      // 获取帧时间（包含Bayer转换，在getFrame内部）
    double avgDisplayTime = 0.0;           // 显示时间
    double avgTotalFrameTime = 0.0;        // 总帧时间
    
    // 性能日志输出控制（每1秒输出一次）
    auto lastPerfLogTime = std::chrono::steady_clock::now();
    const auto perfLogInterval = std::chrono::seconds(1);
    
    // 射击日志控制（只输出一次）
    static bool shootingLogPrinted = false;
    
    // 角度输出控制（检测到目标时每0.5秒输出一次）
    auto lastTargetAngleLogTime = std::chrono::steady_clock::now();
    const auto targetAngleLogInterval = std::chrono::milliseconds(500);  // 0.5秒
    
    cv::Mat frame;
    
    while (g_running.load()) {
        try {
            // 帧率控制：计算下一帧应该开始的时间
            auto frameStartTime = std::chrono::steady_clock::now();
            auto nextFrameTime = lastFrameTime + targetFrameDuration;
            
            // 如果当前时间还没到下一帧时间，等待（但要尽量短，提高利用率）
            if (frameStartTime < nextFrameTime) {
                auto waitTime = nextFrameTime - frameStartTime;
                // 如果等待时间 > 1ms，使用精确等待；否则立即处理
                if (waitTime > std::chrono::milliseconds(1)) {
                    std::this_thread::sleep_until(nextFrameTime);
                }
            }
            lastFrameTime = std::chrono::steady_clock::now();
            
            // 直接从相机获取BGR图像（相机会自动将Bayer转换为BGR）
            cv::Mat frame;
            
            // 测量获取帧时间
            auto frameAcquireStart = std::chrono::steady_clock::now();
            // 超时时间：如果是60fps需要16.67ms，如果是30fps需要33ms，使用40ms确保能获取到帧
            if (!camera->getFrame(frame, 40)) {
                // 不等待，立即继续下一次循环（提高利用率）
                continue;
            }
            auto frameAcquireTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameAcquireStart).count();
            
            if (frame.empty()) {
                // 不等待，立即继续
                continue;
            }
            
            // 更新图像中心（从实际帧中获取，确保准确性）
            imageCenterX = frame.cols / 2;
            
            // 注意：Bayer转换在getFrame内部完成，时间已包含在frameAcquireTime中
            // 如果需要单独测量，需要在camera->getFrame内部添加测量点
            
            // 验证图像格式（诊断用）
            if (frame.type() != CV_8UC3 || frame.channels() != 3) {
                LOG_ERROR("检测前图像格式错误: type=" + std::to_string(frame.type()) + 
                         ", channels=" + std::to_string(frame.channels()) + 
                         ", size=" + std::to_string(frame.cols) + "x" + std::to_string(frame.rows));
                continue;
            }
            
            // 执行检测（TensorRT推理 - GPU加速），使用BGR图像进行检测
            auto detectStart = std::chrono::steady_clock::now();
            std::vector<Detection> allDetections = detector->detect(frame, 0.3f);
            auto detectTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - detectStart).count();
            
            // 记录各项时间（指数移动平均）
            avgFrameAcquireTime = avgFrameAcquireTime * 0.9 + frameAcquireTime * 0.1;
            avgDetectTime = avgDetectTime * 0.9 + detectTime * 0.1;
            
            // 计算FPS（更精确的计算）
            frameCount++;
            auto fpsCheckTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                fpsCheckTime - lastFpsTime).count();
            
            if (elapsed >= 100) {  // 每100ms更新一次FPS
                fps = (frameCount * 1000.0) / elapsed;
                frameCount = 0;
                lastFpsTime = fpsCheckTime;
            }
            
            // 绘制检测结果（使用frame直接绘制，避免clone开销）
            cv::Mat resultFrame = frame;  // 使用引用，不复制
            std::string statusText = "Patrolling";
            std::string shootStatusText = "Standby";
            
            // 过滤目标（并行优化：使用更高效的过滤）
            std::vector<Detection> targetDetections;
            targetDetections.reserve(allDetections.size());  // 预分配内存
            
            int targetStartId = (g_targetType == "red") ? 5 : 0;
            int targetEndId = (g_targetType == "red") ? 9 : 4;
            
            // 诊断：记录检测到的目标数量（每5秒输出一次）
            static auto lastDetectLogTime = std::chrono::steady_clock::now();
            auto detectLogTime = std::chrono::steady_clock::now();
            if (detectLogTime - lastDetectLogTime >= std::chrono::seconds(5)) {
                // 统计各类别ID的数量
                std::map<int, int> classIdCount;
                for (const auto& det : allDetections) {
                    classIdCount[det.classId]++;
                }
                
                std::string classIdStats = "类别统计: ";
                for (const auto& pair : classIdCount) {
                    classIdStats += "ID" + std::to_string(pair.first) + "=" + std::to_string(pair.second) + " ";
                }
                
                LOG_INFO("检测统计: 总检测数=" + std::to_string(allDetections.size()) + 
                        ", 图像尺寸=" + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) +
                        ", 目标类型=" + g_targetType +
                        ", 过滤范围=[" + std::to_string(targetStartId) + "-" + std::to_string(targetEndId) + "]");
                LOG_INFO(classIdStats);
                lastDetectLogTime = detectLogTime;
            }
            
            std::copy_if(allDetections.begin(), allDetections.end(),
                        std::back_inserter(targetDetections),
                        [targetStartId, targetEndId](const Detection& det) {
                            return det.classId >= targetStartId && det.classId <= targetEndId;
                        });
            
            // 诊断：记录过滤后的目标数量（每5秒输出一次）
            static auto lastFilterLogTime = std::chrono::steady_clock::now();
            auto filterLogTime = std::chrono::steady_clock::now();
            if (filterLogTime - lastFilterLogTime >= std::chrono::seconds(5)) {
                LOG_INFO("过滤统计: 过滤前=" + std::to_string(allDetections.size()) + 
                        ", 过滤后=" + std::to_string(targetDetections.size()) +
                        ", 目标类型=" + g_targetType);
                lastFilterLogTime = filterLogTime;
            }
            
            auto currentTime = std::chrono::steady_clock::now();
            
            if (!targetDetections.empty()) {
                // 找到置信度最高的目标
                Detection bestDet = *std::max_element(
                    targetDetections.begin(), targetDetections.end(),
                    [](const Detection& a, const Detection& b) {
                        return a.confidence < b.confidence;
                    });
                
                // 计算目标中心位置
                int targetCenterX = (bestDet.x1 + bestDet.x2) / 2;
                int targetCenterY = (bestDet.y1 + bestDet.y2) / 2;
                int xDeviation = targetCenterX - imageCenterX;
                
                // 判断是否在中心区域
                bool centerStable = std::abs(xDeviation) <= centerMargin;
                
                // 诊断：记录目标信息（每2秒输出一次）
                static auto lastTargetLogTime = std::chrono::steady_clock::now();
                auto targetLogTime = std::chrono::steady_clock::now();
                if (targetLogTime - lastTargetLogTime >= std::chrono::seconds(2)) {
                    LOG_INFO("目标信息: className=" + bestDet.className + 
                            ", classId=" + std::to_string(bestDet.classId) +
                            ", confidence=" + std::to_string(bestDet.confidence) +
                            ", 位置=(" + std::to_string(targetCenterX) + "," + std::to_string(targetCenterY) + ")" +
                            ", 图像中心=" + std::to_string(imageCenterX) +
                            ", 偏差=" + std::to_string(xDeviation) +
                            ", 中心稳定=" + (centerStable ? "是" : "否"));
                    lastTargetLogTime = targetLogTime;
                }
                
                // 锁定目标（优化：减少锁持有时间）
                {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    
                    if (!g_targetLock.load()) {
                        // 移除检测到目标的日志，减少输出（只在开始射击时输出）
                        lockStartTime = currentTime;
                        g_shooting = false;
                        shootingLogPrinted = false;  // 重置射击日志标志
                    }
                    g_targetLock = true;
                }  // 锁范围结束
                
                // 云台控制（在锁外执行，减少锁竞争）
                auto it = ELEVATION_MAPPING.find(bestDet.className);
                if (it != ELEVATION_MAPPING.end()) {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    gimbal->setPicAngle(it->second);
                    gimbal->sendCommand();
                } else {
                    // 诊断：如果找不到映射，输出警告
                    static auto lastMappingWarnTime = std::chrono::steady_clock::now();
                    auto mappingWarnTime = std::chrono::steady_clock::now();
                    if (mappingWarnTime - lastMappingWarnTime >= std::chrono::seconds(5)) {
                        LOG_WARNING("未找到className映射: " + bestDet.className + 
                                   " (classId=" + std::to_string(bestDet.classId) + ")");
                        lastMappingWarnTime = mappingWarnTime;
                    }
                }
                
                // 水平调整（在锁外计算，减少锁持有时间）
                if (!centerStable) {
                    int newAngle = g_currentYawAngle.load() + 
                                 (xDeviation > 0 ? -stepSize : stepSize);
                    newAngle = std::max(0, std::min(30000, newAngle));
                    {
                        std::lock_guard<std::mutex> lock(g_gimbalMutex);
                        gimbal->setYawAngle(newAngle);
                        g_currentYawAngle = newAngle;
                    }
                }
                
                // 每0.5秒输出一次角度信息（检测到目标时）
                auto now = std::chrono::steady_clock::now();
                if (now - lastTargetAngleLogTime >= targetAngleLogInterval) {
                    int currentPicAngle = gimbal->getCurrentPicAngle();
                    int currentYawAngle = g_currentYawAngle.load();
                    // 每0.5秒都输出角度信息
                    LOG_INFO("云台角度 - 俯仰角: " + std::to_string(currentPicAngle) + 
                            ", 偏航角: " + std::to_string(currentYawAngle));
                    lastTargetAngleLogTime = now;
                }
                
                statusText = centerStable ? "Centered" : "Adjusting";
                
                // 射击控制逻辑（只在检测到目标时输出）
                auto elapsedSinceLock = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lockStartTime).count() / 1000.0;
                
                if (centerStable && elapsedSinceLock >= shootingDelay) {
                    auto elapsedSincePulse = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentTime - lastPulseSwitch).count() / 1000.0;
                    
                    if (!g_shooting.load() && elapsedSincePulse >= shootPulseOff) {
                        std::lock_guard<std::mutex> lock(g_gimbalMutex);
                        gimbal->triggerShoot();
                        g_shooting = true;
                        lastPulseSwitch = currentTime;
                        shootStatusText = "Firing";
                        // 只在开始射击时输出一次（检测到目标时）
                        if (!shootingLogPrinted) {
                            LOG_INFO("开始射击" + g_targetType + "目标: " + bestDet.className);
                            shootingLogPrinted = true;
                        }
                    } else if (g_shooting.load() && elapsedSincePulse >= shootPulseOn) {
                        std::lock_guard<std::mutex> lock(g_gimbalMutex);
                        gimbal->stopShoot();
                        g_shooting = false;
                        lastPulseSwitch = currentTime;
                        shootStatusText = "Pulse Off";
                        // 不输出暂停射击日志（减少输出）
                    } else {
                        shootStatusText = g_shooting.load() ? "Firing" : "Standby";
                    }
                } else {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    gimbal->stopShoot();
                    g_shooting = false;
                    shootStatusText = centerStable ? "Standby" : "Adjusting";
                    // 如果不在中心区域，重置射击日志标志
                    if (!centerStable) {
                        shootingLogPrinted = false;
                    }
                }
                
                // 绘制目标框
                cv::rectangle(resultFrame, 
                            cv::Point(bestDet.x1, bestDet.y1),
                            cv::Point(bestDet.x2, bestDet.y2),
                            cv::Scalar(0, 255, 0), 2);
                
                std::string label = bestDet.className + " " + std::to_string(bestDet.confidence);
                cv::putText(resultFrame, label,
                          cv::Point(bestDet.x1, bestDet.y1 - 10),
                          cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
                
                // 绘制目标中心
                cv::circle(resultFrame, cv::Point(targetCenterX, targetCenterY), 
                         10, cv::Scalar(0, 0, 255), -1);
                
            } else {
                // 处理目标丢失（没有检测到目标，不输出射击相关信息）
                {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    gimbal->stopShoot();
                    g_shooting = false;
                }
                
                // 重置射击日志标志（目标丢失时）
                shootingLogPrinted = false;
                
                if (g_targetLock.load()) {
                    auto elapsedSinceLost = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentTime - targetLostTime).count() / 1000.0;
                    
                    if (elapsedSinceLost == 0) {
                        targetLostTime = currentTime;
                        statusText = "Target Lost";
                    } else if (elapsedSinceLost > targetLostTimeout) {
                        g_targetLock = false;
                        lockStartTime = currentTime;
                        statusText = "Patrolling";
                    } else {
                        statusText = "Confirming Loss (" + 
                                   std::to_string(targetLostTimeout - elapsedSinceLost) + "s)";
                    }
                } else {
                    targetLostTime = currentTime;
                    statusText = "Patrolling";
                }
                shootStatusText = "Standby";
            }
            
            // 显示信息
            cv::putText(resultFrame, "Target: " + (targetDetections.empty() ? 
                       "None" : targetDetections[0].className),
                       cv::Point(50, 50), cv::FONT_HERSHEY_SIMPLEX, 1,
                       cv::Scalar(0, 255, 0), 2);
            cv::putText(resultFrame, "Status: " + statusText,
                       cv::Point(50, 100), cv::FONT_HERSHEY_SIMPLEX, 1,
                       cv::Scalar(0, 255, 0), 2);
            cv::putText(resultFrame, "Weapon: " + shootStatusText,
                       cv::Point(50, 150), cv::FONT_HERSHEY_SIMPLEX, 1,
                       cv::Scalar(0, 0, 255), 2);
            
            // 显示FPS和性能信息
            std::string fpsText = "FPS: " + std::to_string(static_cast<int>(fps)) + 
                                 " / " + std::to_string(static_cast<int>(targetFPS));
            cv::Scalar fpsColor = (fps >= targetFPS * 0.9) ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 165, 255);
            cv::putText(resultFrame, fpsText,
                       cv::Point(frame.cols - 300, 50), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       fpsColor, 2);
            cv::putText(resultFrame, "Detections: " + std::to_string(allDetections.size()),
                       cv::Point(frame.cols - 300, 80), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       cv::Scalar(0, 255, 0), 2);
            cv::putText(resultFrame, "Inference: " + std::to_string(static_cast<int>(avgDetectTime / 1000.0)) + "ms",
                       cv::Point(frame.cols - 300, 110), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       cv::Scalar(0, 255, 255), 2);
            
            // 显示帧时间（用于性能监控）
            auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameStartTime).count();
            std::string frameTimeText = "Frame: " + std::to_string(static_cast<int>(frameTime / 1000.0)) + 
                                       "ms / " + std::to_string(static_cast<int>(targetFrameTime)) + "ms";
            cv::Scalar frameTimeColor = (frameTime < targetFrameDuration.count()) ? 
                                       cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::putText(resultFrame, frameTimeText,
                       cv::Point(frame.cols - 300, 140), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       frameTimeColor, 2);
            
            // 绘制中心线
            cv::line(resultFrame, 
                    cv::Point(imageCenterX, 0),
                    cv::Point(imageCenterX, frame.rows),
                    cv::Scalar(255, 0, 0), 2);
            
            // 显示俯仰角
            cv::putText(resultFrame, "Elevation: " + std::to_string(gimbal->getCurrentPicAngle()),
                       cv::Point(50, 200), cv::FONT_HERSHEY_SIMPLEX, 1,
                       cv::Scalar(0, 255, 0), 2);
            
            // 显示当前目标类型
            cv::putText(resultFrame, "Target Type: " + g_targetType,
                       cv::Point(50, 250), cv::FONT_HERSHEY_SIMPLEX, 1,
                       cv::Scalar(0, 255, 0), 2);
            
            // 测量显示时间
            auto displayStart = std::chrono::steady_clock::now();
            
            // 显示窗口（确保resultFrame有效）
            if (!resultFrame.empty() && resultFrame.cols > 0 && resultFrame.rows > 0) {
                cv::imshow("Detection Results", resultFrame);
            }
            
            // 键盘控制
            int key = cv::waitKey(1) & 0xFF;
            if (key == 'q') {
                // 移除退出日志，减少输出
                g_running = false;
                break;
            }
            
            auto displayTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - displayStart).count();
            avgDisplayTime = avgDisplayTime * 0.9 + displayTime * 0.1;
            
            // 计算总帧时间
            auto totalFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameStartTime).count();
            avgTotalFrameTime = avgTotalFrameTime * 0.9 + totalFrameTime * 0.1;
            
            // 每1秒输出一次性能统计到日志
            auto now = std::chrono::steady_clock::now();
            if (now - lastPerfLogTime >= perfLogInterval) {
                LOG_INFO("性能统计 | "
                        "获取帧: " + std::to_string(static_cast<int>(avgFrameAcquireTime / 1000.0)) + "ms | "
                        "推理: " + std::to_string(static_cast<int>(avgDetectTime / 1000.0)) + "ms | "
                        "显示: " + std::to_string(static_cast<int>(avgDisplayTime / 1000.0)) + "ms | "
                        "总帧时间: " + std::to_string(static_cast<int>(avgTotalFrameTime / 1000.0)) + "ms | "
                        "FPS: " + std::to_string(static_cast<int>(fps)));
                lastPerfLogTime = now;
            }
            
            // 不再使用sleep，让循环尽可能快地执行（提高利用率）
            // 帧率控制已通过循环开始时的sleep_until实现
            
        } catch (const std::exception& e) {
            LOG_ERROR("目标检测线程异常: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    // 清理资源
    {
        std::lock_guard<std::mutex> lock(g_gimbalMutex);
        gimbal->stopShoot();
        g_shooting = false;
    }
    
    cv::destroyAllWindows();
    LOG_INFO("目标检测线程已停止");
}

// 主函数
int main(int /*argc*/, char* /*argv*/[]) {
    LOG_INFO("=== 目标检测与云台控制系统启动 ===");
    
    // 设置日志级别和日志文件
    Logger::getInstance().setLogLevel(LogLevel::INFO);
    Logger::getInstance().setLogFile("rm_auto_attack.log");
    
    // 选择目标类型
    selectTargetType();
    LOG_INFO("系统目标类型设置为: " + g_targetType);
    
    try {
        // 1. 初始化相机
        LOG_INFO("正在初始化相机...");
        MVSCamera camera;
        if (!camera.initialize(0)) {
            LOG_ERROR("相机初始化失败，程序退出");
            return -1;
        }
        
        if (!camera.startGrabbing()) {
            LOG_ERROR("相机开始采集失败，程序退出");
            return -1;
        }
        
        // 2. 加载模型
        LOG_INFO("加载目标检测模型（使用TensorRT加速）...");
        YOLODetectorTensorRT detector;
        
        // 尝试多个可能的模型路径
        std::vector<std::string> possiblePaths = {
            "best.onnx",                                    // 当前目录
            "../best.onnx",                                 // 上一级目录
            "../../best.onnx",                              // 项目根目录
            "../../blue_detect/best.onnx",                  // blue_detect目录
            "../../RmControl/best.onnx",                    // RmControl目录
            "config/../best.onnx"                           // 相对于配置目录
        };
        
        std::string modelPath;
        bool found = false;
        for (const auto& path : possiblePaths) {
            std::ifstream testFile(path);
            if (testFile.good()) {
                modelPath = path;
                found = true;
                testFile.close();
                LOG_INFO("找到模型文件: " + modelPath);
                break;
            }
            testFile.close();
        }
        
        if (!found) {
            LOG_ERROR("未找到模型文件 best.onnx");
            LOG_ERROR("请将 best.onnx 文件放置在以下任一位置:");
            for (const auto& path : possiblePaths) {
                LOG_ERROR("  - " + path);
            }
            LOG_ERROR("提示: 如果只有 .pt 文件，请先转换为 .onnx 格式:");
            LOG_ERROR("  from ultralytics import YOLO");
            LOG_ERROR("  model = YOLO('best.pt')");
            LOG_ERROR("  model.export(format='onnx', imgsz=640)");
            return -1;
        }
        
        // 加载TensorRT模型（启用FP16加速）
        LOG_INFO("加载TensorRT模型（首次运行会转换ONNX到TensorRT引擎，可能需要几分钟）...");
        if (!detector.loadModel(modelPath, true)) {  // true = 使用FP16加速
            LOG_ERROR("TensorRT模型加载失败，程序退出");
            return -1;
        }
        
        // 预热引擎（提高首次推理速度）
        LOG_INFO("预热TensorRT引擎...");
        detector.warmup(10);
        LOG_INFO("✓ TensorRT引擎准备就绪");
        
        // 3. 初始化云台
        LOG_INFO("初始化云台控制...");
        GimbalController gimbal;
        if (!gimbal.initialize("/dev/ttyACM0", 115200)) {
            LOG_ERROR("云台初始化失败，程序退出");
            return -1;
        }
        
        // 4. 创建并启动线程（优化线程配置）
        LOG_INFO("启动工作线程（优化配置）...");
        
        int cpuCores = ThreadOptimizer::getCPUCoreCount();
        LOG_INFO("检测到 " + std::to_string(cpuCores) + " 个CPU核心");
        
        std::thread detectionThreadObj(detectionThread, &camera, &detector, &gimbal);
        std::thread patrolThreadObj(patrolThread, &gimbal);
        
        // 设置检测线程为高优先级（如果可能）
        ThreadOptimizer::setThreadPriority(detectionThreadObj, ThreadOptimizer::HIGH);
        ThreadOptimizer::setThreadPriority(patrolThreadObj, ThreadOptimizer::NORMAL);
        
        LOG_INFO("✓ 线程优先级已设置（检测线程：高优先级，巡航线程：正常优先级）");
        
        // 5. 等待用户中断
        LOG_INFO("系统运行中，按Ctrl+C或q键退出...");
        
        try {
            while (g_running.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        } catch (...) {
            LOG_INFO("接收到中断信号，准备退出...");
            g_running = false;
        }
        
        // 6. 等待线程结束
        if (detectionThreadObj.joinable()) {
            detectionThreadObj.join();
        }
        if (patrolThreadObj.joinable()) {
            patrolThreadObj.join();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("程序运行出错: " + std::string(e.what()));
        return -1;
    }
    
    // 清理资源
    LOG_INFO("清理系统资源...");
    g_running = false;
    
    // 确保停止射击
    g_shooting = false;
    
    LOG_INFO("=== 系统已停止 ===");
    return 0;
}
