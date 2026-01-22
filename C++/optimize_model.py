#!/usr/bin/env python3
"""优化模型脚本 - 降低输入尺寸以提高性能"""
from ultralytics import YOLO
import sys

print("=== 模型优化工具 ===")
print("将YOLO模型导出为更小的输入尺寸以提高性能")
print()

model_path = input("输入模型路径 (如: best.pt，直接回车使用 best.pt): ").strip()
if not model_path:
    model_path = "best.pt"

if not os.path.exists(model_path):
    print(f"错误: 文件不存在: {model_path}")
    sys.exit(1)

print()
print("选择输入尺寸:")
print("1. 640x640 (原始，性能要求高)")
print("2. 512x512 (推荐，性能和精度平衡) ⭐")
print("3. 416x416 (最快，精度可能下降)")
print()

choice = input("选择 (1/2/3，默认2): ").strip() or "2"

size_map = {"1": 640, "2": 512, "3": 416}
imgsz = size_map.get(choice, 512)

print()
print(f"加载模型: {model_path}")
model = YOLO(model_path)

print(f"导出为ONNX (输入尺寸: {imgsz}x{imgsz})...")
output_path = model.export(format='onnx', imgsz=imgsz, simplify=True)

print()
print(f"✓ 模型已导出: {output_path}")
print()
print("下一步:")
print(f"1. 删除旧的引擎文件: rm -f best.engine")
print("2. 重新运行程序，会自动生成新的TensorRT引擎")
print(f"3. 预期性能提升: {1.6 if imgsz == 512 else 2.4 if imgsz == 416 else 1.0:.1f}x")
