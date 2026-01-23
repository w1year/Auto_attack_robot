#include "detection/yolo_detector_tensorrt.h"
#include "utils/logger.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace rm_auto_attack {

// TensorRT Logger实现（只输出ERROR级别，减少日志）
void TensorRTLogger::log(Severity severity, const char* msg) noexcept {
    if (severity == Severity::kERROR) {
        LOG_ERROR("TensorRT: " + std::string(msg));
    }
    // 移除WARNING和DEBUG日志，减少输出
}

YOLODetectorTensorRT::YOLODetectorTensorRT()
    : m_modelLoaded(false)
    , m_runtime(nullptr)
    , m_engine(nullptr)
    , m_context(nullptr)
    , m_inputBuffer(nullptr)
    , m_outputBuffer(nullptr)
    , m_inputSize(0)
    , m_outputSize(0)
    , m_inputWidth(640)
    , m_inputHeight(640)
    , m_outputSizeElements(0) {
    
    m_logger = std::make_unique<TensorRTLogger>();
    cudaStreamCreate(&m_stream);
    
    // 默认类别名称
    m_classNames = {
        "blue100", "blue200", "blue300", "blue400", "blue500",
        "red100", "red200", "red300", "red400", "red500"
    };
}

YOLODetectorTensorRT::~YOLODetectorTensorRT() {
    cleanup();
    if (m_stream) {
        cudaStreamDestroy(m_stream);
    }
}

void YOLODetectorTensorRT::cleanup() {
    if (m_context) {
        m_context->destroy();
        m_context = nullptr;
    }
    if (m_engine) {
        m_engine->destroy();
        m_engine = nullptr;
    }
    if (m_runtime) {
        m_runtime->destroy();
        m_runtime = nullptr;
    }
    if (m_inputBuffer) {
        cudaFree(m_inputBuffer);
        m_inputBuffer = nullptr;
    }
    if (m_outputBuffer) {
        cudaFree(m_outputBuffer);
        m_outputBuffer = nullptr;
    }
}

bool YOLODetectorTensorRT::loadModel(const std::string& modelPath, bool useFP16) {
    if (m_modelLoaded) {
        LOG_WARNING("模型已经加载");
        return true;
    }
    
    LOG_INFO("开始加载TensorRT模型: " + modelPath);
    
    // 检查文件是否存在
    std::ifstream file(modelPath);
    if (!file.good()) {
        LOG_ERROR("模型文件不存在: " + modelPath);
        return false;
    }
    file.close();
    
    // 确定文件类型
    std::string onnxPath, enginePath;
    if (modelPath.substr(modelPath.find_last_of(".") + 1) == "onnx") {
        onnxPath = modelPath;
        enginePath = modelPath.substr(0, modelPath.find_last_of(".")) + ".engine";
    } else if (modelPath.substr(modelPath.find_last_of(".") + 1) == "engine") {
        enginePath = modelPath;
    } else {
        LOG_ERROR("不支持的模型格式，请使用.onnx或.engine文件");
        return false;
    }
    
    // 尝试加载引擎文件
    bool engineExists = false;
    if (!enginePath.empty()) {
        std::ifstream engineFile(enginePath);
        engineExists = engineFile.good();
        engineFile.close();
    }
    
    // 如果引擎文件不存在，从ONNX构建
    if (!engineExists && !onnxPath.empty()) {
        // 移除构建日志
        if (!buildEngineFromONNX(onnxPath, enginePath, useFP16)) {
            LOG_ERROR("构建TensorRT引擎失败");
            return false;
        }
    }
    
    // 加载引擎
    if (!loadEngine(enginePath)) {
        LOG_ERROR("加载TensorRT引擎失败");
        return false;
    }
    
    // 分配缓冲区
    if (!allocateBuffers()) {
        LOG_ERROR("分配CUDA缓冲区失败");
        return false;
    }
    
    m_modelLoaded = true;
    // 移除加载成功日志
    return true;
}

