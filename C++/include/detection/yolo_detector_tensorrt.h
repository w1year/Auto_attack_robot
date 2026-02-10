#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime.h>
#include <fstream>

namespace rm_auto_attack {

struct Detection {
    int x1, y1, x2, y2;  // 边界框坐标
    float confidence;     // 置信度
    int classId;          // 类别ID
    std::string className; // 类别名称
};

// TensorRT Logger类
class TensorRTLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
};

// TensorRT YOLO检测器类
class YOLODetectorTensorRT {
public:
    YOLODetectorTensorRT();
    ~YOLODetectorTensorRT();
    
    // 禁用拷贝构造和赋值
    YOLODetectorTensorRT(const YOLODetectorTensorRT&) = delete;
    YOLODetectorTensorRT& operator=(const YOLODetectorTensorRT&) = delete;
    
    // 加载模型（支持ONNX和TensorRT引擎）
    bool loadModel(const std::string& modelPath, bool useFP16 = true);
    
    // 执行检测
    std::vector<Detection> detect(const cv::Mat& frame, float confThreshold = 0.3f);
    
    // 设置类别名称
    void setClassNames(const std::vector<std::string>& classNames);
    
    // 获取类别名称
    std::string getClassName(int classId) const;
    
    // 检查模型是否已加载
    bool isModelLoaded() const { return m_modelLoaded; }
    
    // 预热（用于性能测试）
    void warmup(int iterations = 10);

private:
    bool m_modelLoaded;
    std::vector<std::string> m_classNames;
    
    // TensorRT相关
    std::unique_ptr<TensorRTLogger> m_logger;
    nvinfer1::IRuntime* m_runtime;
    nvinfer1::ICudaEngine* m_engine;
    nvinfer1::IExecutionContext* m_context;
    
    // CUDA相关
    void* m_inputBuffer;
    void* m_outputBuffer;
    size_t m_inputSize;
    size_t m_outputSize;
    cudaStream_t m_stream;
    
    // 模型参数
    int m_inputWidth;
    int m_inputHeight;
    int m_outputSizeElements;
    
    // 内部方法
    bool buildEngineFromONNX(const std::string& onnxPath, const std::string& enginePath, bool useFP16);
    bool loadEngine(const std::string& enginePath);
    bool allocateBuffers();
    void YOLODetectorTensorRT::preprocessImage(const cv::Mat& image, float* gpu_Input, const cudaStream_t& stream);
    std::vector<Detection> postprocessOutput(float* gpuOutput, int imgWidth, int imgHeight, float confThreshold);
    int adjustClassId(int classId) const;
    
    // 清理资源
    void cleanup();
};

} // namespace rm_auto_attack

