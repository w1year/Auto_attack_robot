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
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

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
)

set(HEADERS
    include/camera/mvs_camera.h
    include/detection/yolo_detector.h
    include/serial/serial_comm.h
    include/can/can_protocol.h
    include/gimbal/gimbal_controller.h
    include/utils/logger.h
    include/utils/config.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接库
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
)

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