bool YOLODetectorTensorRT::buildEngineFromONNX(const std::string& onnxPath, 
                                                const std::string& enginePath, 
                                                bool useFP16) {
    // 移除构建引擎日志
    
    // 创建builder
    nvinfer1::IBuilder* builder = nvinfer1::createInferBuilder(*m_logger);
    if (!builder) {
        LOG_ERROR("创建TensorRT builder失败");
        return false;
    }
    
    // 创建网络
    const auto explicitBatch = 1U << static_cast<uint32_t>(nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
    nvinfer1::INetworkDefinition* network = builder->createNetworkV2(explicitBatch);
    if (!network) {
        LOG_ERROR("创建TensorRT网络失败");
        builder->destroy();
        return false;
    }
    
    // 创建ONNX解析器
    nvonnxparser::IParser* parser = nvonnxparser::createParser(*network, *m_logger);
    if (!parser) {
        LOG_ERROR("创建ONNX解析器失败");
        network->destroy();
        builder->destroy();
        return false;
    }
    
    // 解析ONNX模型
    if (!parser->parseFromFile(onnxPath.c_str(), 
                               static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
        LOG_ERROR("解析ONNX模型失败");
        parser->destroy();
        network->destroy();
        builder->destroy();
        return false;
    }
    
    // 创建构建配置
    nvinfer1::IBuilderConfig* config = builder->createBuilderConfig();
    if (!config) {
        LOG_ERROR("创建构建配置失败");
        parser->destroy();
        network->destroy();
        builder->destroy();
        return false;
    }
    
    // 设置最大工作空间
    config->setMaxWorkspaceSize(1 << 30); // 1GB
    
    // 设置精度
    if (useFP16 && builder->platformHasFastFp16()) {
        config->setFlag(nvinfer1::BuilderFlag::kFP16);
        // 移除启用FP16日志
    }
    
    // 构建引擎
    // 移除开始构建日志
    nvinfer1::ICudaEngine* engine = builder->buildEngineWithConfig(*network, *config);
    if (!engine) {
        LOG_ERROR("构建TensorRT引擎失败");
        config->destroy();
        parser->destroy();
        network->destroy();
        builder->destroy();
        return false;
    }
    
    // 序列化引擎
    nvinfer1::IHostMemory* serializedEngine = engine->serialize();
    if (!serializedEngine) {
        LOG_ERROR("序列化引擎失败");
        engine->destroy();
        config->destroy();
        parser->destroy();
        network->destroy();
        builder->destroy();
        return false;
    }
    
    // 保存引擎文件
    std::ofstream engineFile(enginePath, std::ios::binary);
    if (!engineFile) {
        LOG_ERROR("无法创建引擎文件: " + enginePath);
        serializedEngine->destroy();
        engine->destroy();
        config->destroy();
        parser->destroy();
        network->destroy();
        builder->destroy();
        return false;
    }
    
    engineFile.write(reinterpret_cast<const char*>(serializedEngine->data()), 
                     serializedEngine->size());
    engineFile.close();
    
    // 移除引擎已保存日志
    
    // 清理
    serializedEngine->destroy();
    engine->destroy();
    config->destroy();
    parser->destroy();
    network->destroy();
    builder->destroy();
    
    return true;
}

bool YOLODetectorTensorRT::loadEngine(const std::string& enginePath) {
    // 移除加载引擎日志
    
    // 读取引擎文件
    std::ifstream engineFile(enginePath, std::ios::binary);
    if (!engineFile) {
        LOG_ERROR("无法打开引擎文件: " + enginePath);
        return false;
    }
    
    engineFile.seekg(0, std::ios::end);
    size_t engineSize = engineFile.tellg();
    engineFile.seekg(0, std::ios::beg);
    
    std::vector<char> engineData(engineSize);
    engineFile.read(engineData.data(), engineSize);
    engineFile.close();
    
    // 创建运行时
    m_runtime = nvinfer1::createInferRuntime(*m_logger);
    if (!m_runtime) {
        LOG_ERROR("创建TensorRT运行时失败");
        return false;
    }
    
    // 反序列化引擎
    m_engine = m_runtime->deserializeCudaEngine(engineData.data(), engineSize, nullptr);
    if (!m_engine) {
        LOG_ERROR("反序列化TensorRT引擎失败");
        m_runtime->destroy();
        m_runtime = nullptr;
        return false;
    }
    
    // 创建执行上下文
    m_context = m_engine->createExecutionContext();
    if (!m_context) {
        LOG_ERROR("创建TensorRT执行上下文失败");
        m_engine->destroy();
        m_engine = nullptr;
        m_runtime->destroy();
        m_runtime = nullptr;
        return false;
    }
    
    // 移除引擎加载成功日志
    return true;
}

bool YOLODetectorTensorRT::allocateBuffers() {
    // 获取输入输出绑定
    int numBindings = m_engine->getNbBindings();
    if (numBindings != 2) {
        LOG_ERROR("期望2个绑定（输入+输出），实际: " + std::to_string(numBindings));
        return false;
    }
    
    // 分配输入输出缓冲区
    for (int i = 0; i < numBindings; ++i) {
        nvinfer1::Dims dims = m_engine->getBindingDimensions(i);
        size_t bindingSize = 1;
        for (int j = 0; j < dims.nbDims; ++j) {
            bindingSize *= dims.d[j];
        }
        bindingSize *= sizeof(float);
        
        void* buffer;
        cudaMalloc(&buffer, bindingSize);
        
        if (m_engine->bindingIsInput(i)) {
            m_inputBuffer = buffer;
            m_inputSize = bindingSize;
            // 移除输入缓冲区大小日志
        } else {
            m_outputBuffer = buffer;
            m_outputSize = bindingSize;
            m_outputSizeElements = bindingSize / sizeof(float);
            // 移除输出缓冲区大小日志
        }
    }
    
    return true;
}

void YOLODetectorTensorRT::preprocessImage(const cv::Mat& image, float* gpuInput) {
    if (image.empty()) return;

    // ==========================================
    // 步骤 1: 计算 Letterbox 缩放比例和偏移量
    // ==========================================
    float scale = std::min((float)m_inputWidth / image.cols, (float)m_inputHeight / image.rows);
    int newUnpadW = (int)(image.cols * scale);
    int newUnpadH = (int)(image.rows * scale);
    
    // 计算黑边偏移量（让图像居中）
    int dw = (m_inputWidth - newUnpadW) / 2;
    int dh = (m_inputHeight - newUnpadH) / 2;

    // ==========================================
    // 步骤 2: 图像缩放与填充
    // ==========================================
    cv::Mat resized;
    if (image.cols != newUnpadW || image.rows != newUnpadH) {
        cv::resize(image, resized, cv::Size(newUnpadW, newUnpadH));
    } else {
        resized = image;
    }

    // 创建全黑画布 (640x640)
    cv::Mat letterboxed = cv::Mat::zeros(m_inputHeight, m_inputWidth, CV_8UC3);
    
    // 将缩放后的图拷贝到画布中心
    resized.copyTo(letterboxed(cv::Rect(dw, dh, newUnpadW, newUnpadH)));

    // ==========================================
    // 步骤 3: 预处理 (BGR->RGB, 归一化, HWC->CHW)
    // ==========================================
    cv::Mat rgb;
    cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);
    
    // 归一化 0~255 -> 0.0~1.0
    rgb.convertTo(rgb, CV_32F, 1.0);

    // 拷贝到 GPU 输入缓冲区 (并行加速)
    std::vector<float> inputData(m_inputWidth * m_inputHeight * 3);
    const int totalPixels = m_inputHeight * m_inputWidth;
    
    #ifdef _OPENMP
    #pragma omp parallel for num_threads(3)
    #endif
    for (int c = 0; c < 3; ++c) {
        float* channelData = inputData.data() + c * totalPixels;
        for (int h = 0; h < m_inputHeight; ++h) {
            for (int w = 0; w < m_inputWidth; ++w) {
                // 指针访问比 at() 更快
                const float* rowPtr = rgb.ptr<float>(h);
                channelData[h * m_inputWidth + w] = rowPtr[w * 3 + c];
            }
        }
    }
    
    cudaMemcpyAsync(gpuInput, inputData.data(), m_inputSize, 
                    cudaMemcpyHostToDevice, m_stream);
}

std::vector<Detection> YOLODetectorTensorRT::postprocessOutput(float* gpuOutput, 
    int imgWidth, 
    int imgHeight, 
    float confThreshold) {
std::vector<Detection> detections;

// ==========================================
// 步骤 1: 获取数据并计算还原参数
// ==========================================
std::vector<float> outputData(m_outputSizeElements);
cudaMemcpyAsync(outputData.data(), gpuOutput, m_outputSize, 
cudaMemcpyDeviceToHost, m_stream);
cudaStreamSynchronize(m_stream);

// 定义模型输出维度 (根据之前的日志确认是 8400 个锚点)
int numClasses = 10; 
int numAnchors = 8400; 

// 必须与 preprocessImage 中的逻辑完全一致
float scale = std::min((float)m_inputWidth / imgWidth, (float)m_inputHeight / imgHeight);
int newUnpadW = (int)(imgWidth * scale);
int newUnpadH = (int)(imgHeight * scale);
int dw = (m_inputWidth - newUnpadW) / 2;
int dh = (m_inputHeight - newUnpadH) / 2;

std::vector<Detection> tempDetections;
tempDetections.reserve(numAnchors);

// ==========================================
// 步骤 2: 解析所有预测框
// ==========================================
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 100)
#endif
for (int i = 0; i < numAnchors; ++i) {
// 寻找该锚点的最高置信度类别
// outputData layout: [channels, anchors]
// channels 0-3: bbox, 4-13: class scores
int bestClassId = -1;
float bestConf = -1.0f;

for (int c = 0; c < numClasses; ++c) {
float conf = outputData[(4 + c) * numAnchors + i];
if (conf > bestConf) {
bestConf = conf;
bestClassId = c;
}
}

if (bestConf < confThreshold) {
continue;
}

// 获取并还原坐标
float cx = outputData[0 * numAnchors + i];
float cy = outputData[1 * numAnchors + i];
float w  = outputData[2 * numAnchors + i];
float h  = outputData[3 * numAnchors + i];

// --- 核心修改：反向映射坐标 (Remove Padding & Scale) ---
cx = (cx - dw) / scale;
cy = (cy - dh) / scale;
w  = w / scale;
h  = h / scale;

// 转为左上角坐标
int x1 = static_cast<int>(cx - w / 2);
int y1 = static_cast<int>(cy - h / 2);
int x2 = static_cast<int>(cx + w / 2);
int y2 = static_cast<int>(cy + h / 2);

// 边界限制
x1 = std::max(0, std::min(x1, imgWidth - 1));
y1 = std::max(0, std::min(y1, imgHeight - 1));
x2 = std::max(0, std::min(x2, imgWidth - 1));
y2 = std::max(0, std::min(y2, imgHeight - 1));

Detection det;
det.x1 = x1;
det.y1 = y1;
det.x2 = x2;
det.y2 = y2;
det.confidence = bestConf;
det.classId = bestClassId;
det.className = getClassName(bestClassId);

#ifdef _OPENMP
#pragma omp critical
#endif
{
tempDetections.push_back(det);
}
}

// ==========================================
// 步骤 3: NMS (非极大值抑制)
// ==========================================
// 先按置信度排序
std::sort(tempDetections.begin(), tempDetections.end(), 
[](const Detection& a, const Detection& b) {
return a.confidence > b.confidence;
});

std::vector<bool> isSuppressed(tempDetections.size(), false);
float nmsThreshold = 0.45f; // NMS 阈值

for (size_t i = 0; i < tempDetections.size(); ++i) {
if (isSuppressed[i]) continue;
detections.push_back(tempDetections[i]);

for (size_t j = i + 1; j < tempDetections.size(); ++j) {
if (isSuppressed[j]) continue;

// 计算 IoU
int xx1 = std::max(tempDetections[i].x1, tempDetections[j].x1);
int yy1 = std::max(tempDetections[i].y1, tempDetections[j].y1);
int xx2 = std::min(tempDetections[i].x2, tempDetections[j].x2);
int yy2 = std::min(tempDetections[i].y2, tempDetections[j].y2);

int w_inter = std::max(0, xx2 - xx1);
int h_inter = std::max(0, yy2 - yy1);
int interArea = w_inter * h_inter;

int area1 = (tempDetections[i].x2 - tempDetections[i].x1) * (tempDetections[i].y2 - tempDetections[i].y1);
int area2 = (tempDetections[j].x2 - tempDetections[j].x1) * (tempDetections[j].y2 - tempDetections[j].y1);

float iou = static_cast<float>(interArea) / (area1 + area2 - interArea + 1e-6);

if (iou > nmsThreshold) {
isSuppressed[j] = true;
}
}
}

return detections;
}




