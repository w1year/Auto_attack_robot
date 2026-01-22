#include "gimbal/gimbal_controller.h"
#include "utils/logger.h"
#include <chrono>
#include <thread>

namespace rm_auto_attack {

GimbalController::GimbalController()
    : m_initialized(false),
      m_running(false),
      m_picAngle(11000),
      m_yawAngle(20000),
      m_shootStatus(0),
      m_idleAngle(0),
      m_canSetId(0x601),
      m_receivedPic(0),
      m_receivedYaw(0),
      m_receivedShoot(0),
      m_receivedIdle(0) {
}

GimbalController::~GimbalController() {
    close();
}

bool GimbalController::initialize(const std::string& serialPort, int baudRate) {
    if (m_initialized.load()) {
        LOG_WARNING("云台已经初始化");
        return true;
    }
    
    LOG_INFO("开始初始化云台控制器...");
    
    // 尝试打开串口
    std::vector<std::string> ports = {
        serialPort,
        "/dev/ttyUSB0",
        "/dev/ttyACM1",
        "/dev/ttyUSB1",
        "COM3",
        "COM4"
    };
    
    bool opened = false;
    for (const auto& port : ports) {
        if (m_serial.open(port, baudRate)) {
            opened = true;
            LOG_INFO("成功打开串口: " + port);
            break;
        }
    }
    
    if (!opened) {
        LOG_ERROR("无法打开任何串口设备");
        return false;
    }
    
    // 配置USB-CAN速率
    if (!configureUSBCANRate()) {
        LOG_ERROR("配置USB-CAN速率失败");
        m_serial.close();
        return false;
    }
    
    // 确保射击状态为关闭
    m_shootStatus = 0;
    sendCommand();
    
    // 启动接收线程
    startReceiveThread();
    
    m_initialized = true;
    LOG_INFO("云台初始化完成");
    return true;
}

void GimbalController::close() {
    if (!m_initialized.load()) {
        return;
    }
    
    LOG_INFO("关闭云台控制器...");
    
    // 停止接收线程
    stopReceiveThread();
    
    // 确保射击状态为关闭
    m_shootStatus = 0;
    sendCommand();
    
    // 关闭串口
    m_serial.close();
    
    m_initialized = false;
    LOG_INFO("云台控制器已关闭");
}

bool GimbalController::setPicAngle(int angle) {
    m_picAngle = clampAngle(angle);
    // 移除频繁的日志输出，减少日志量
    return sendCommand();
}

bool GimbalController::setYawAngle(int angle) {
    m_yawAngle = clampAngle(angle);
    // 移除频繁的日志输出，减少日志量
    return sendCommand();
}

bool GimbalController::setShootStatus(uint16_t status) {
    m_shootStatus = status;
    // 移除频繁的日志输出，减少日志量
    return sendCommand();
}

bool GimbalController::setIdleAngle(int angle) {
    m_idleAngle = clampAngle(angle);
    LOG_INFO("设置闲置角度: " + std::to_string(m_idleAngle.load()));
    return sendCommand();
}

bool GimbalController::sendCommand() {
    if (!m_initialized.load()) {
        LOG_ERROR("云台未初始化，无法发送命令");
        return false;
    }
    
    // 构建CAN帧
    std::vector<uint8_t> frame = CANProtocol::buildUSBCANFrame(
        m_canSetId,
        static_cast<uint16_t>(m_picAngle.load()),
        static_cast<uint16_t>(m_yawAngle.load()),
        m_shootStatus.load(),
        static_cast<uint16_t>(m_idleAngle.load())
    );
    
    // 发送数据
    if (!m_serial.send(frame)) {
        LOG_ERROR("发送云台命令失败");
        return false;
    }
    
    return true;
}

void GimbalController::triggerShoot() {
    // 移除频繁的日志输出，开始射击日志在main.cpp中已输出
    setShootStatus(1);
}

void GimbalController::stopShoot() {
    // 移除频繁的日志输出，减少日志量
    setShootStatus(0);
}

void GimbalController::startReceiveThread() {
    if (m_receiveThread && m_receiveThread->joinable()) {
        return;  // 线程已经在运行
    }
    
    m_running = true;
    m_receiveThread = std::make_unique<std::thread>(&GimbalController::receiveThreadFunc, this);
    LOG_INFO("启动云台接收线程");
}

void GimbalController::stopReceiveThread() {
    if (!m_receiveThread) {
        return;
    }
    
    m_running = false;
    
    if (m_receiveThread->joinable()) {
        m_receiveThread->join();
    }
    
    m_receiveThread.reset();
    LOG_INFO("停止云台接收线程");
}

void GimbalController::receiveThreadFunc() {
    LOG_INFO("云台接收线程已启动");
    
    std::vector<uint8_t> buffer;
    buffer.resize(1024);
    
    while (m_running.load()) {
        // 检查是否有可用数据
        size_t available = m_serial.available();
        if (available < 15) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        // 接收数据
        if (!m_serial.receive(buffer, available + 1)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        // 解析CAN ID为07FF的数据
        uint16_t pic, yaw, shoot, idle;
        if (CANProtocol::parseCAN07FF(buffer, pic, yaw, shoot, idle)) {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_receivedPic = pic;
            m_receivedYaw = yaw;
            m_receivedShoot = shoot;
            m_receivedIdle = idle;
            
            // 每20次更新输出一次日志
            static int counter = 0;
            if (++counter % 20 == 0) {
                LOG_INFO("接收到CAN数据: pic=" + std::to_string(pic) +
                        ", yaw=" + std::to_string(yaw) +
                        ", shoot=" + std::to_string(shoot) +
                        ", idle=" + std::to_string(idle));
                counter = 0;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    LOG_INFO("云台接收线程已停止");
}

bool GimbalController::configureUSBCANRate() {
    // 构建USB-CAN速率设置帧
    std::vector<uint8_t> rateFrame = CANProtocol::buildUSBCANRateFrame(0);  // 1000kbps
    
    // 发送速率设置命令
    if (!m_serial.send(rateFrame)) {
        LOG_ERROR("发送USB-CAN速率设置失败");
        return false;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("USB-CAN速率已设置为1000kbps");
    return true;
}

int GimbalController::clampAngle(int angle) const {
    if (angle < 0) return 0;
    if (angle > 30000) return 30000;
    return angle;
}

void GimbalController::getReceivedStatus(uint16_t& pic, uint16_t& yaw, uint16_t& shoot, uint16_t& idle) const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    pic = m_receivedPic;
    yaw = m_receivedYaw;
    shoot = m_receivedShoot;
    idle = m_receivedIdle;
}

} // namespace rm_auto_attack
