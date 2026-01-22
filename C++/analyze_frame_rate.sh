#!/bin/bash

# 帧率瓶颈分析脚本

echo "=== 帧率瓶颈分析 ==="
echo ""

# 1. 检查相机实际支持的帧率
echo "[1] 检查相机帧率设置..."
echo "    - 代码中设置的目标帧率: 60 FPS"
echo "    - 代码中设置的相机帧率: 60.0f"
echo "    - 实际获取帧的超时时间: 10ms"
echo "    - 如果相机只支持30fps，10ms超时会频繁失败"
echo ""

# 2. 分析可能的瓶颈
echo "[2] 潜在性能瓶颈:"
echo "    a) 相机实际帧率限制 (可能是硬件限制30fps)"
echo "    b) getFrame超时时间过短 (10ms vs 33ms for 30fps)"
echo "    c) Bayer转换开销 (cv::cvtColor)"
echo "    d) 图像克隆开销 (frame.clone() 复制 ~3.7MB)"
echo "    e) YOLO推理时间 (如果 >16.67ms会限制60fps)"
echo "    f) 显示开销 (cv::imshow + cv::waitKey)"
echo ""

# 3. 计算时间开销
echo "[3] 时间开销估算 (1280x960图像):"
echo "    - 图像数据大小: 1280*960*3 = ~3.7MB"
echo "    - frame.clone(): ~2-5ms (内存复制)"
echo "    - Bayer->BGR转换: ~3-8ms (取决于优化)"
echo "    - YOLO推理: 需要实际测量"
echo "    - 60fps要求每帧时间: 16.67ms"
echo "    - 30fps要求每帧时间: 33.33ms"
echo ""

# 4. 检查代码中的问题
echo "[4] 代码中的潜在问题:"
echo "    - convertToMat中Bayer转换可能使用了错误的COLOR代码"
echo "    - 需要验证相机实际支持的帧率"
echo "    - 需要测量各个步骤的实际耗时"
echo ""

# 5. 建议的优化方案
echo "[5] 优化建议:"
echo "    1. 增加getFrame超时时间到33ms (如果是30fps)"
echo "    2. 减少或去除frame.clone() (直接使用frame或引用)"
echo "    3. 降低显示频率 (每2-3帧显示一次)"
echo "    4. 检查YOLO推理时间是否超过16.67ms"
echo "    5. 验证相机实际支持的帧率"
echo "    6. 检查Bayer转换是否正确"
echo ""

echo "=== 建议运行程序并查看控制台输出的FPS信息 ==="

