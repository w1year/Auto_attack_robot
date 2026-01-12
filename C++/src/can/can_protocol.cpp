#include "can/can_protocol.h"
#include "utils/logger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace rm_auto_attack {

std::vector<uint8_t> CANProtocol::buildUSBCANFrame(
    uint32_t canId,
    uint16_t picAngle,
    uint16_t yawAngle,
    uint16_t shootStatus,
    uint16_t idleAngle) {
    
    std::vector<uint8_t> frame;
    
    // USB_CAN帧的AT帧头
    frame.push_back(0x55);
    frame.push_back(0xAA);
    
    // USB_CAN帧的帧长
    frame.push_back(0x1E);
    
    // USB_CAN帧的命令 (01:转发CAN数据帧)
    frame.push_back(0x01);
    
    // USB_CAN的发送次数:从低位到高位
    frame.push_back(0x01);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(0x00);
    
    // USB_CAN的时间间隔:从低位到高位
    frame.push_back(0x0A);
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(0x00);
    
    // USB_CAN的ID类型 (00:标准帧)
    frame.push_back(0x00);
    
    // USB_CAN的ID:从低位到高位 (小端序)
    frame.push_back((canId >> 0) & 0xFF);
    frame.push_back((canId >> 8) & 0xFF);
    frame.push_back((canId >> 16) & 0xFF);
    frame.push_back((canId >> 24) & 0xFF);
    
    // USB_CAN的帧类型 (00:数据帧)
    frame.push_back(0x00);
    
    // USB_CAN的数据长度
    frame.push_back(0x08);
    
    // USB_CAN的IDACC
    frame.push_back(0x00);
    
    // USB_CAN的dataACC
    frame.push_back(0x00);
    
    // USB_CAN的data:从低位到高位
    uint8_t low, high;
    
    // pic轴角度 (first_data)
    uint16ToBytes(picAngle, low, high);
    frame.push_back(low);
    frame.push_back(high);
    
    // yao轴角度 (second_data)
    uint16ToBytes(yawAngle, low, high);
    frame.push_back(low);
    frame.push_back(high);
    
    // 发射状态 (third_data)
    uint16ToBytes(shootStatus, low, high);
    frame.push_back(low);
    frame.push_back(high);
    
    // 闲置角度 (fourth_data)
    uint16ToBytes(idleAngle, low, high);
    frame.push_back(low);
    frame.push_back(high);
    
    // USB_CAN的帧尾
    frame.push_back(0x88);
    
    return frame;
}

bool CANProtocol::parseUSBCANFrame(
    const std::vector<uint8_t>& frame,
    uint32_t& canId,
    uint16_t& data1,
    uint16_t& data2,
    uint16_t& data3,
    uint16_t& data4) {
    
    if (frame.size() < 30) {
        return false;
    }
    
    // 检查帧头
    if (frame[0] != 0x55 || frame[1] != 0xAA) {
        return false;
    }
    
    // 检查帧尾
    if (frame[29] != 0x88) {
        return false;
    }
    
    // 解析CAN ID (小端序)
    canId = static_cast<uint32_t>(frame[13]) |
           (static_cast<uint32_t>(frame[14]) << 8) |
           (static_cast<uint32_t>(frame[15]) << 16) |
           (static_cast<uint32_t>(frame[16]) << 24);
    
    // 解析数据 (假设格式与buildUSBCANFrame对应)
    // 根据实际协议调整
    
    return true;
}

std::vector<uint8_t> CANProtocol::buildUSBCANRateFrame(uint8_t rateIndex) {
    std::vector<uint8_t> frame;
    
    // USB_CAN帧的AT帧头
    frame.push_back(0x55);
    frame.push_back(0x05);
    
    // CAN_RATE (00 = 1000kbps, 01 = 800kbps, etc.)
    frame.push_back(rateIndex);
    
    // USB_CAN的帧尾
    frame.push_back(0xAA);
    frame.push_back(0x55);
    
    return frame;
}

bool CANProtocol::filterCANID(const std::vector<uint8_t>& data, uint32_t targetId) {
    if (data.size() < 15) {
        return false;
    }
    
    // 根据Python代码，CAN ID在data[3]和data[4] (小端序)
    // 0x07FF: data[3] = 0xFF, data[4] = 0x07
    uint16_t canId = static_cast<uint16_t>(data[3]) |
                     (static_cast<uint16_t>(data[4]) << 8);
    
    return (canId == (targetId & 0xFFFF));
}

