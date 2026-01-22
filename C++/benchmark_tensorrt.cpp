#include <opencv2/opencv.hpp>
#include "detection/yolo_detector_tensorrt.h"
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace rm_auto_attack;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "使用方法: " << argv[0] << " <model_path> [iterations]" << std::endl;
        return -1;
    }
    
    std::string modelPath = argv[1];
    int iterations = (argc >= 3) ? std::stoi(argv[2]) : 100;
    
    std::cout << "=== TensorRT性能基准测试 ===" << std::endl;
    std::cout << "模型路径: " << modelPath << std::endl;
    std::cout << "迭代次数: " << iterations << std::endl;
    std::cout << std::endl;
    
    // 创建检测器
    YOLODetectorTensorRT detector;
    
    // 加载模型
    std::cout << "加载模型..." << std::endl;
    auto loadStart = std::chrono::steady_clock::now();
    if (!detector.loadModel(modelPath, true)) {  // FP16
        std::cerr << "模型加载失败!" << std::endl;
        return -1;
    }
    auto loadTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadStart).count();
    std::cout << "模型加载时间: " << loadTime << " ms" << std::endl;
    std::cout << std::endl;
    
    // 创建测试图像
    cv::Mat testImage = cv::Mat::zeros(640, 640, CV_8UC3);
    cv::randu(testImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    
    // 预热
    std::cout << "预热引擎..." << std::endl;
    detector.warmup(10);
    std::cout << "预热完成" << std::endl;
    std::cout << std::endl;
    
    // 性能测试
    std::cout << "开始性能测试..." << std::endl;
    std::vector<double> times;
    times.reserve(iterations);
    
    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::steady_clock::now();
        auto detections = detector.detect(testImage, 0.3f);
        auto end = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        times.push_back(duration / 1000.0);  // 转换为毫秒
        
        if ((i + 1) % 10 == 0) {
            std::cout << "完成 " << (i + 1) << " / " << iterations << " 次迭代" << std::endl;
        }
    }
    
    // 统计结果
    double sum = 0.0;
    double minTime = times[0];
    double maxTime = times[0];
    
    for (double t : times) {
        sum += t;
        minTime = std::min(minTime, t);
        maxTime = std::max(maxTime, t);
    }
    
    double avgTime = sum / times.size();
    
    // 计算中位数
    std::sort(times.begin(), times.end());
    double medianTime = times[times.size() / 2];
    
    // 计算百分位数
    double p95 = times[static_cast<int>(times.size() * 0.95)];
    double p99 = times[static_cast<int>(times.size() * 0.99)];
    
    // 计算标准差
    double variance = 0.0;
    for (double t : times) {
        variance += (t - avgTime) * (t - avgTime);
    }
    double stdDev = std::sqrt(variance / times.size());
    
    // 计算FPS
    double avgFPS = 1000.0 / avgTime;
    double maxFPS = 1000.0 / minTime;
    
    // 输出结果
    std::cout << std::endl;
    std::cout << "=" << std::setfill('=') << std::setw(60) << "" << std::endl;
    std::cout << "性能测试结果" << std::endl;
    std::cout << "=" << std::setfill('=') << std::setw(60) << "" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "平均推理时间: " << avgTime << " ms" << std::endl;
    std::cout << "最小推理时间: " << minTime << " ms" << std::endl;
    std::cout << "最大推理时间: " << maxTime << " ms" << std::endl;
    std::cout << "中位数时间:   " << medianTime << " ms" << std::endl;
    std::cout << "P95时间:      " << p95 << " ms" << std::endl;
    std::cout << "P99时间:      " << p99 << " ms" << std::endl;
    std::cout << "标准差:       " << stdDev << " ms" << std::endl;
    std::cout << std::endl;
    std::cout << "平均FPS:      " << avgFPS << " FPS" << std::endl;
    std::cout << "最大FPS:      " << maxFPS << " FPS" << std::endl;
    std::cout << std::endl;
    
    // 性能评估
    std::cout << "性能评估:" << std::endl;
    if (avgTime < 16.67) {
        std::cout << "  ✓ 可以维持60 FPS (要求 < 16.67ms)" << std::endl;
    } else if (avgTime < 33.33) {
        std::cout << "  ⚠️  可以维持30 FPS，但无法达到60 FPS" << std::endl;
        std::cout << "     建议: 降低模型输入尺寸或使用更小的模型" << std::endl;
    } else {
        std::cout << "  ✗ 性能较低，建议优化模型或降低输入尺寸" << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "达到60 FPS所需时间: ≤ 16.67 ms" << std::endl;
    std::cout << "当前平均时间: " << avgTime << " ms" << std::endl;
    std::cout << "性能差距: " << (avgTime - 16.67) << " ms" << std::endl;
    
    return 0;
}

