#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

namespace rm_auto_attack {

class SerialComm {
public:
    SerialComm();
    ~SerialComm();
    
    // 禁用拷贝构造和赋值
    SerialComm(const SerialComm&) = delete;
    SerialComm& operator=(const SerialComm&) = delete;
    
    // 打开串口
    bool open(const std::string& port, int baudRate = 115200);
    
    // 关闭串口
    void close();
    
    // 发送数据
    bool send(const std::vector<uint8_t>& data);
    
    // 发送字节数组
    bool send(const uint8_t* data, size_t length);
    
    // 接收数据
    bool receive(std::vector<uint8_t>& data, size_t maxLength = 1024);
    
    // 检查串口是否打开
    bool isOpen() const { return m_isOpen; }
    
    // 获取可用数据量
    size_t available() const;
    
    // 清空接收缓冲区
    void flush();

private:
    void* m_handle;  // 串口句柄 (Windows: HANDLE, Linux: int)
    bool m_isOpen;
    std::string m_port;
    int m_baudRate;
    mutable std::mutex m_mutex;
    
    // 平台特定的打开/关闭/发送/接收函数
    bool openPort();
    void closePort();
    bool sendData(const uint8_t* data, size_t length);
    bool receiveData(uint8_t* data, size_t maxLength, size_t& received);
    size_t getAvailableBytes() const;
};

} // namespace rm_auto_attack