bool CANProtocol::parseCAN07FF(
    const std::vector<uint8_t>& data,
    uint16_t& pic,
    uint16_t& yaw,
    uint16_t& shoot,
    uint16_t& idle) {
    
    if (data.size() < 15) {
        return false;
    }
    
    // 检查CAN ID为07FF
    if (!filterCANID(data, 0x07FF)) {
        return false;
    }
    
    // 解析数据 (根据Python代码中的逻辑)
    // data1 = data[7] << 8 | data[8]
    pic = bytesToUint16(data[8], data[7]);
    
    // data2 = data[9] << 8 | data[10]
    yaw = bytesToUint16(data[10], data[9]);
    
    // data3 = data[11] << 8 | data[12]
    shoot = bytesToUint16(data[12], data[11]);
    
    // data4 = data[13] << 8 | data[14]
    idle = bytesToUint16(data[14], data[13]);
    
    return true;
}

bool CANProtocol::parseCAN7FE(
    const std::vector<uint8_t>& data,
    uint16_t& data1,
    uint16_t& data2,
    uint8_t& data3,
    uint8_t& data4) {
    
    if (data.size() < 13) {
        return false;
    }
    
    // 检查CAN ID为0x7FE
    // 根据Python代码，CAN ID在data[3]和data[4] (小端序)
    // 0x7FE: data[3] = 0xFE, data[4] = 0x07
    if (data[3] != 0xFE || data[4] != 0x07) {
        return false;
    }
    
    // 解析数据 (根据Python代码中的receive_can_7FE函数)
    // data1 = data[7] << 8 | data[8]
    data1 = bytesToUint16(data[8], data[7]);
    
    // data2 = data[9] << 8 | data[10]
    data2 = bytesToUint16(data[10], data[9]);
    
    // data3 = data[11] (8位)
    data3 = data[11];
    
    // data4 = data[12] (8位)
    data4 = data[12];
    
    return true;
}

std::string CANProtocol::hexToBin(const std::string& hexStr) {
    // 将十六进制字符串转换为整数
    unsigned long num = std::stoul(hexStr, nullptr, 16);
    
    // 转换为二进制字符串
    std::string binStr;
    while (num > 0) {
        binStr = (num % 2 == 0 ? "0" : "1") + binStr;
        num /= 2;
    }
    
    // 左移一位 (相当于在末尾加0)
    binStr += "0";
    
    // 确保二进制字符串长度是4的倍数
    size_t targetLength = hexStr.length() * 4;
    while (binStr.length() < targetLength) {
        binStr = "0" + binStr;
    }
    
    // 转换回十六进制
    unsigned long shiftedNum = std::stoul(binStr, nullptr, 2);
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << shiftedNum;
    std::string shiftedHex = ss.str();
    
    // 分成两位一组，用空格连接
    std::string result;
    for (size_t i = 0; i < shiftedHex.length(); i += 2) {
        if (i > 0) result += " ";
        result += shiftedHex.substr(i, 2);
    }
    
    return result;
}

std::vector<uint8_t> CANProtocol::buildTransparentCANFrame(
    const std::string& canId,
    const std::string& canData) {
    
    std::vector<uint8_t> frame;
    
    // 透传-CAN帧的AT帧头
    frame.push_back(0x41);  // 'A'
    frame.push_back(0x54);  // 'T'
    
    // 透传-CAN帧的ID (使用hexToBin转换)
    std::string canIdBin = hexToBin(canId);
    std::istringstream iss(canIdBin);
    std::string byteStr;
    while (iss >> byteStr) {
        frame.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
    }
    
    // 透传-CAN帧的数据长度
    std::istringstream dataIss(canData);
    size_t dataLength = 0;
    while (dataIss >> byteStr) {
        dataLength++;
    }
    frame.push_back(static_cast<uint8_t>(dataLength));
    
    // 透传-CAN帧的数据
    std::istringstream dataIss2(canData);
    while (dataIss2 >> byteStr) {
        frame.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
    }
    
    // 透传-CAN帧的帧尾
    frame.push_back(0x0D);  // '\r'
    frame.push_back(0x0A);  // '\n'
    
    return frame;
}

uint16_t CANProtocol::bytesToUint16(uint8_t low, uint8_t high) {
    return static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
}

void CANProtocol::uint16ToBytes(uint16_t value, uint8_t& low, uint8_t& high) {
    low = value & 0xFF;
    high = (value >> 8) & 0xFF;
}

} // namespace rm_auto_attack
