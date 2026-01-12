# RoboMaster 自动攻击系统 - C++版本

这是RoboMaster机器人自动攻击系统的C++实现版本，从原始Python项目重构而来。

## 项目概述

本系统实现了基于YOLO目标检测的机器人自动攻击功能，包括：
- 实时目标检测（使用YOLO模型）
- 云台自动巡航
- 目标跟踪和锁定
- 自动射击控制
- CAN总线通信（通过USB转CAN模块）

## 系统要求

### 硬件要求
- MVS相机（海康威视工业相机）
- 云台控制系统（通过USB转CAN模块连接）
- 支持CUDA的GPU（可选，用于加速推理）

### 软件要求
- CMake 3.15 或更高版本
- C++17 兼容的编译器（GCC 7+, Clang 5+, MSVC 2019+）
- OpenCV 4.x
- MVS SDK 3.0.1（相机控制）
- ONNX Runtime 或 LibTorch（模型推理，可选）

## 编译说明

### Linux/ARM (Jetson)

```bash
# 1. 安装依赖
sudo apt-get update
sudo apt-get install -y cmake build-essential libopencv-dev

# 2. 创建构建目录
cd C++
mkdir build && cd build

# 3. 配置CMake
cmake ..

# 4. 编译
make -j4

# 5. 运行
./bin/RM_Auto_Attack
```

### Windows

```bash
# 1. 安装依赖
# - CMake (https://cmake.org/download/)
# - Visual Studio 2019 或更高版本
# - OpenCV (https://opencv.org/releases/)
# - MVS SDK

# 2. 使用CMake GUI或命令行生成Visual Studio项目
cd C++
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64

# 3. 打开生成的.sln文件，在Visual Studio中编译

# 4. 运行
.\bin\RM_Auto_Attack.exe
```

## 模型转换

原始Python项目使用PyTorch格式的YOLO模型（`.pt`文件），需要转换为ONNX格式以在C++中使用：

```python
# 使用Python脚本转换模型
from ultralytics import YOLO

# 加载PyTorch模型
model = YOLO('best.pt')

# 导出为ONNX格式
model.export(format='onnx', imgsz=640)
```

将生成的`best.onnx`文件放置在项目根目录或配置文件中指定的路径。

## 配置说明

编辑`config/config.yaml`文件以配置系统参数：

- `camera`: 相机配置（设备索引、帧率等）
- `model`: 模型配置（路径、输入尺寸、置信度阈值）
- `gimbal`: 云台配置（串口、波特率、CAN ID等）
- `patrol`: 巡航配置（速度、范围等）
- `detection`: 检测配置（目标类型、跟踪参数等）
- `shooting`: 射击配置（延迟、脉冲参数等）

## 使用说明

### 1. 启动程序

```bash
./bin/RM_Auto_Attack
```

### 2. 选择目标类型

程序启动后会提示选择目标类型：
- `1`: 红方目标
- `2`: 蓝方目标

### 3. 运行模式

- **巡航模式**: 云台自动左右摆动，搜索目标
- **跟踪模式**: 检测到目标后，云台自动跟踪目标
- **射击模式**: 目标居中后，自动开始射击

### 4. 退出程序

- 按 `q` 键退出
- 或按 `Ctrl+C` 中断程序

## 目录结构

```
C++/
├── CMakeLists.txt          # CMake构建配置文件
├── README.md               # 本文件
├── config/                 # 配置文件目录
│   └── config.yaml        # 主配置文件
├── include/                # 头文件目录
│   ├── camera/            # 相机模块
│   ├── detection/         # 检测模块
│   ├── serial/            # 串口通信模块
│   ├── can/               # CAN协议模块
│   ├── gimbal/            # 云台控制模块
│   └── utils/             # 工具类模块
└── src/                    # 源文件目录
    ├── main.cpp           # 主程序入口
    ├── camera/            # 相机模块实现
    ├── detection/         # 检测模块实现
    ├── serial/            # 串口通信模块实现
    ├── can/               # CAN协议模块实现
    ├── gimbal/            # 云台控制模块实现
    └── utils/             # 工具类实现
```

## 模块说明

### 1. 相机模块 (MVSCamera)

封装MVS SDK，提供相机初始化、图像采集等功能。

**注意事项**: 当前实现中MVS SDK的具体API调用被注释，需要根据实际的MVS SDK版本进行集成。

### 2. 检测模块 (YOLODetector)

使用OpenCV DNN加载ONNX格式的YOLO模型进行推理。

**备选方案**: 可以替换为ONNX Runtime或LibTorch以获得更好的性能。

### 3. 串口通信模块 (SerialComm)

跨平台的串口通信封装，支持Windows和Linux/Unix系统。

### 4. CAN协议模块 (CANProtocol)

实现USB-CAN转换器的协议封装，包括消息帧构建和解析。

### 5. 云台控制模块 (GimbalController)

实现云台控制逻辑，包括角度设置、射击控制、数据接收等。

### 6. 主程序 (main)

整合所有模块，实现多线程检测、巡航和射击控制。

## 故障排除

### 1. 相机初始化失败

- 检查MVS SDK是否正确安装
- 检查相机驱动是否正确安装
- 确认相机设备索引是否正确
- 检查MVS SDK路径是否正确配置

### 2. 模型加载失败

- 确认模型文件路径正确
- 确认模型格式为ONNX
- 检查OpenCV DNN是否支持该模型格式

### 3. 串口连接失败

- 检查串口设备名称是否正确（Windows: COM3, Linux: /dev/ttyACM0）
- 检查串口权限（Linux需要将用户添加到dialout组）
- 确认USB转CAN模块已正确连接

### 4. 云台控制无响应

- 检查串口通信是否正常
- 确认CAN总线配置正确
- 检查云台设备是否上电

## 性能优化建议

1. **使用CUDA加速**: 如果支持GPU，使用CUDA后端可以显著提高检测速度
2. **模型优化**: 使用TensorRT等推理引擎可以进一步提高性能
3. **多线程优化**: 调整线程数量和优先级以适应实际硬件
4. **图像预处理优化**: 减少不必要的图像格式转换

## 已知限制

1. MVS SDK集成需要根据实际SDK版本进行调整
2. YOLO模型后处理（NMS）需要根据具体模型输出格式实现
3. 某些功能可能需要根据实际硬件进行调整

## 开发计划

- [ ] 完整实现MVS SDK集成
- [ ] 实现完整的YOLO后处理（包括NMS）
- [ ] 支持TensorRT推理引擎
- [ ] 添加配置文件解析
- [ ] 改进错误处理和日志系统
- [ ] 添加性能分析工具

## 许可证

本项目基于原始Python项目重构，许可证与原项目保持一致。

## 贡献

欢迎提交Issue和Pull Request来改进本项目。

## 联系方式

如有问题或建议，请通过GitHub Issues联系。
