#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>

namespace rm_auto_attack {

struct Detection {
    int x1, y1, x2, y2;  // 边界框坐标
    float confidence;     // 置信度
    int classId;          // 类别ID
    std::string className; // 类别名称
};

class YOLODetector {
public:
    YOLODetector();
    ~YOLODetector();
    
    // 禁用拷贝构造和赋值
    YOLODetector(const YOLODetector&) = delete;
    YOLODetector& operator=(const YOLODetector&) = delete;
    
    // 加载模型
    bool loadModel(const std::string& modelPath);
    
    // 执行检测
    std::vector<Detection> detect(const cv::Mat& frame, float confThreshold = 0.3f);
    
    // 设置类别名称
    void setClassNames(const std::vector<std::string>& classNames);
    
    // 获取类别名称
    std::string getClassName(int classId) const;
    
    // 检查模型是否已加载
    bool isModelLoaded() const { return m_modelLoaded; }
    
    // 预处理图像
    cv::Mat preprocess(const cv::Mat& frame);
    
    // 后处理检测结果
    std::vector<Detection> postprocess(const std::vector<cv::Mat>& outputs, 
                                       int imgWidth, int imgHeight,
                                       float confThreshold);

private:
    bool m_modelLoaded;
    std::vector<std::string> m_classNames;
    
    // 模型相关 (使用ONNX Runtime或LibTorch)
    // 这里使用OpenCV DNN作为示例实现
    cv::dnn::Net m_net;
    
    // 输入尺寸
    int m_inputWidth;
    int m_inputHeight;
    
    // 加载ONNX模型 (如果使用ONNX Runtime)
    bool loadONNXModel(const std::string& modelPath);
    
    // 使用ONNX Runtime进行推理
    std::vector<float> inferenceONNX(const cv::Mat& blob);
    
    // 使用OpenCV DNN进行推理 (备选方案)
    bool loadOpenCVDNN(const std::string& modelPath);
    std::vector<float> inferenceOpenCVDNN(const cv::Mat& blob);
    
    // 调整类别ID (根据Python代码中的逻辑)
    int adjustClassId(int classId) const;
};

} // namespace rm_auto_attack
