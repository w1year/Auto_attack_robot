#!/bin/bash
# OpenCV环境设置脚本
# 使用方法: source setup_opencv_env.sh

echo "设置OpenCV 4.8.0 with CUDA环境..."

# 设置库路径
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 设置pkg-config路径（如果存在）
if [ -d "/usr/local/lib/pkgconfig" ]; then
    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
fi

# 设置包含路径
export CPLUS_INCLUDE_PATH=/usr/local/include/opencv4:$CPLUS_INCLUDE_PATH
export C_INCLUDE_PATH=/usr/local/include/opencv4:$C_INCLUDE_PATH

echo "✓ 环境变量已设置"
echo "  LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo ""
echo "提示: 使用 'source setup_opencv_env.sh' 来设置环境"

