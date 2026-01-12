# MVS SDK 集成指南

本指南说明如何将MVS（海康威视工业相机）SDK集成到C++项目中。

## 一、前提条件

1. **MVS SDK已安装**
   - Linux/ARM: `MVS/MVS-3.0.1_aarch64_20241128/MVS/`
   - Windows: `windows_MVS/MVS/Development/`

2. **系统环境变量设置（Linux/ARM）**
   ```bash
   # 设置MVS SDK环境变量
   export MVCAM_COMMON_RUNENV=/opt/MVS
   # 或者如果SDK在项目目录中
   export MVCAM_COMMON_RUNENV=$(pwd)/MVS/MVS-3.0.1_aarch64_20241128/MVS
   ```

## 二、集成步骤

### 步骤1: 修改CMakeLists.txt

编辑 `C++/CMakeLists.txt`，取消注释并配置MVS SDK相关部分：

```cmake
# MVS SDK路径 (根据实际情况调整)
if(WIN32)
    set(MVS_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../windows_MVS/MVS/Development")
else()
    set(MVS_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../MVS/MVS-3.0.1_aarch64_20241128/MVS")
endif()

if(EXISTS ${MVS_SDK_PATH})
    include_directories(${MVS_SDK_PATH}/include)
    link_directories(${MVS_SDK_PATH}/lib/aarch64)  # 对于ARM架构
    # link_directories(${MVS_SDK_PATH}/lib/x64)    # 对于x86_64架构
endif()
```

在链接库部分，取消注释MVS SDK库：

```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
    MvCameraControl  # 取消注释这一行
    rt
    dl
)
```

### 步骤2: 修改头文件

编辑 `C++/include/camera/mvs_camera.h`，取消注释MVS SDK头文件：

```cpp
#include "MvCameraControl.h"  // 取消注释
```

同时需要包含其他必要的头文件：

```cpp
#include "PixelType.h"        // 像素类型定义
#include "CameraParams.h"     // 相机参数定义
#include "MvErrorDefine.h"    // 错误码定义
```

### 步骤3: 修改相机句柄类型

在 `C++/include/camera/mvs_camera.h` 中，将 `void* m_cameraHandle` 改为正确的类型：

```cpp
private:
    void* m_cameraHandle;  // 改为: void* m_cameraHandle; (MVS SDK使用void*作为句柄)
```

实际上MVS SDK使用 `void*` 作为句柄类型，所以可以保持不变。

### 步骤4: 实现MVS SDK API调用

编辑 `C++/src/camera/mvs_camera.cpp`，取消注释并实现所有MVS SDK相关的函数。

#### 4.1 initMVSSDK() 函数

```cpp
bool MVSCamera::initMVSSDK() {
    unsigned int version = MV_CC_GetSDKVersion();
    LOG_INFO("MVS SDK版本: 0x" + std::to_string(version, std::hex));
    return true;
}
```

#### 4.2 enumDevices() 函数

```cpp
bool MVSCamera::enumDevices(int& deviceCount) {
    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    
    unsigned int nTLayerType = MV_GIGE_DEVICE | MV_USB_DEVICE;
    int ret = MV_CC_EnumDevices(nTLayerType, &deviceList);
    if (ret != MV_OK) {
        LOG_ERROR("枚举设备失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    
    deviceCount = deviceList.nDeviceNum;
    return true;
}
```

#### 4.3 openDevice() 函数

```cpp
bool MVSCamera::openDevice(int deviceIndex) {
    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    
    unsigned int nTLayerType = MV_GIGE_DEVICE | MV_USB_DEVICE;
    int ret = MV_CC_EnumDevices(nTLayerType, &deviceList);
    if (ret != MV_OK || deviceList.nDeviceNum == 0) {
        LOG_ERROR("枚举设备失败");
        return false;
    }
    
    if (deviceIndex >= static_cast<int>(deviceList.nDeviceNum)) {
        LOG_ERROR("设备索引超出范围");
        return false;
    }
    
    // 创建句柄
    ret = MV_CC_CreateHandle(&m_cameraHandle, &deviceList.pDeviceInfo[deviceIndex]);
    if (ret != MV_OK) {
        LOG_ERROR("创建句柄失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    
    // 打开设备
    ret = MV_CC_OpenDevice(m_cameraHandle, MV_ACCESS_Exclusive, 0);
    if (ret != MV_OK) {
        LOG_ERROR("打开设备失败，错误码: 0x" + std::to_string(ret, std::hex));
        MV_CC_DestroyHandle(m_cameraHandle);
        m_cameraHandle = nullptr;
        return false;
    }
    
    return true;
}
```

#### 4.4 configureDevice() 函数

