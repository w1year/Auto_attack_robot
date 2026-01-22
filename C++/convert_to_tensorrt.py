#!/usr/bin/env python3
"""
ONNX到TensorRT引擎转换工具
使用方法: python3 convert_to_tensorrt.py [onnx_file] [engine_file]
"""

import tensorrt as trt
import sys
import os

def build_engine(onnx_file, engine_file, fp16_mode=True, max_workspace_size=1<<30):
    """
    从ONNX文件构建TensorRT引擎
    
    Args:
        onnx_file: ONNX模型文件路径
        engine_file: 输出的TensorRT引擎文件路径
        fp16_mode: 是否使用FP16精度
        max_workspace_size: 最大工作空间大小（字节）
    """
    TRT_LOGGER = trt.Logger(trt.Logger.WARNING)
    
    print(f"正在构建TensorRT引擎...")
    print(f"  输入: {onnx_file}")
    print(f"  输出: {engine_file}")
    print(f"  FP16: {fp16_mode}")
    
    # 创建builder
    builder = trt.Builder(TRT_LOGGER)
    network = builder.create_network(1 << int(trt.NetworkDefinitionCreationFlag.EXPLICIT_BATCH))
    parser = trt.OnnxParser(network, TRT_LOGGER)
    
    # 解析ONNX模型
    print("解析ONNX模型...")
    with open(onnx_file, 'rb') as model:
        if not parser.parse(model.read()):
            print('错误: 解析ONNX文件失败')
            for error in range(parser.num_errors):
                print(f"  {parser.get_error(error)}")
            return None
    
    print(f"✓ ONNX模型解析成功")
    print(f"  输入层数: {network.num_inputs}")
    print(f"  输出层数: {network.num_outputs}")
    
    # 配置构建器
    config = builder.create_builder_config()
    config.max_workspace_size = max_workspace_size
    
    if fp16_mode:
        if builder.platform_has_fast_fp16:
            config.set_flag(trt.BuilderFlag.FP16)
            print("✓ 启用FP16精度")
        else:
            print("警告: 平台不支持FP16，使用FP32")
    
    # 构建引擎
    print("构建TensorRT引擎（这可能需要几分钟）...")
    try:
        engine = builder.build_engine(network, config)
        if engine is None:
            print("错误: 构建引擎失败")
            return None
    except Exception as e:
        print(f"错误: {e}")
        return None
    
    # 保存引擎
    print(f"保存引擎到 {engine_file}...")
    with open(engine_file, 'wb') as f:
        f.write(engine.serialize())
    
    print(f"✓ TensorRT引擎构建完成: {engine_file}")
    print(f"  引擎大小: {os.path.getsize(engine_file) / 1024 / 1024:.2f} MB")
    
    return engine

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("使用方法: python3 convert_to_tensorrt.py <onnx_file> [engine_file] [--fp32]")
        print("示例: python3 convert_to_tensorrt.py best.onnx best.engine")
        sys.exit(1)
    
    onnx_file = sys.argv[1]
    if len(sys.argv) > 2:
        engine_file = sys.argv[2]
    else:
        engine_file = onnx_file.replace('.onnx', '.engine')
    
    fp16_mode = '--fp32' not in sys.argv
    
    if not os.path.exists(onnx_file):
        print(f"错误: ONNX文件不存在: {onnx_file}")
        sys.exit(1)
    
    build_engine(onnx_file, engine_file, fp16_mode)

