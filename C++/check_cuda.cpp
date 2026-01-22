#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>

int main() {
    std::cout << "=== OpenCV CUDA 诊断工具 ===" << std::endl;
    std::cout << "OpenCV版本: " << CV_VERSION << std::endl;
    
    // 检查CUDA设备
    try {
        int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
        std::cout << "CUDA设备数量: " << deviceCount << std::endl;
        
        if (deviceCount > 0) {
            std::cout << "✓ CUDA设备可用" << std::endl;
            cv::cuda::printCudaDeviceInfo(0);
        } else {
            std::cout << "✗ 未检测到CUDA设备" << std::endl;
        }
    } catch (const cv::Exception& e) {
        std::cout << "✗ CUDA模块不可用: " << e.what() << std::endl;
    }
    
    // 检查DNN后端
    std::cout << "\n=== DNN后端检查 ===" << std::endl;
    try {
        std::vector<std::pair<cv::dnn::Backend, cv::dnn::Target>> backends = cv::dnn::getAvailableBackends();
        std::cout << "可用后端数量: " << backends.size() << std::endl;
        
        bool cudaFound = false;
        for (const auto& backend : backends) {
            std::string backendName = "Unknown";
            std::string targetName = "Unknown";
            
            switch (backend.first) {
                case cv::dnn::DNN_BACKEND_DEFAULT: backendName = "DEFAULT"; break;
                case cv::dnn::DNN_BACKEND_HALIDE: backendName = "HALIDE"; break;
                case cv::dnn::DNN_BACKEND_INFERENCE_ENGINE: backendName = "INFERENCE_ENGINE"; break;
                case cv::dnn::DNN_BACKEND_OPENCV: backendName = "OPENCV"; break;
                case cv::dnn::DNN_BACKEND_VKCOM: backendName = "VKCOM"; break;
                case cv::dnn::DNN_BACKEND_CUDA: backendName = "CUDA"; break;
                default: backendName = "UNKNOWN"; break;
            }
            
            switch (backend.second) {
                case cv::dnn::DNN_TARGET_CPU: targetName = "CPU"; break;
                case cv::dnn::DNN_TARGET_OPENCL: targetName = "OPENCL"; break;
                case cv::dnn::DNN_TARGET_OPENCL_FP16: targetName = "OPENCL_FP16"; break;
                case cv::dnn::DNN_TARGET_MYRIAD: targetName = "MYRIAD"; break;
                case cv::dnn::DNN_TARGET_VULKAN: targetName = "VULKAN"; break;
                case cv::dnn::DNN_TARGET_FPGA: targetName = "FPGA"; break;
                case cv::dnn::DNN_TARGET_CUDA: targetName = "CUDA"; break;
                case cv::dnn::DNN_TARGET_CUDA_FP16: targetName = "CUDA_FP16"; break;
                default: targetName = "UNKNOWN"; break;
            }
            
            std::cout << "  后端: " << backendName << ", 目标: " << targetName << std::endl;
            
            if (backend.first == cv::dnn::DNN_BACKEND_CUDA && 
                backend.second == cv::dnn::DNN_TARGET_CUDA) {
                cudaFound = true;
            }
        }
        
        if (cudaFound) {
            std::cout << "\n✓ CUDA后端可用，可以启用GPU加速" << std::endl;
        } else {
            std::cout << "\n✗ CUDA后端不可用" << std::endl;
            std::cout << "原因: OpenCV编译时未启用CUDA支持" << std::endl;
        }
    } catch (const cv::Exception& e) {
        std::cout << "检查DNN后端时出错: " << e.what() << std::endl;
    }
    
    // 检查OpenCV构建信息
    std::cout << "\n=== OpenCV构建信息 (CUDA相关) ===" << std::endl;
    std::string buildInfo = cv::getBuildInformation();
    size_t pos = buildInfo.find("CUDA");
    if (pos != std::string::npos) {
        size_t end = buildInfo.find("\n\n", pos);
        if (end == std::string::npos) end = buildInfo.length();
        std::cout << buildInfo.substr(pos, end - pos) << std::endl;
    } else {
        std::cout << "未找到CUDA相关信息" << std::endl;
    }
    
    return 0;
}

