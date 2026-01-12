# MVS SDK 快速集成指南

## 一、快速开始（5个步骤）

### 步骤1: 设置环境变量（Linux/ARM）

```bash
# 进入MVS SDK目录
cd MVS/MVS-3.0.1_aarch64_20241128

# 运行环境设置脚本（会自动配置环境变量）
source set_env_path.sh

# 或者手动设置
export MVCAM_COMMON_RUNENV=$(pwd)/MVS/lib
export LD_LIBRARY_PATH=$MVCAM_COMMON_RUNENV/aarch64:$LD_LIBRARY_PATH
```

### 步骤2: 修改CMakeLists.txt

编辑 `C++/CMakeLists.txt`，找到以下部分并取消注释：

```cmake
# Linux/ARM特定链接
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
    MvCameraControl  # 取消注释这一行
    rt
    dl
)
```

确保库路径正确：

```cmake
link_directories(${MVS_SDK_PATH}/lib/aarch64)  # 对于ARM架构
```

### 步骤3: 取消注释头文件

编辑 `C++/include/camera/mvs_camera.h`：

```cpp
#include "MvCameraControl.h"  // 取消注释
```

### 步骤4: 实现API调用

编辑 `C++/src/camera/mvs_camera.cpp`，取消注释所有 `// TODO: 调用MVS SDK` 的代码块。

**重要API差异说明：**
- `MV_CC_OpenDevice(handle)` - 只需要handle参数（不是3个参数）
- 需要使用 `MV_CC_Initialize()` 先初始化SDK
- 可以使用 `MV_CC_GetOneFrameTimeout()` 或 `MV_CC_GetImageBuffer()`

### 步骤5: 重新编译

```bash
cd C++/build
cmake ..
make -j4
```

## 二、核心修改点

### 1. initMVSSDK() 函数

```cpp
bool MVSCamera::initMVSSDK() {
    int ret = MV_CC_Initialize();  // 初始化SDK
    if (ret != MV_OK) {
        LOG_ERROR("MVS SDK初始化失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    
    unsigned int version = MV_CC_GetSDKVersion();
    LOG_INFO("MVS SDK版本: 0x" + std::to_string(version, std::hex));
    return true;
}
```

### 2. enumDevices() 函数

```cpp
bool MVSCamera::enumDevices(int& deviceCount) {
    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK) {
        LOG_ERROR("枚举设备失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    
    deviceCount = deviceList.nDeviceNum;
    return true;
}
```

### 3. openDevice() 函数

```cpp
bool MVSCamera::openDevice(int deviceIndex) {
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
        LOG_ERROR("创建句柄失败，错误码: 0x" + std::to_string(ret, std::hex));
        return false;
    }
    
    // 打开设备（注意：只需要handle参数）
    ret = MV_CC_OpenDevice(m_cameraHandle);
    if (ret != MV_OK) {
        LOG_ERROR("打开设备失败，错误码: 0x" + std::to_string(ret, std::hex));
        MV_CC_DestroyHandle(m_cameraHandle);
        m_cameraHandle = nullptr;
        return false;
    }
    
    return true;
}
```

### 4. cleanup() 函数

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
    
    // 清理SDK
    MV_CC_Finalize();
    
    m_initialized = false;
    LOG_INFO("相机资源已清理");
}
```

## 三、编译和运行

### 编译

```bash
cd C++
mkdir -p build
cd build
cmake ..
make -j4
```

### 运行时设置库路径

如果编译成功但运行时找不到库，执行：

```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/../../MVS/MVS-3.0.1_aarch64_20241128/MVS/lib/aarch64
./bin/RM_Auto_Attack
```

### 永久设置（推荐）

在 `~/.bashrc` 中添加：

```bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/path/to/MVS/MVS-3.0.1_aarch64_20241128/MVS/lib/aarch64
```

然后执行：

```bash
source ~/.bashrc
```

## 四、验证集成

运行程序后，应该看到：

```
[INFO] 开始初始化MVS相机...
[INFO] MVS SDK版本: 0x...
[INFO] 找到 X 个相机设备
[INFO] 相机初始化成功
```

## 五、常见问题

### Q1: 编译时找不到MvCameraControl库

**A:** 检查 `CMakeLists.txt` 中的库路径是否正确，确保库文件存在：
```bash
ls MVS/MVS-3.0.1_aarch64_20241128/MVS/lib/aarch64/libMvCameraControl.so*
```

### Q2: 运行时找不到动态库

**A:** 设置 `LD_LIBRARY_PATH` 环境变量（见上方"运行时设置库路径"）

### Q3: 相机枚举失败

**A:** 
- 检查相机是否已连接
- 检查相机驱动是否已安装
- 尝试使用MVS客户端软件测试相机

### Q4: API调用返回错误码

**A:** 查看 `MVS/include/MvErrorDefine.h` 了解错误码含义

## 六、参考资源

1. **示例代码**: `MVS/MVS-3.0.1_aarch64_20241128/MVS/Samples/aarch64/C++/General/GrabImage/GrabImage.cpp`
2. **API文档**: `MVS/MVS-3.0.1_aarch64_20241128/MVS/doc/`
3. **头文件**: `MVS/MVS-3.0.1_aarch64_20241128/MVS/include/MvCameraControl.h`
