#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace rm_auto_attack {

// CAN协议相关结构体
struct CANFrame {
    uint32_t id;              // CAN ID
    uint8_t data[8];          // CAN数据 (最多8字节)
    uint8_t length;           // 数据长度
    bool isExtended;          // 是否为扩展帧
};

class CANProtocol {
public:
    // 构建USB-CAN消息帧
    // 根据Python代码中的USB_CAN_SET函数实现
    static std::vector<uint8_t> buildUSBCANFrame(
        uint32_t canId,
        uint16_t picAngle,      // 俯仰角度
        uint16_t yawAngle,      // 偏航角度
        uint16_t shootStatus,   // 发射状态
        uint16_t idleAngle      // 闲置角度
    );
    
    // 解析USB-CAN消息帧
    // 解析接收到的CAN数据
    static bool parseUSBCANFrame(
        const std::vector<uint8_t>& frame,
        uint32_t& canId,
        uint16_t& data1,
        uint16_t& data2,
        uint16_t& data3,
        uint16_t& data4
    );
    
    // 设置USB-CAN速率
    // 根据Python代码中的SET_USB_CAN_RATE函数实现
    static std::vector<uint8_t> buildUSBCANRateFrame(uint8_t rateIndex = 0);
    
    // 筛选CAN ID (根据Python代码中的FILTER_CAN_ID函数)
    static bool filterCANID(const std::vector<uint8_t>& data, uint32_t targetId);
    
    // 解析CAN ID为07FF的数据
    static bool parseCAN07FF(
        const std::vector<uint8_t>& data,
        uint16_t& pic,
        uint16_t& yaw,
        uint16_t& shoot,
        uint16_t& idle
    );
    
    // 解析CAN ID为0x7FE的数据
    static bool parseCAN7FE(
        const std::vector<uint8_t>& data,
        uint16_t& data1,
        uint16_t& data2,
        uint8_t& data3,
        uint8_t& data4
    );
    
    // 十六进制转二进制字符串 (根据Python代码中的hex_to_bin函数)
    static std::string hexToBin(const std::string& hexStr);
    
    // 构建透传CAN消息 (根据Python代码中的CAN_message函数)
    static std::vector<uint8_t> buildTransparentCANFrame(
        const std::string& canId,
        const std::string& canData
    );

private:
    // 内部辅助函数
    static uint16_t bytesToUint16(uint8_t low, uint8_t high);
    static void uint16ToBytes(uint16_t value, uint8_t& low, uint8_t& high);
};

} // namespace rm_auto_attack