```cpp
bool MVSCamera::configureDevice() {
    // 设置触发模式为关闭
    int ret = MV_CC_SetEnumValue(m_cameraHandle, "TriggerMode", MV_TRIGGER_MODE_OFF);
    if (ret != MV_OK) {
        LOG_ERROR("设置触发模式失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    LOG_INFO("触发模式已设置为: 关闭");
    
    // 获取负载大小
    MVCC_INTVALUE param;
    memset(&param, 0, sizeof(MVCC_INTVALUE));
    ret = MV_CC_GetIntValue(m_cameraHandle, "PayloadSize", &param);
    if (ret != MV_OK) {
        LOG_ERROR("获取负载大小失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    m_params.payloadSize = param.nCurValue;
    
    // 获取图像尺寸
    ret = MV_CC_GetIntValue(m_cameraHandle, "Width", &param);
    if (ret != MV_OK) {
        LOG_ERROR("获取宽度失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    m_params.width = param.nCurValue;
    
    ret = MV_CC_GetIntValue(m_cameraHandle, "Height", &param);
    if (ret != MV_OK) {
        LOG_ERROR("获取高度失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    m_params.height = param.nCurValue;
    
    // 设置帧率
    ret = MV_CC_SetFloatValue(m_cameraHandle, "AcquisitionFrameRate", m_params.frameRate);
    if (ret != MV_OK) {
        LOG_ERROR("设置帧率失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    LOG_INFO("帧率已设置为: " + std::to_string(m_params.frameRate));
    
    return true;
}
```

#### 4.5 startGrabbing() 函数

```cpp
bool MVSCamera::startGrabbing() {
    if (!m_initialized) {
        LOG_ERROR("相机未初始化，无法开始采集");
        return false;
    }
    
    LOG_INFO("开始相机采集");
    
    int ret = MV_CC_StartGrabbing(m_cameraHandle);
    if (ret != MV_OK) {
        LOG_ERROR("开始采集失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    
    return true;
}
```

#### 4.6 stopGrabbing() 函数

```cpp
void MVSCamera::stopGrabbing() {
    if (!m_initialized) {
        return;
    }
    
    LOG_INFO("停止相机采集");
    
    MV_CC_StopGrabbing(m_cameraHandle);
}
```

#### 4.7 getFrame() 函数

```cpp
bool MVSCamera::getFrame(cv::Mat& frame, int timeoutMs) {
    if (!m_initialized) {
        LOG_ERROR("相机未初始化");
        return false;
    }
    
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
}
```

#### 4.8 convertToMat() 函数

```cpp
cv::Mat MVSCamera::convertToMat(unsigned char* data, int width, int height, int pixelType) {
    cv::Mat frame;
    
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
            LOG_ERROR("不支持的像素格式: 0x" + std::to_string(pixelType, std::hex));
            frame = cv::Mat::zeros(height, width, CV_8UC3);
            break;
    }
    
    return frame;
}
```

#### 4.9 cleanup() 函数

```cpp
void MVSCamera::cleanup() {
    if (!m_initialized) {
        return;
    }
    
    stopGrabbing();
    
    if (m_cameraHandle) {
        MV_CC_CloseDevice(m_cameraHandle);
        MV_CC_DestroyHandle(m_cameraHandle);
        m_cameraHandle = nullptr;
    }
    
    m_initialized = false;
    LOG_INFO("相机资源已清理");
}
```

### 步骤5: 设置库文件路径（运行时）

在Linux/ARM系统上，需要确保程序能够找到MVS SDK的动态库：

**方法1: 使用LD_LIBRARY_PATH**
```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/MVS/MVS-3.0.1_aarch64_20241128/MVS/lib/aarch64
```

**方法2: 使用RPATH（推荐）**
在CMakeLists.txt中已经配置了RPATH，确保库文件可以被找到。

**方法3: 复制库文件到系统目录**
```bash
sudo cp MVS/MVS-3.0.1_aarch64_20241128/MVS/lib/aarch64/*.so /usr/local/lib/
sudo ldconfig
```

### 步骤6: 编译和测试

```bash
cd C++
mkdir -p build
cd build
cmake ..
make -j4
```

如果编译成功，运行程序：
```bash
./bin/RM_Auto_Attack
```

## 三、常见问题

### 问题1: 找不到MvCameraControl库

**解决方案:**
- 检查 `CMakeLists.txt` 中的库路径是否正确
- 确保库文件存在于指定路径
- Linux/ARM: `MVS/lib/aarch64/libMvCameraControl.so`
- x86_64: `MVS/lib/x64/libMvCameraControl.so`

### 问题2: 运行时找不到动态库

**解决方案:**
- 设置 `LD_LIBRARY_PATH` 环境变量
- 或使用RPATH（已在CMakeLists.txt中配置）
- 或将库文件复制到系统库目录

### 问题3: 相机枚举失败

**解决方案:**
- 检查相机是否已连接
- 检查相机驱动是否已安装
- 尝试使用MVS客户端软件测试相机是否正常工作

### 问题4: API调用返回错误码

**解决方案:**
- 查看 `MvErrorDefine.h` 了解错误码含义
- 检查相机参数设置是否正确
- 确保相机处于正确的状态（已打开、未锁定等）

## 四、参考资源

1. **MVS SDK文档**: `MVS/MVS-3.0.1_aarch64_20241128/MVS/doc/`
2. **示例代码**: `MVS/MVS-3.0.1_aarch64_20241128/MVS/Samples/`
3. **头文件**: `MVS/MVS-3.0.1_aarch64_20241128/MVS/include/`

## 五、注意事项

1. **API版本**: 确保使用的API与SDK版本匹配
2. **线程安全**: MVS SDK的API调用不是线程安全的，需要加锁保护
3. **资源管理**: 确保正确释放相机资源（调用cleanup()）
4. **错误处理**: 所有API调用都应该检查返回值
5. **像素格式**: 根据实际相机支持的像素格式进行转换