std::vector<Detection> YOLODetectorTensorRT::detect(const cv::Mat& frame, float confThreshold) {
    std::vector<Detection> detections;
    
    if (!m_modelLoaded) {
        LOG_ERROR("模型未加载，无法执行检测");
        return detections;
    }
    
    if (frame.empty()) {
        // 移除输入图像为空警告
        return detections;
    }
    
    try {
        // 预处理
        preprocessImage(frame, static_cast<float*>(m_inputBuffer));
        
        // 执行推理（异步，提高GPU利用率）
        void* bindings[] = {m_inputBuffer, m_outputBuffer};
        bool success = m_context->enqueueV2(bindings, m_stream, nullptr);
        if (!success) {
            LOG_ERROR("TensorRT推理失败");
            return detections;
        }
        
        // 延迟同步：先启动后处理准备，在真正需要结果时才同步
        // 这样可以充分利用GPU和CPU并行工作
        // cudaStreamSynchronize会在postprocessOutput中执行
        // 但我们可以先做其他准备工作
        
        // 后处理（在CPU上并行处理，内部会同步GPU）
        detections = postprocessOutput(static_cast<float*>(m_outputBuffer), 
                                      frame.cols, frame.rows, confThreshold);
        
    } catch (const std::exception& e) {
        LOG_ERROR("检测过程中发生异常: " + std::string(e.what()));
    }
    
    return detections;
}

void YOLODetectorTensorRT::setClassNames(const std::vector<std::string>& classNames) {
    m_classNames = classNames;
}

std::string YOLODetectorTensorRT::getClassName(int classId) const {
    int adjustedId = adjustClassId(classId);
    if (adjustedId >= 0 && adjustedId < static_cast<int>(m_classNames.size())) {
        return m_classNames[adjustedId];
    }
    return "unknown_" + std::to_string(classId);
}

int YOLODetectorTensorRT::adjustClassId(int classId) const {

        return classId;

}

void YOLODetectorTensorRT::warmup(int iterations) {
    if (!m_modelLoaded) return;
    
    // 移除预热日志
    cv::Mat dummy = cv::Mat::zeros(m_inputHeight, m_inputWidth, CV_8UC3);
    
    for (int i = 0; i < iterations; ++i) {
        detect(dummy, 0.3f);
    }
    
    // 移除预热完成日志
}

} // namespace rm_auto_attack

