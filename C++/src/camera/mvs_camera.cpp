#include "camera/mvs_camera.h"
#include "utils/logger.h"
#include <cstring>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>

#ifdef MVS_SDK_ENABLED
#include "MvCameraControl.h"
#include "PixelType.h"
#include "CameraParams.h"
#include "MvErrorDefine.h"
#endif

namespace rm_auto_attack {

// 辅助函数：将整数转换为十六进制字符串
static std::string intToHex(unsigned int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << value;
    return ss.str();
}

// 辅助函数：检查文件是否存在
static bool fileExists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

MVSCamera::MVSCamera() 
    : m_cameraHandle(nullptr), m_initialized(false) {
}

MVSCamera::~MVSCamera() {
    cleanup();
}

bool MVSCamera::initialize(int deviceIndex) {
    if (m_initialized) {
        LOG_WARNING("相机已经初始化");
        return true;
    }
    
    LOG_INFO("开始初始化MVS相机...");
    
#ifdef MVS_SDK_ENABLED
    // 初始化MVS SDK
    if (!initMVSSDK()) {
        LOG_ERROR("MVS SDK初始化失败");
        return false;
    }
    
    // 枚举设备
    int deviceCount = 0;
    if (!enumDevices(deviceCount)) {
        LOG_ERROR("枚举相机设备失败");
        return false;
    }
    
    if (deviceCount == 0) {
        LOG_ERROR("未找到相机设备");
        MV_CC_Finalize();
        return false;
    }
    
    LOG_INFO("找到 " + std::to_string(deviceCount) + " 个相机设备");
    
    if (deviceIndex >= deviceCount) {
        LOG_ERROR("设备索引超出范围: " + std::to_string(deviceIndex) + " >= " + std::to_string(deviceCount));
        MV_CC_Finalize();
        return false;
    }
    
    // 打开设备
    if (!openDevice(deviceIndex)) {
        LOG_ERROR("打开相机设备失败");
        MV_CC_Finalize();
        return false;
    }
    
    // 配置设备参数
    if (!configureDevice()) {
        LOG_ERROR("配置相机参数失败");
        cleanup();
        return false;
    }
    
    m_initialized = true;
    LOG_INFO("相机初始化成功");
    LOG_INFO("相机参数: " + std::to_string(m_params.width) + "x" + 
             std::to_string(m_params.height) + 
             ", 帧率: " + std::to_string(m_params.frameRate));
    
    return true;
#else
    LOG_ERROR("MVS SDK未启用，请在CMakeLists.txt中定义MVS_SDK_ENABLED");
    return false;
#endif
}

bool MVSCamera::startGrabbing() {
    if (!m_initialized) {
        LOG_ERROR("相机未初始化，无法开始采集");
        return false;
    }
    
#ifdef MVS_SDK_ENABLED
    LOG_INFO("开始相机采集");
    
    int ret = MV_CC_StartGrabbing(m_cameraHandle);
    if (ret != MV_OK) {
        LOG_ERROR("开始采集失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    
    return true;
#else
    LOG_ERROR("MVS SDK未启用，无法开始采集");
    return false;
#endif
}

void MVSCamera::stopGrabbing() {
    if (!m_initialized) {
        return;
    }
    
#ifdef MVS_SDK_ENABLED
    LOG_INFO("停止相机采集");
    
    MV_CC_StopGrabbing(m_cameraHandle);
#endif
}

bool MVSCamera::getFrame(cv::Mat& frame, int timeoutMs) {
    if (!m_initialized) {
        LOG_ERROR("相机未初始化");
        return false;
    }
    
#ifdef MVS_SDK_ENABLED
    MV_FRAME_OUT_INFO_EX frameInfo;
    memset(&frameInfo, 0, sizeof(MV_FRAME_OUT_INFO_EX));
    
    unsigned char* dataBuf = new unsigned char[m_params.payloadSize];
    int ret = MV_CC_GetOneFrameTimeout(m_cameraHandle, dataBuf, 
                                       m_params.payloadSize, &frameInfo, timeoutMs);
    
    if (ret != MV_OK) {
        delete[] dataBuf;
        return false;
    }
    
    // 转换图像格式
    frame = convertToMat(dataBuf, frameInfo.nWidth, frameInfo.nHeight, 
                         frameInfo.enPixelType);
    delete[] dataBuf;
    
    return true;
#else
    LOG_ERROR("MVS SDK未启用，无法获取图像帧");
    return false;
#endif
}

bool MVSCamera::setFrameRate(float frameRate) {
    if (!m_initialized) {
        LOG_ERROR("相机未初始化");
        return false;
    }
    
#ifdef MVS_SDK_ENABLED
    int ret = MV_CC_SetFloatValue(m_cameraHandle, "AcquisitionFrameRate", frameRate);
    if (ret != MV_OK) {
        LOG_ERROR("设置帧率失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    
    m_params.frameRate = frameRate;
    LOG_INFO("帧率已设置为: " + std::to_string(frameRate));
    return true;
#else
    LOG_ERROR("MVS SDK未启用");
    return false;
#endif
}

bool MVSCamera::setTriggerMode(bool enable) {
    if (!m_initialized) {
        LOG_ERROR("相机未初始化");
        return false;
    }
    
#ifdef MVS_SDK_ENABLED
    int mode = enable ? MV_TRIGGER_MODE_ON : MV_TRIGGER_MODE_OFF;
    int ret = MV_CC_SetEnumValue(m_cameraHandle, "TriggerMode", mode);
    if (ret != MV_OK) {
        LOG_ERROR("设置触发模式失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    
    LOG_INFO("触发模式已设置为: " + std::string(enable ? "开启" : "关闭"));
    return true;
#else
    LOG_ERROR("MVS SDK未启用");
    return false;
#endif
}

void MVSCamera::cleanup() {
    if (!m_initialized) {
        return;
    }
    
#ifdef MVS_SDK_ENABLED
    stopGrabbing();
    
    if (m_cameraHandle) {
        MV_CC_CloseDevice(m_cameraHandle);
        MV_CC_DestroyHandle(m_cameraHandle);
        m_cameraHandle = nullptr;
    }
    
    // 清理SDK
    MV_CC_Finalize();
#endif
    
    m_initialized = false;
    LOG_INFO("相机资源已清理");
}

bool MVSCamera::initMVSSDK() {
#ifdef MVS_SDK_ENABLED
    int ret = MV_CC_Initialize();
    if (ret != MV_OK) {
        LOG_ERROR("MVS SDK初始化失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    
    unsigned int version = MV_CC_GetSDKVersion();
    LOG_INFO("MVS SDK版本: " + intToHex(version));
    return true;
#else
    LOG_ERROR("MVS SDK未启用");
    return false;
#endif
}

bool MVSCamera::enumDevices(int& deviceCount) {
#ifdef MVS_SDK_ENABLED
    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK) {
        LOG_ERROR("枚举设备失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    
    deviceCount = deviceList.nDeviceNum;
    return true;
#else
    deviceCount = 0;
    LOG_ERROR("MVS SDK未启用");
    return false;
#endif
}

bool MVSCamera::openDevice(int deviceIndex) {
#ifdef MVS_SDK_ENABLED
    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK || deviceList.nDeviceNum == 0) {
        LOG_ERROR("枚举设备失败");
        return false;
    }
    
    if (deviceIndex >= static_cast<int>(deviceList.nDeviceNum)) {
        LOG_ERROR("设备索引超出范围");
        return false;
    }
    
    // 创建句柄
    ret = MV_CC_CreateHandle(&m_cameraHandle, deviceList.pDeviceInfo[deviceIndex]);
    if (ret != MV_OK) {
        LOG_ERROR("创建句柄失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    
    // 打开设备（注意：只需要handle参数）
    ret = MV_CC_OpenDevice(m_cameraHandle);
    if (ret != MV_OK) {
        LOG_ERROR("打开设备失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        MV_CC_DestroyHandle(m_cameraHandle);
        m_cameraHandle = nullptr;
        return false;
    }
    
    return true;
#else
    LOG_ERROR("MVS SDK未启用");
    return false;
#endif
}

bool MVSCamera::configureDevice() {
#ifdef MVS_SDK_ENABLED
    // 设置触发模式为关闭
    int ret = MV_CC_SetEnumValue(m_cameraHandle, "TriggerMode", MV_TRIGGER_MODE_OFF);
    if (ret != MV_OK) {
        LOG_ERROR("设置触发模式失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    LOG_INFO("触发模式已设置为: 关闭");
    
    // 获取负载大小
    MVCC_INTVALUE param;
    memset(&param, 0, sizeof(MVCC_INTVALUE));
    ret = MV_CC_GetIntValue(m_cameraHandle, "PayloadSize", &param);
    if (ret != MV_OK) {
        LOG_ERROR("获取负载大小失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    m_params.payloadSize = param.nCurValue;
    
    // 获取图像尺寸
    ret = MV_CC_GetIntValue(m_cameraHandle, "Width", &param);
    if (ret != MV_OK) {
        LOG_ERROR("获取宽度失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    m_params.width = param.nCurValue;
    
    ret = MV_CC_GetIntValue(m_cameraHandle, "Height", &param);
    if (ret != MV_OK) {
        LOG_ERROR("获取高度失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    m_params.height = param.nCurValue;
    
    // 设置帧率
    ret = MV_CC_SetFloatValue(m_cameraHandle, "AcquisitionFrameRate", m_params.frameRate);
    if (ret != MV_OK) {
        LOG_ERROR("设置帧率失败，错误码: " + intToHex(static_cast<unsigned int>(ret)));
        return false;
    }
    LOG_INFO("帧率已设置为: " + std::to_string(m_params.frameRate));
    
    return true;
#else
    LOG_ERROR("MVS SDK未启用");
    return false;
#endif
}

cv::Mat MVSCamera::convertToMat(unsigned char* data, int width, int height, int pixelType) {
    cv::Mat frame;
    
#ifdef MVS_SDK_ENABLED
    switch (pixelType) {
        case PixelType_Gvsp_BayerRG8:
        case PixelType_Gvsp_BayerGB8:
        case PixelType_Gvsp_BayerGR8:
        case PixelType_Gvsp_BayerBG8: {
            cv::Mat bayerImg(height, width, CV_8UC1, data);
            cv::cvtColor(bayerImg, frame, cv::COLOR_BayerBG2BGR);
            break;
        }
        case PixelType_Gvsp_Mono8: {
            cv::Mat monoImg(height, width, CV_8UC1, data);
            cv::cvtColor(monoImg, frame, cv::COLOR_GRAY2BGR);
            break;
        }
        case PixelType_Gvsp_RGB8_Packed:
        case PixelType_Gvsp_BGR8_Packed: {
            cv::Mat rgbImg(height, width, CV_8UC3, data);
            frame = rgbImg.clone();
            if (pixelType == PixelType_Gvsp_RGB8_Packed) {
                cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
            }
            break;
        }
        default:
            LOG_ERROR("不支持的像素格式: " + intToHex(static_cast<unsigned int>(pixelType)));
            frame = cv::Mat::zeros(height, width, CV_8UC3);
            break;
    }
#else
    // 临时实现：创建空白图像
    frame = cv::Mat::zeros(height, width, CV_8UC3);
#endif
    
    return frame;
}

} // namespace rm_auto_attack
