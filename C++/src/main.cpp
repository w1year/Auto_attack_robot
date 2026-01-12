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
#include "detection/yolo_detector.h"
#include "gimbal/gimbal_controller.h"
#include "utils/logger.h"
#include "utils/config.h"

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
                    LOG_INFO("巡航: 到达左限制，转向右侧");
                } else if (currentAngle <= rightLimit) {
                    currentAngle = rightLimit;
                    direction = 1;
                    LOG_INFO("巡航: 到达右限制，转向左侧");
                }
                
                // 设置新角度
                gimbal->setYawAngle(currentAngle);
                g_currentYawAngle = currentAngle;
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
void detectionThread(MVSCamera* camera, YOLODetector* detector, GimbalController* gimbal) {
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
    
    // 跟踪参数
    int imageCenterX = 640;  // 假设图像中心 (实际应从相机获取)
    int stepSize = 25;       // 基本步长
    int centerMargin = 100;  // 中心区域范围
    
    // FPS计算
    int frameCount = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();
    double fps = 0.0;
    
    cv::Mat frame;
    
    while (g_running.load()) {
        try {
            auto startTime = std::chrono::steady_clock::now();
            
            // 获取一帧图像
            if (!camera->getFrame(frame, 1000)) {
                LOG_WARNING("获取帧失败");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            if (frame.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // 更新图像中心 (如果从相机获取尺寸)
            imageCenterX = frame.cols / 2;
            
            // 执行检测
            std::vector<Detection> allDetections = detector->detect(frame, 0.3f);
            
            // 计算FPS
            frameCount++;
            if (frameCount % 10 == 0) {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastFpsTime).count();
                if (elapsed > 0) {
                    fps = 10000.0 / elapsed;
                    lastFpsTime = currentTime;
                }
            }
            
            // 绘制检测结果
            cv::Mat resultFrame = frame.clone();
            std::string statusText = "Patrolling";
            std::string shootStatusText = "Standby";
            
            // 过滤目标
            std::vector<Detection> targetDetections;
            for (const auto& det : allDetections) {
                bool isTargetType = false;
                if (g_targetType == "red") {
                    isTargetType = (det.classId >= 5 && det.classId <= 9);  // red目标
                } else {
                    isTargetType = (det.classId >= 0 && det.classId <= 4);  // blue目标
                }
                
                if (isTargetType) {
                    targetDetections.push_back(det);
                }
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
                
                // 锁定目标
                {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    
                    if (!g_targetLock.load()) {
                        LOG_INFO("检测到" + g_targetType + "目标，类型: " + bestDet.className);
                        lockStartTime = currentTime;
                        g_shooting = false;
                    }
                    g_targetLock = true;
                    
                    // 设置俯仰角
                    auto it = ELEVATION_MAPPING.find(bestDet.className);
                    if (it != ELEVATION_MAPPING.end()) {
                        gimbal->setPicAngle(it->second);
                        gimbal->sendCommand();
                    }
                    
                    // 水平调整
                    if (!centerStable) {
                        int newAngle = g_currentYawAngle.load() + 
                                     (xDeviation > 0 ? -stepSize : stepSize);
                        newAngle = std::max(0, std::min(30000, newAngle));
                        gimbal->setYawAngle(newAngle);
                        g_currentYawAngle = newAngle;
                    }
                }
                
                statusText = centerStable ? "Centered" : "Adjusting";
                
                // 射击控制逻辑
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
                        LOG_INFO("开始射击" + g_targetType + "目标: " + bestDet.className);
                    } else if (g_shooting.load() && elapsedSincePulse >= shootPulseOn) {
                        std::lock_guard<std::mutex> lock(g_gimbalMutex);
                        gimbal->stopShoot();
                        g_shooting = false;
                        lastPulseSwitch = currentTime;
                        shootStatusText = "Pulse Off";
                        LOG_INFO("暂停脉冲射击");
                    } else {
                        shootStatusText = g_shooting.load() ? "Firing" : "Standby";
                    }
                } else {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    gimbal->stopShoot();
                    g_shooting = false;
                    shootStatusText = centerStable ? "Standby" : "Adjusting";
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
                // 处理目标丢失
                {
                    std::lock_guard<std::mutex> lock(g_gimbalMutex);
                    gimbal->stopShoot();
                    g_shooting = false;
                }
                
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
            
            // 显示FPS
            cv::putText(resultFrame, "FPS: " + std::to_string(fps),
                       cv::Point(frame.cols - 200, 50), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       cv::Scalar(0, 255, 0), 2);
            cv::putText(resultFrame, "Detections: " + std::to_string(allDetections.size()),
                       cv::Point(frame.cols - 200, 80), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       cv::Scalar(0, 255, 0), 2);
            
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
            
            // 显示窗口
            cv::imshow("Detection Results", resultFrame);
            
            // 键盘控制
            int key = cv::waitKey(1) & 0xFF;
            if (key == 'q') {
                LOG_INFO("用户按下q键，准备退出程序");
                g_running = false;
                break;
            }
            
            // 处理时间 (可用于性能分析)
            // auto procTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            //     std::chrono::steady_clock::now() - startTime).count();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
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
        LOG_INFO("加载目标检测模型...");
        YOLODetector detector;
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
        
        if (!detector.loadModel(modelPath)) {
            LOG_ERROR("模型加载失败，程序退出");
            return -1;
        }
        
        // 3. 初始化云台
        LOG_INFO("初始化云台控制...");
        GimbalController gimbal;
        if (!gimbal.initialize("/dev/ttyACM0", 115200)) {
            LOG_ERROR("云台初始化失败，程序退出");
            return -1;
        }
        
        // 4. 创建并启动线程
        LOG_INFO("启动工作线程...");
        std::thread detectionThreadObj(detectionThread, &camera, &detector, &gimbal);
        std::thread patrolThreadObj(patrolThread, &gimbal);
        
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
