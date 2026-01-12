#include "detection/yolo_detector.h"
#include "utils/logger.h"
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <cmath>

namespace rm_auto_attack {

YOLODetector::YOLODetector() 
    : m_modelLoaded(false), m_inputWidth(640), m_inputHeight(640) {
    
    // 默认类别名称 (根据Python代码中的定义)
    m_classNames = {
        "blue100", "blue200", "blue300", "blue400", "blue500",
        "red100", "red200", "red300", "red400", "red500"
    };
}

YOLODetector::~YOLODetector() {
    // 清理资源
}

bool YOLODetector::loadModel(const std::string& modelPath) {
    if (m_modelLoaded) {
        LOG_WARNING("模型已经加载");
        return true;
    }
    
    LOG_INFO("开始加载YOLO模型: " + modelPath);
    
    // 检查文件是否存在
    std::ifstream file(modelPath);
    if (!file.good()) {
        LOG_ERROR("模型文件不存在: " + modelPath);
        LOG_ERROR("请确保模型文件存在，或检查文件路径是否正确");
        return false;
    }
    file.close();
    
    // 优先尝试使用OpenCV DNN加载ONNX模型
    try {
        m_net = cv::dnn::readNetFromONNX(modelPath);
        
        if (m_net.empty()) {
            LOG_ERROR("无法加载ONNX模型: " + modelPath);
            return false;
        }
        
        // 设置后端和目标设备
        // 优先使用CUDA (如果可用)
        try {
            m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
            LOG_INFO("使用CUDA后端进行推理");
        } catch (...) {
            // 如果CUDA不可用，使用默认后端
            m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
            m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            LOG_INFO("使用CPU后端进行推理");
        }
        
        m_modelLoaded = true;
        LOG_INFO("成功加载YOLO模型");
        return true;
        
    } catch (const cv::Exception& e) {
        LOG_ERROR("加载模型时发生OpenCV异常: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("加载模型时发生异常: " + std::string(e.what()));
        return false;
    }
}

std::vector<Detection> YOLODetector::detect(const cv::Mat& frame, float confThreshold) {
    std::vector<Detection> detections;
    
    if (!m_modelLoaded) {
        LOG_ERROR("模型未加载，无法执行检测");
        return detections;
    }
    
    if (frame.empty()) {
        LOG_WARNING("输入图像为空");
        return detections;
    }
    
    try {
        // 预处理图像
        cv::Mat blob;
        cv::dnn::blobFromImage(frame, blob, 1.0/255.0, 
                              cv::Size(m_inputWidth, m_inputHeight),
                              cv::Scalar(0, 0, 0), true, false, CV_32F);
        
        // 设置输入
        m_net.setInput(blob);
        
        // 前向传播
        std::vector<cv::Mat> outputs;
        m_net.forward(outputs, m_net.getUnconnectedOutLayersNames());
        
        // 后处理输出
        detections = postprocess(outputs, frame.cols, frame.rows, confThreshold);
        
    } catch (const cv::Exception& e) {
        LOG_ERROR("检测过程中发生OpenCV异常: " + std::string(e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("检测过程中发生异常: " + std::string(e.what()));
    }
    
    return detections;
}

void YOLODetector::setClassNames(const std::vector<std::string>& classNames) {
    m_classNames = classNames;
}

std::string YOLODetector::getClassName(int classId) const {
    int adjustedId = adjustClassId(classId);
    if (adjustedId >= 0 && adjustedId < static_cast<int>(m_classNames.size())) {
        return m_classNames[adjustedId];
    }
    return "unknown_" + std::to_string(classId);
}

cv::Mat YOLODetector::preprocess(const cv::Mat& frame) {
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(m_inputWidth, m_inputHeight));
    return resized;
}

std::vector<Detection> YOLODetector::postprocess(
    const std::vector<cv::Mat>& outputs, 
    int imgWidth, int imgHeight,
    float confThreshold) {
    
    std::vector<Detection> detections;
    
    if (outputs.empty()) {
        return detections;
    }
    
    // YOLOv8/v10 输出格式通常是 [1, 84, 8400] (84 = 4 bbox + 80 classes)
    // 或者 [1, 8400, 84]
    // 这里使用OpenCV DNN的输出格式处理
    
    // 计算缩放因子
    float xScale = static_cast<float>(imgWidth) / m_inputWidth;
    float yScale = static_cast<float>(imgHeight) / m_inputHeight;
    
    // 处理每个输出层
    for (const auto& output : outputs) {
        // 输出格式: [batch, num_proposals, num_classes + 4]
        // 通常格式为: [1, 8400, 84] 或 [1, 25200, 84]
        
        // 获取输出尺寸
        if (output.dims < 2) {
            continue;
        }
        
        int numProposals = output.size[1];  // 8400 或类似
        int numClasses = output.size[2] - 4;  // 通常是 80，这里是10类
        
        // 遍历所有检测框
        for (int i = 0; i < numProposals; ++i) {
            // 获取bbox坐标 (center_x, center_y, width, height)
            float* data = (float*)output.data + i * output.size[2];
            float centerX = data[0];
            float centerY = data[1];
            float width = data[2];
            float height = data[3];
            
            // 转换为左上角和右下角坐标
            int x1 = static_cast<int>((centerX - width / 2) * xScale);
            int y1 = static_cast<int>((centerY - height / 2) * yScale);
            int x2 = static_cast<int>((centerX + width / 2) * xScale);
            int y2 = static_cast<int>((centerY + height / 2) * yScale);
            
            // 限制在图像范围内
            x1 = std::max(0, std::min(x1, imgWidth - 1));
            y1 = std::max(0, std::min(y1, imgHeight - 1));
            x2 = std::max(0, std::min(x2, imgWidth - 1));
            y2 = std::max(0, std::min(y2, imgHeight - 1));
            
            // 找到置信度最高的类别
            int bestClassId = 0;
            float bestConf = data[4];  // 第一个类别的置信度
            
            for (int c = 1; c < numClasses && c < 10; ++c) {
                float conf = data[4 + c];
                if (conf > bestConf) {
                    bestConf = conf;
                    bestClassId = c;
                }
            }
            
            // 检查置信度阈值
            if (bestConf < confThreshold) {
                continue;
            }
            
            // 创建检测结果
            Detection det;
            det.x1 = x1;
            det.y1 = y1;
            det.x2 = x2;
            det.y2 = y2;
            det.confidence = bestConf;
            det.classId = bestClassId;
            det.className = getClassName(bestClassId);
            
            detections.push_back(det);
        }
    }
    
    // TODO: 实现NMS (Non-Maximum Suppression) 来去除重复检测框
    // 这里暂时返回所有检测结果
    
    return detections;
}

int YOLODetector::adjustClassId(int classId) const {
    // 根据Python代码中的逻辑调整类别ID
    // Python代码中的逻辑:
    // adjusted_cls_ids = np.where(boxes_cls <= 4, boxes_cls + 5, 
    //                            np.where(boxes_cls <= 9, boxes_cls - 5, boxes_cls))
    
    // 转换为C++逻辑:
    if (classId <= 4) {
        return classId + 5;  // blue目标映射
    } else if (classId <= 9) {
        return classId - 5;  // red目标映射
    } else {
        return classId;      // 其他保持不变
    }
}

} // namespace rm_auto_attack
