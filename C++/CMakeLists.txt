cmake_minimum_required(VERSION 3.15)
project(RM_Auto_Attack VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖库
# 优先使用/usr/local下的新版本OpenCV (with CUDA)
set(OpenCV_DIR "/usr/local/lib/cmake/opencv4" CACHE PATH "OpenCV config directory")
find_package(OpenCV REQUIRED)

# 如果找不到，尝试默认路径
if(NOT OpenCV_FOUND)
    find_package(OpenCV REQUIRED)
endif()

# 检查OpenCV是否支持CUDA
if(OpenCV_CUDA_VERSION)
    message(STATUS "OpenCV CUDA支持: 已启用 (版本: ${OpenCV_CUDA_VERSION})")
    add_compile_definitions(OPENCV_CUDA_ENABLED)
else()
    message(STATUS "OpenCV CUDA支持: 未启用 (如需CUDA加速，请重新编译OpenCV并启用CUDA)")
endif()

find_package(Threads REQUIRED)

# 查找OpenMP（用于并行处理）
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP支持: 已启用")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else()
    message(STATUS "OpenMP支持: 未找到（将使用单线程处理）")
endif()

# 可选：查找CUDA工具包（用于更详细的CUDA信息）
find_package(CUDA QUIET)
if(CUDA_FOUND)
    message(STATUS "CUDA工具包已找到: ${CUDA_VERSION}")
    message(STATUS "CUDA架构: ${CUDA_ARCH_BIN}")
else()
    message(STATUS "CUDA工具包未找到（不影响使用，OpenCV可能已静态链接CUDA）")
endif()

# ONNX Runtime (用于YOLO模型推理)
# 如果系统中已安装ONNX Runtime，取消注释以下行并设置路径
# set(ONNXRUNTIME_ROOT_DIR "/path/to/onnxruntime")
# find_package(onnxruntime)

# 替代方案：使用LibTorch (PyTorch C++ API)
# set(Torch_DIR "/path/to/libtorch/share/cmake/Torch")
# find_package(Torch REQUIRED)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

# MVS SDK路径 (根据实际情况调整)
if(WIN32)
    set(MVS_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../windows_MVS/MVS/Development")
else()
    set(MVS_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../MVS/MVS-3.0.1_aarch64_20241128/MVS")
endif()

if(EXISTS ${MVS_SDK_PATH})
    include_directories(${MVS_SDK_PATH}/include)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        link_directories(${MVS_SDK_PATH}/lib/aarch64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        link_directories(${MVS_SDK_PATH}/lib/x64)
    else()
        link_directories(${MVS_SDK_PATH}/lib)
    endif()
    # 定义MVS SDK启用宏
    add_compile_definitions(MVS_SDK_ENABLED)
endif()

# 查找TensorRT
find_library(TENSORRT_LIB nvinfer
    PATHS
    /usr/lib/aarch64-linux-gnu
    /usr/local/lib
    /usr/lib
)
find_path(TENSORRT_INCLUDE NvInfer.h
    PATHS
    /usr/include/aarch64-linux-gnu
    /usr/local/include
    /usr/include
)

# 查找CUDA（TensorRT需要）
# 首先尝试通过CUDA包查找
find_package(CUDA QUIET)
if(CUDA_FOUND AND CUDA_INCLUDE_DIRS)
    set(CUDA_INCLUDE ${CUDA_INCLUDE_DIRS})
    message(STATUS "通过find_package找到CUDA: ${CUDA_INCLUDE}")
else()
    # 手动查找CUDA头文件
    find_path(CUDA_INCLUDE cuda_runtime_api.h
        PATHS
        /usr/local/cuda/include
        /usr/local/cuda-11.8/include
        /usr/local/cuda-11.4/include
        /usr/local/cuda-11.4/targets/aarch64-linux/include
        /usr/include
    )
    if(CUDA_INCLUDE)
        message(STATUS "手动找到CUDA头文件: ${CUDA_INCLUDE}")
    endif()
endif()

if(TENSORRT_LIB AND TENSORRT_INCLUDE)
    message(STATUS "TensorRT已找到: ${TENSORRT_LIB}")
    message(STATUS "TensorRT头文件: ${TENSORRT_INCLUDE}")
    if(CUDA_INCLUDE)
        message(STATUS "CUDA头文件: ${CUDA_INCLUDE}")
    endif()
    add_compile_definitions(TENSORRT_ENABLED)
    set(TENSORRT_FOUND TRUE)
else()
    message(STATUS "TensorRT未找到，将使用OpenCV DNN后端")
    set(TENSORRT_FOUND FALSE)
endif()

# 源文件
set(SOURCES
    src/main.cpp
    src/camera/mvs_camera.cpp
    src/detection/yolo_detector.cpp
    src/serial/serial_comm.cpp
    src/can/can_protocol.cpp
    src/gimbal/gimbal_controller.cpp
    src/utils/logger.cpp
    src/utils/config.cpp
    src/utils/thread_optimizer.cpp
)

# 如果启用TensorRT，添加TensorRT检测器
if(TENSORRT_FOUND)
    list(APPEND SOURCES src/detection/yolo_detector_tensorrt.cpp)
    message(STATUS "TensorRT支持已启用")
endif()

# 源文件列表完成后，设置优化编译选项
# 注意：编译选项将在创建可执行文件后设置

set(HEADERS
    include/camera/mvs_camera.h
    include/detection/yolo_detector.h
    include/serial/serial_comm.h
    include/can/can_protocol.h
    include/gimbal/gimbal_controller.h
    include/utils/logger.h
    include/utils/config.h
    include/utils/thread_optimizer.h
)

# 如果启用TensorRT，添加TensorRT头文件
if(TENSORRT_FOUND)
    list(APPEND HEADERS include/detection/yolo_detector_tensorrt.h)
endif()

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 添加性能优化编译选项（提高CPU和GPU利用率）
target_compile_options(${PROJECT_NAME} PRIVATE
    -march=native      # 使用本地CPU架构优化
    -mtune=native      # 针对本地CPU调优
    -ffast-math        # 快速数学运算（提高浮点运算速度）
    -funroll-loops     # 循环展开（提高循环性能）
)
# 注意：O3已在全局设置（第14行），这里不再重复

# 链接库
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
)

# 如果启用TensorRT，链接TensorRT库
if(TENSORRT_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE 
        ${TENSORRT_INCLUDE}
    )
    # 添加CUDA包含路径（如果找到）
    if(CUDA_INCLUDE)
        target_include_directories(${PROJECT_NAME} PRIVATE ${CUDA_INCLUDE})
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${TENSORRT_LIB}
        nvinfer_plugin
        nvonnxparser
        cudart
    )
    # 添加CUDA库路径
    find_library(CUDART_LIB cudart
        PATHS
        /usr/local/cuda/lib64
        /usr/local/cuda-11.8/lib64
        /usr/local/cuda-11.4/lib64
        /usr/lib/aarch64-linux-gnu
    )
    if(CUDART_LIB)
        target_link_directories(${PROJECT_NAME} PRIVATE 
            ${CMAKE_SYSTEM_LIBRARY_PATH}
            /usr/local/cuda/lib64
            /usr/local/cuda-11.8/lib64
            /usr/local/cuda-11.4/lib64
        )
    endif()
    message(STATUS "TensorRT库已链接")
endif()

# Windows特定链接
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        # MVS SDK库 (根据实际库名调整)
        # MvCameraControl
    )
    # 复制MVS SDK DLL到输出目录
    if(EXISTS ${MVS_SDK_PATH}/bin)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${MVS_SDK_PATH}/bin $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
else()
    # Linux/ARM特定链接
    if(EXISTS ${MVS_SDK_PATH})
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${OpenCV_LIBS}
            Threads::Threads
            MvCameraControl
            rt
            dl
        )
        # 设置RPATH以便找到MVS SDK库
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
            set_target_properties(${PROJECT_NAME} PROPERTIES
                INSTALL_RPATH "${MVS_SDK_PATH}/lib/aarch64"
                BUILD_WITH_INSTALL_RPATH TRUE
            )
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
            set_target_properties(${PROJECT_NAME} PROPERTIES
                INSTALL_RPATH "${MVS_SDK_PATH}/lib/x64"
                BUILD_WITH_INSTALL_RPATH TRUE
            )
        endif()
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${OpenCV_LIBS}
            Threads::Threads
            rt
            dl
        )
    endif()
endif()

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY config/
    DESTINATION etc/rm_auto_attack
    FILES_MATCHING PATTERN "*.yaml" PATTERN "*.json"
)
