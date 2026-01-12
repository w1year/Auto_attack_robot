#pragma once

#include "serial/serial_comm.h"
#include "can/can_protocol.h"
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <memory>

namespace rm_auto_attack {

class GimbalController {
public:
    GimbalController();
    ~GimbalController();
    
    // 禁用拷贝构造和赋值
    GimbalController(const GimbalController&) = delete;
    GimbalController& operator=(const GimbalController&) = delete;
    
    // 初始化云台
    bool initialize(const std::string& serialPort = "/dev/ttyACM0", int baudRate = 115200);
    
    // 关闭云台
    void close();
    
    // 设置俯仰角度 (0-30000, 0为水平，30000约25度)
    bool setPicAngle(int angle);
    
    // 设置偏航角度 (0-30000, 15000为正西，往右减，往左加)
    bool setYawAngle(int angle);
    
    // 设置发射状态 (0:停止, 1:发射)
    bool setShootStatus(uint16_t status);
    
    // 设置闲置角度
    bool setIdleAngle(int angle);
    
    // 发送控制命令
    bool sendCommand();
    
    // 触发射击
    void triggerShoot();
    
    // 停止射击
    void stopShoot();
    
    // 获取当前角度
    int getCurrentPicAngle() const { return m_picAngle.load(); }
    int getCurrentYawAngle() const { return m_yawAngle.load(); }
    
    // 启动接收线程 (用于接收CAN数据)
    void startReceiveThread();
    
    // 停止接收线程
    void stopReceiveThread();
    
    // 获取接收到的状态 (CAN ID 07FF)
    void getReceivedStatus(uint16_t& pic, uint16_t& yaw, uint16_t& shoot, uint16_t& idle) const;

private:
    SerialComm m_serial;
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_running;
    
    // 角度参数
    std::atomic<int> m_picAngle;   // 俯仰角度
    std::atomic<int> m_yawAngle;   // 偏航角度
    std::atomic<uint16_t> m_shootStatus;  // 发射状态
    std::atomic<int> m_idleAngle;  // 闲置角度
    
    // CAN ID
    uint32_t m_canSetId;  // 发送CAN ID (0x601)
    
    // 接收到的状态 (CAN ID 07FF)
    mutable std::mutex m_statusMutex;
    uint16_t m_receivedPic;
    uint16_t m_receivedYaw;
    uint16_t m_receivedShoot;
    uint16_t m_receivedIdle;
    
    // 接收线程
    std::unique_ptr<std::thread> m_receiveThread;
    
    // 接收线程函数
    void receiveThreadFunc();
    
    // 配置USB-CAN速率
    bool configureUSBCANRate();
    
    // 限制角度范围
    int clampAngle(int angle) const;
};

} // namespace rm_auto_attack
