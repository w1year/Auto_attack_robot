#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <memory>

// MVS SDK头文件
#ifdef MVS_SDK_ENABLED
#include "MvCameraControl.h"
#include "PixelType.h"
#include "CameraParams.h"
#include "MvErrorDefine.h"
#endif

namespace rm_auto_attack {

struct CameraParams {
    int width = 0;
    int height = 0;
    int payloadSize = 0;
    float frameRate = 60.0f;  // 设置为60帧以匹配目标帧率
};

class MVSCamera {
public:
    MVSCamera();
    ~MVSCamera();
    
    // 禁用拷贝构造和赋值
    MVSCamera(const MVSCamera&) = delete;
    MVSCamera& operator=(const MVSCamera&) = delete;
    
    // 初始化相机
    bool initialize(int deviceIndex = 0);
    
    // 开始采集
    bool startGrabbing();
    
    // 停止采集
    void stopGrabbing();
    
    // 获取一帧图像
    bool getFrame(cv::Mat& frame, int timeoutMs = 1000);
    
    // 获取相机参数
    CameraParams getParams() const { return m_params; }
    
    // 设置帧率
    bool setFrameRate(float frameRate);
    
    // 设置触发模式 (false = 关闭)
    bool setTriggerMode(bool enable);
    
    // 清理资源
    void cleanup();
    
    // 检查相机是否已初始化
    bool isInitialized() const { return m_initialized; }

private:
    void* m_cameraHandle;  // MVS相机句柄
    bool m_initialized;
    CameraParams m_params;
    
    // 初始化MVS SDK
    bool initMVSSDK();
    
    // 枚举设备
    bool enumDevices(int& deviceCount);
    
    // 打开设备
    bool openDevice(int deviceIndex);
    
    // 配置设备参数
    bool configureDevice();
    
    // 转换图像格式
    cv::Mat convertToMat(unsigned char* data, int width, int height, int pixelType);
};

} // namespace rm_auto_attack
