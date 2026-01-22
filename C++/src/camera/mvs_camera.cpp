#include "camera/mvs_camera.h"
#include "utils/logger.h"
#include <cstring>
#include <opencv2/opencv.hpp>
#ifdef OPENCV_CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#endif
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

#ifdef OPENCV_CUDA_ENABLED
// CUDA可用性检查（静态变量，所有实例共享）
static bool s_cudaAvailable = false;
static bool s_cudaChecked = false;
static cv::cuda::Stream s_cudaStream;
#endif

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
    
    // 设置像素格式为BayerRG8格式（用于从Bayer直接提取红蓝色）
    ret = MV_CC_SetEnumValue(m_cameraHandle, "PixelFormat", PixelType_Gvsp_BayerRG8);
    if (ret != MV_OK) {
        // 如果BayerRG8不支持，尝试BayerBG8
        ret = MV_CC_SetEnumValue(m_cameraHandle, "PixelFormat", PixelType_Gvsp_BayerBG8);
        if (ret != MV_OK) {
            // 如果BayerBG8不支持，尝试其他Bayer格式
            ret = MV_CC_SetEnumValue(m_cameraHandle, "PixelFormat", PixelType_Gvsp_BayerGB8);
            if (ret != MV_OK) {
                ret = MV_CC_SetEnumValue(m_cameraHandle, "PixelFormat", PixelType_Gvsp_BayerGR8);
                if (ret != MV_OK) {
                    LOG_WARNING("设置Bayer像素格式失败，使用相机默认格式，错误码: " + intToHex(static_cast<unsigned int>(ret)));
                } else {
                    LOG_INFO("像素格式已设置为: BayerGR8 (用于从Bayer提取红蓝色)");
                }
            } else {
                LOG_INFO("像素格式已设置为: BayerGB8 (用于从Bayer提取红蓝色)");
            }
        } else {
            LOG_INFO("像素格式已设置为: BayerBG8 (用于从Bayer提取红蓝色)");
        }
    } else {
        LOG_INFO("像素格式已设置为: BayerRG8 (用于从Bayer提取红蓝色)");
    }
    
    // 启用自动白平衡（有助于颜色识别）
    MVCC_ENUMVALUE enumValue;
    memset(&enumValue, 0, sizeof(MVCC_ENUMVALUE));
    ret = MV_CC_GetEnumValue(m_cameraHandle, "BalanceWhiteAuto", &enumValue);
    if (ret == MV_OK) {
        // 尝试设置为自动白平衡
        ret = MV_CC_SetEnumValue(m_cameraHandle, "BalanceWhiteAuto", MV_BALANCEWHITE_AUTO_CONTINUOUS);
        if (ret == MV_OK) {
            LOG_INFO("白平衡已设置为: 自动连续模式 (有助于红蓝色识别)");
        } else {
            LOG_WARNING("设置自动白平衡失败，使用相机默认设置");
        }
    }
    
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
#ifdef OPENCV_CUDA_ENABLED
    // 检查CUDA是否可用（只检查一次）
    if (!s_cudaChecked) {
        try {
            int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
            s_cudaAvailable = (deviceCount > 0);
            if (s_cudaAvailable) {
                LOG_INFO("CUDA加速已启用，Bayer转换将使用GPU加速 (设备数量: " + std::to_string(deviceCount) + ")");
            } else {
                LOG_WARNING("CUDA设备数量为0，Bayer转换将使用CPU (可能需要检查CUDA驱动)");
            }
        } catch (const cv::Exception& e) {
            s_cudaAvailable = false;
            LOG_WARNING("CUDA检查失败，Bayer转换将使用CPU: " + std::string(e.what()));
        } catch (...) {
            s_cudaAvailable = false;
            LOG_WARNING("CUDA检查异常，Bayer转换将使用CPU");
        }
        s_cudaChecked = true;
    }
