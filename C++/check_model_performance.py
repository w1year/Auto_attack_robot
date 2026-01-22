#!/usr/bin/env python3
"""
模型性能检查工具
检查ONNX模型的复杂度、输入输出尺寸等信息
"""

import sys
import os

def check_model_info(model_path):
    """检查ONNX模型信息"""
    try:
        import onnx
        
        if not os.path.exists(model_path):
            print(f"错误: 模型文件不存在: {model_path}")
            return False
        
        print(f"检查模型: {model_path}")
        print(f"文件大小: {os.path.getsize(model_path) / 1024 / 1024:.2f} MB")
        print()
        
        model = onnx.load(model_path)
        
        # 检查输入
        print("=" * 60)
        print("模型输入:")
        print("=" * 60)
        for inp in model.graph.input:
            shape = [dim.dim_value if dim.dim_value > 0 else '?' for dim in inp.type.tensor_type.shape.dim]
            print(f"  名称: {inp.name}")
            print(f"  形状: {shape}")
            print(f"  类型: {onnx.TensorProto.DataType.Name(inp.type.tensor_type.elem_type)}")
            
            # 计算参数数量
            total_params = 1
            for dim in shape:
                if isinstance(dim, int):
                    total_params *= dim
            
            print(f"  参数数: {total_params:,}")
            print()
        
        # 检查输出
        print("=" * 60)
        print("模型输出:")
        print("=" * 60)
        for out in model.graph.output:
            shape = [dim.dim_value if dim.dim_value > 0 else '?' for dim in out.type.tensor_type.shape.dim]
            print(f"  名称: {out.name}")
            print(f"  形状: {shape}")
            print(f"  类型: {onnx.TensorProto.DataType.Name(out.type.tensor_type.elem_type)}")
            
            # 计算输出大小
            output_size = 1
            for dim in shape:
                if isinstance(dim, int):
                    output_size *= dim
            
            print(f"  输出元素数: {output_size:,}")
            print()
        
        # 检查节点数量和类型
        print("=" * 60)
        print("模型复杂度:")
        print("=" * 60)
        node_types = {}
        for node in model.graph.node:
            node_type = node.op_type
            node_types[node_type] = node_types.get(node_type, 0) + 1
        
        print(f"总节点数: {len(model.graph.node)}")
        print(f"节点类型分布:")
        for node_type, count in sorted(node_types.items(), key=lambda x: -x[1]):
            print(f"  {node_type}: {count}")
        print()
        
        # 性能评估
        print("=" * 60)
        print("性能评估:")
        print("=" * 60)
        
        # 获取输入尺寸
        input_shape = []
        for inp in model.graph.input:
            for dim in inp.type.tensor_type.shape.dim:
                if dim.dim_value > 0:
                    input_shape.append(dim.dim_value)
        
        if len(input_shape) >= 4:
            batch, channels, height, width = input_shape[0], input_shape[1], input_shape[2], input_shape[3]
            
            print(f"输入尺寸: {batch}x{channels}x{height}x{width}")
            
            # 计算FLOPs（粗略估计）
            # YOLO模型的大致FLOPs = 输入尺寸^2 * 节点数 * 常数
            pixels = height * width
            estimated_flops = pixels * len(model.graph.node) * 1000  # 粗略估计
            print(f"估计FLOPs: {estimated_flops / 1e9:.2f} GFLOPs")
            
            # 性能建议
            print()
            print("性能建议:")
            if height > 640 or width > 640:
                print("  ⚠️  输入尺寸较大，考虑降低到640x640或更小")
            if len(model.graph.node) > 500:
                print("  ⚠️  模型节点数较多，考虑使用更小的模型")
            if estimated_flops > 50:
                print("  ⚠️  计算量较大，可能影响实时性能")
            
            # 推断模型类型
            if "yolo" in model_path.lower() or "detect" in model_path.lower():
                if height == 640 and width == 640:
                    print("  ✓ 标准YOLO输入尺寸 (640x640)")
                else:
                    print(f"  ⚠️  非标准输入尺寸，YOLO通常使用640x640")
        
        return True
        
    except ImportError:
        print("错误: 需要安装onnx库")
        print("安装方法: pip install onnx")
        return False
    except Exception as e:
        print(f"错误: {e}")
        return False

if __name__ == '__main__':
    model_path = 'best.onnx'
    
    if len(sys.argv) > 1:
        model_path = sys.argv[1]
    
    # 尝试多个路径
    possible_paths = [
        model_path,
        f'../{model_path}',
        f'../../{model_path}',
        f'./build/{model_path}',
    ]
    
    found = False
    for path in possible_paths:
        if os.path.exists(path):
            check_model_info(path)
            found = True
            break
    
    if not found:
        print(f"未找到模型文件: {model_path}")
        print("请确保模型文件存在，或指定正确的路径:")
        print("  python3 check_model_performance.py /path/to/best.onnx")

