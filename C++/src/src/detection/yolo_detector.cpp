#include "detection/yolo_detector.h"
#include "utils/logger.h"
#include <opencv2/dnn.hpp>
#include <opencv2/core/cuda.hpp>
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
        bool cudaAvailable = false;
        
        // 检查OpenCV是否支持CUDA
        int backendCount = cv::dnn::getAvailableBackends().size();
        LOG_INFO("可用DNN后端数量: " + std::to_string(backendCount));
        
        // 尝试设置CUDA后端
        try {
            // 检查CUDA后端是否可用
            std::vector<std::pair<cv::dnn::Backend, cv::dnn::Target>> backends = cv::dnn::getAvailableBackends();
            bool cudaBackendFound = false;
            for (const auto& backend : backends) {
                if (backend.first == cv::dnn::DNN_BACKEND_CUDA && 
                    backend.second == cv::dnn::DNN_TARGET_CUDA) {
                    cudaBackendFound = true;
                    break;
                }
            }
            
            if (cudaBackendFound) {
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                cudaAvailable = true;
                LOG_INFO("✓ CUDA后端已启用，将使用GPU加速推理");
                
                // 获取CUDA设备信息（如果可用）
                try {
                    cv::cuda::printCudaDeviceInfo(0);
                } catch (...) {
                    // 忽略CUDA设备信息获取失败
                }
            } else {
                LOG_WARNING("CUDA后端不可用，将使用CPU后端");
                m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
                m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            }
        } catch (const cv::Exception& e) {
            LOG_WARNING("设置CUDA后端时发生异常: " + std::string(e.what()));
            LOG_WARNING("回退到CPU后端");
            m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
            m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        } catch (...) {
            LOG_WARNING("CUDA后端不可用，使用CPU后端");
            m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
            m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
        
        if (!cudaAvailable) {
            LOG_INFO("使用CPU后端进行推理（如需CUDA加速，请确保OpenCV编译时启用了CUDA支持）");
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
        
        // 安全地打印blob形状
        if (blob.dims >= 4) {
            LOG_DEBUG("Blob形状: [" + std::to_string(blob.size[0]) + ", " + 
                      std::to_string(blob.size[1]) + ", " + 
                      std::to_string(blob.size[2]) + ", " + 
                      std::to_string(blob.size[3]) + "]");
        }
        
        // 设置输入
        m_net.setInput(blob);
        
        // 前向传播
        std::vector<cv::Mat> outputs;
        std::vector<std::string> outNames = m_net.getUnconnectedOutLayersNames();
        LOG_DEBUG("输出层数量: " + std::to_string(outNames.size()));
        
        try {
            m_net.forward(outputs, outNames);
            LOG_DEBUG("前向传播成功，实际输出数量: " + std::to_string(outputs.size()));
        } catch (const cv::Exception& e) {
            LOG_ERROR("前向传播失败: " + std::string(e.what()));
            throw;  // 重新抛出异常
        }
        
        // 后处理输出
        detections = postprocess(outputs, frame.cols, frame.rows, confThreshold);
        
    } catch (const cv::Exception& e) {
        LOG_ERROR("检测过程中发生OpenCV异常: " + std::string(e.what()));
        LOG_ERROR("异常文件: " + std::string(e.file));
        LOG_ERROR("异常行号: " + std::to_string(e.line));
        LOG_ERROR("异常函数: " + std::string(e.func));
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
        // 检查输出维度
        if (output.dims < 2 || output.dims > 3) {
            LOG_WARNING("输出维度不符合预期: " + std::to_string(output.dims));
            continue;
        }
        
        // 打印输出形状用于调试
        std::string shapeStr = "输出形状: [";
        for (int d = 0; d < output.dims; ++d) {
            shapeStr += std::to_string(output.size[d]);
            if (d < output.dims - 1) shapeStr += ", ";
        }
        shapeStr += "]";
        LOG_INFO(shapeStr);
        
        // YOLOv8/v10输出格式可能是:
        // 格式1: [1, 84, 8400] - [batch, features, num_proposals]
        // 格式2: [1, 8400, 84] - [batch, num_proposals, features]
        // 格式3: [8400, 84] - [num_proposals, features] (无batch维度)
        
        int numProposals = 0;
        int numFeatures = 0;
        bool isTransposed = false;
        
        // 安全地获取维度大小
        std::vector<int> dimSizes;
        for (int d = 0; d < output.dims; ++d) {
            dimSizes.push_back(output.size[d]);
        }
        
        if (output.dims == 3) {
            // 3维: [batch, dim1, dim2]
            if (dimSizes[1] > dimSizes[2]) {
                // 格式1: [1, 84, 8400]
                numFeatures = dimSizes[1];
                numProposals = dimSizes[2];
                isTransposed = true;
            } else {
                // 格式2: [1, 8400, 84]
                numProposals = dimSizes[1];
                numFeatures = dimSizes[2];
            }
        } else if (output.dims == 2) {
            // 2维: [dim1, dim2]
            if (dimSizes[0] > dimSizes[1]) {
                // 格式: [84, 8400]
                numFeatures = dimSizes[0];
                numProposals = dimSizes[1];
                isTransposed = true;
            } else {
                // 格式: [8400, 84]
                numProposals = dimSizes[0];
                numFeatures = dimSizes[1];
            }
        }
        
        if (numProposals == 0 || numFeatures < 4) {
            LOG_WARNING("无法解析输出格式，跳过此输出层");
            continue;
        }
        
        int numClasses = numFeatures - 4;  // 4个bbox坐标 + N个类别
        
        LOG_INFO("解析结果: numProposals=" + std::to_string(numProposals) + 
                 ", numFeatures=" + std::to_string(numFeatures) + 
                 ", numClasses=" + std::to_string(numClasses) +
                 ", isTransposed=" + std::string(isTransposed ? "true" : "false"));
        
        // 遍历所有检测框
        for (int i = 0; i < numProposals; ++i) {
            float* data = nullptr;
            
            if (isTransposed) {
                // 转置格式 [1, numFeatures, numProposals]: 
                // 访问第i个proposal的第j个特征: data[j * numProposals + i]
                // 但我们需要连续访问所有特征，所以需要重新组织数据
                // 临时方案: 创建一个临时缓冲区
                std::vector<float> tempData(numFeatures);
                for (int j = 0; j < numFeatures; ++j) {
                    tempData[j] = ((float*)output.data)[j * numProposals + i];
                }
                data = tempData.data();
            } else {
                // 正常格式 [1, numProposals, numFeatures]: 按行访问
                data = (float*)output.data + i * numFeatures;
            }
            
            // 获取bbox坐标
            // YOLOv8/v10格式: [x_center, y_center, width, height] (归一化到[0,1])
            float centerX = data[0];
            float centerY = data[1];
            float width = data[2];
            float height = data[3];
            
            // 转换为像素坐标
            centerX *= m_inputWidth;
            centerY *= m_inputHeight;
            width *= m_inputWidth;
            height *= m_inputHeight;
            
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
                if (4 + c >= numFeatures) break;  // 安全检查
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
