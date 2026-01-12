#!/bin/bash
# RoboMaster自动攻击系统 - Linux/ARM构建脚本

set -e

echo "=== RoboMaster自动攻击系统 - 构建脚本 ==="

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到CMake，请先安装CMake"
    exit 1
fi

# 检查OpenCV
if ! pkg-config --exists opencv4; then
    echo "警告: 未找到OpenCV4，尝试查找OpenCV..."
    if ! pkg-config --exists opencv; then
        echo "错误: 未找到OpenCV，请先安装OpenCV开发库"
        echo "Ubuntu/Debian: sudo apt-get install libopencv-dev"
        exit 1
    fi
fi

# 创建构建目录
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "配置CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo "开始编译..."
make -j$(nproc)

echo "构建完成！"
echo "可执行文件位置: $(pwd)/bin/RM_Auto_Attack"
echo ""
echo "运行程序:"
echo "  cd $BUILD_DIR"
echo "  ./bin/RM_Auto_Attack"
