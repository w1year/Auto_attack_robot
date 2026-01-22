#!/bin/bash
# 快速性能检查脚本

echo "=== 帧率性能快速检查 ==="
echo ""

echo "1. 模型信息:"
if [ -f "best.onnx" ]; then
    echo "  ✓ best.onnx 存在 ($(ls -lh best.onnx | awk '{print $5}'))"
else
    echo "  ✗ best.onnx 不存在"
fi

if [ -f "best.engine" ]; then
    echo "  ✓ best.engine 存在 ($(ls -lh best.engine | awk '{print $5}'))"
else
    echo "  ✗ best.engine 不存在（首次运行会生成）"
fi

echo ""
echo "2. GPU性能模式:"
if command -v nvpmodel &> /dev/null; then
    current_mode=$(sudo nvpmodel -q 2>/dev/null | grep "Power Mode" | awk '{print $4}' || echo "unknown")
    echo "  当前模式: $current_mode"
    if [ "$current_mode" != "MODE_0" ]; then
        echo "  ⚠️  建议: sudo nvpmodel -m 0 (设置为最大性能模式)"
    else
        echo "  ✓ 已是最大性能模式"
    fi
else
    echo "  ⚠️  nvpmodel 未找到"
fi

echo ""
echo "3. GPU频率设置:"
if [ -f "/sys/devices/17000000.gv11b/device/current_freq" ]; then
    current_freq=$(cat /sys/devices/17000000.gv11b/device/current_freq)
    echo "  当前GPU频率: $((current_freq / 1000000)) MHz"
else
    echo "  ⚠️  无法读取GPU频率"
fi

echo ""
echo "4. 性能建议:"
echo "  如果帧率低于60 FPS，尝试："
echo "  a) 设置最大性能模式: sudo nvpmodel -m 0 && sudo jetson_clocks"
echo "  b) 检查程序显示的推理时间（Inference）"
echo "  c) 如果推理时间 > 10ms，考虑："
echo "     - 降低模型输入尺寸（640→512）"
echo "     - 使用更小的模型（YOLOv8n）"
echo "  d) 临时降低目标帧率到30 FPS（修改main.cpp中的targetFPS）"

echo ""
echo "5. 运行程序查看实时性能:"
echo "  cd build"
echo "  export LD_LIBRARY_PATH=/usr/local/lib:/usr/lib/aarch64-linux-gnu:\$LD_LIBRARY_PATH"
echo "  ./bin/RM_Auto_Attack"
echo ""
echo "  观察显示的信息："
echo "  - FPS: 当前帧率 / 60（目标）"
echo "  - Inference: TensorRT推理时间（应该 < 10ms）"
echo "  - Frame: 每帧总处理时间（应该 < 16.67ms）"