#else
    // 如果没有OPENCV_CUDA_ENABLED宏，说明编译时未启用CUDA
    static bool cudaWarningPrinted = false;
    if (!cudaWarningPrinted) {
        LOG_WARNING("编译时未启用OpenCV CUDA支持，Bayer转换将使用CPU");
        cudaWarningPrinted = true;
    }
#endif

    switch (pixelType) {
        case PixelType_Gvsp_BayerRG8: {
            cv::Mat bayerImg(height, width, CV_8UC1, data);
#ifdef OPENCV_CUDA_ENABLED
            if (s_cudaAvailable) {
                // CUDA加速：使用GPU进行Bayer转换（这是最耗时的部分）
                cv::cuda::GpuMat gpuBayer(bayerImg);
                cv::cuda::GpuMat gpuBGR;
                
                // GPU上的Bayer转换（8-10ms -> 2-3ms）
                cv::cuda::cvtColor(gpuBayer, gpuBGR, cv::COLOR_BayerRG2BGR, 0, s_cudaStream);
                
                // 下载到CPU
                gpuBGR.download(frame, s_cudaStream);
                s_cudaStream.waitForCompletion();
                
                // 确保frame格式正确（BGR，3通道，8位）
                if (frame.type() != CV_8UC3 || frame.channels() != 3) {
                    LOG_ERROR("CUDA转换后图像格式错误: type=" + std::to_string(frame.type()) + 
                             ", channels=" + std::to_string(frame.channels()) + 
                             ", 回退到CPU转换");
                    // 回退到CPU转换
                    cv::cvtColor(bayerImg, frame, cv::COLOR_BayerRG2BGR);
                }
                
                // CPU上交换B和R通道（很快，<1ms）
                cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
                const int totalPixels = height * width;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int i = 0; i < totalPixels; ++i) {
                    std::swap(framePtr[i][0], framePtr[i][2]);  // 交换B和R
                }
            } else {
#endif
            // CPU版本（fallback或CUDA不可用时）
            cv::cvtColor(bayerImg, frame, cv::COLOR_BayerRG2BGR);
            cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
            const int totalPixels = height * width;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < totalPixels; ++i) {
                std::swap(framePtr[i][0], framePtr[i][2]);  // 交换B和R
            }
#ifdef OPENCV_CUDA_ENABLED
            }
#endif
            break;
        }
        case PixelType_Gvsp_BayerBG8: {
            cv::Mat bayerImg(height, width, CV_8UC1, data);
#ifdef OPENCV_CUDA_ENABLED
            if (s_cudaAvailable) {
                // CUDA加速：使用GPU进行Bayer转换（最耗时的部分）
                cv::cuda::GpuMat gpuBayer(bayerImg);
                cv::cuda::GpuMat gpuBGR;
                cv::cuda::cvtColor(gpuBayer, gpuBGR, cv::COLOR_BayerBG2BGR, 0, s_cudaStream);
                gpuBGR.download(frame, s_cudaStream);
                s_cudaStream.waitForCompletion();
                
                // 确保frame格式正确
                if (frame.type() != CV_8UC3 || frame.channels() != 3) {
                    LOG_ERROR("CUDA转换后图像格式错误，回退到CPU转换");
                    cv::cvtColor(bayerImg, frame, cv::COLOR_BayerBG2BGR);
                }
                
                // CPU上交换B和R通道（很快，<1ms）
                cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
                const int totalPixels = height * width;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int i = 0; i < totalPixels; ++i) {
                    std::swap(framePtr[i][0], framePtr[i][2]);
                }
            } else {
#endif
            cv::cvtColor(bayerImg, frame, cv::COLOR_BayerBG2BGR);
            cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
            const int totalPixels = height * width;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < totalPixels; ++i) {
                std::swap(framePtr[i][0], framePtr[i][2]);
            }
#ifdef OPENCV_CUDA_ENABLED
            }
#endif
            break;
        }
        case PixelType_Gvsp_BayerGB8: {
            cv::Mat bayerImg(height, width, CV_8UC1, data);
#ifdef OPENCV_CUDA_ENABLED
            if (s_cudaAvailable) {
                cv::cuda::GpuMat gpuBayer(bayerImg);
                cv::cuda::GpuMat gpuBGR;
                cv::cuda::cvtColor(gpuBayer, gpuBGR, cv::COLOR_BayerGB2BGR, 0, s_cudaStream);
                gpuBGR.download(frame, s_cudaStream);
                s_cudaStream.waitForCompletion();
                
                // 确保frame格式正确
                if (frame.type() != CV_8UC3 || frame.channels() != 3) {
                    LOG_ERROR("CUDA转换后图像格式错误，回退到CPU转换");
                    cv::cvtColor(bayerImg, frame, cv::COLOR_BayerGB2BGR);
                }
                
                // CPU上交换B和R通道
                cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
                const int totalPixels = height * width;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int i = 0; i < totalPixels; ++i) {
                    std::swap(framePtr[i][0], framePtr[i][2]);
                }
            } else {
#endif
            cv::cvtColor(bayerImg, frame, cv::COLOR_BayerGB2BGR);
            cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
            const int totalPixels = height * width;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < totalPixels; ++i) {
                std::swap(framePtr[i][0], framePtr[i][2]);
            }
#ifdef OPENCV_CUDA_ENABLED
            }
#endif
            break;
        }
        case PixelType_Gvsp_BayerGR8: {
            cv::Mat bayerImg(height, width, CV_8UC1, data);
#ifdef OPENCV_CUDA_ENABLED
            if (s_cudaAvailable) {
                cv::cuda::GpuMat gpuBayer(bayerImg);
                cv::cuda::GpuMat gpuBGR;
                cv::cuda::cvtColor(gpuBayer, gpuBGR, cv::COLOR_BayerGR2BGR, 0, s_cudaStream);
                gpuBGR.download(frame, s_cudaStream);
                s_cudaStream.waitForCompletion();
                
                // 确保frame格式正确
                if (frame.type() != CV_8UC3 || frame.channels() != 3) {
                    LOG_ERROR("CUDA转换后图像格式错误，回退到CPU转换");
                    cv::cvtColor(bayerImg, frame, cv::COLOR_BayerGR2BGR);
                }
                
                // CPU上交换B和R通道
                cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
                const int totalPixels = height * width;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int i = 0; i < totalPixels; ++i) {
                    std::swap(framePtr[i][0], framePtr[i][2]);
                }
            } else {
#endif
            cv::cvtColor(bayerImg, frame, cv::COLOR_BayerGR2BGR);
            cv::Vec3b* framePtr = frame.ptr<cv::Vec3b>();
            const int totalPixels = height * width;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int i = 0; i < totalPixels; ++i) {
                std::swap(framePtr[i][0], framePtr[i][2]);
            }
#ifdef OPENCV_CUDA_ENABLED
            }
#endif
            break;
        }
        case PixelType_Gvsp_Mono8: {
            cv::Mat monoImg(height, width, CV_8UC1, data);
            cv::cvtColor(monoImg, frame, cv::COLOR_GRAY2BGR);
            break;
        }
        case PixelType_Gvsp_RGB8_Packed:
        case PixelType_Gvsp_BGR8_Packed: {
            // 优化：减少数据复制，直接创建Mat视图，只在需要转换时才复制
            cv::Mat rgbImg(height, width, CV_8UC3, data);
            if (pixelType == PixelType_Gvsp_RGB8_Packed) {
                // 需要转换RGB到BGR，必须复制
                cv::cvtColor(rgbImg, frame, cv::COLOR_RGB2BGR);
            } else {
                // BGR8_Packed不需要转换，直接使用（注意：data会在外部释放，需要clone）
                frame = rgbImg.clone();
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

// 颜色阈值分割：从Bayer格式提取红色或蓝色通道的灰度信息
} // namespace rm_auto_attack
