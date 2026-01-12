#include "serial/serial_comm.h"
#include "utils/logger.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>
#endif

namespace rm_auto_attack {

SerialComm::SerialComm() 
    : m_handle(nullptr), m_isOpen(false), m_baudRate(115200) {
#ifdef _WIN32
    m_handle = INVALID_HANDLE_VALUE;
#else
    m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
#endif
}

SerialComm::~SerialComm() {
    close();
}

bool SerialComm::open(const std::string& port, int baudRate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_isOpen) {
        LOG_WARNING("串口已经打开");
        close();
    }
    
    m_port = port;
    m_baudRate = baudRate;
    
    if (!openPort()) {
        LOG_ERROR("打开串口失败: " + port);
        return false;
    }
    
    m_isOpen = true;
    LOG_INFO("成功打开串口: " + port + ", 波特率: " + std::to_string(baudRate));
    return true;
}

void SerialComm::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_isOpen) {
        return;
    }
    
    closePort();
    m_isOpen = false;
    LOG_INFO("串口已关闭: " + m_port);
}

bool SerialComm::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

bool SerialComm::send(const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_isOpen) {
        LOG_ERROR("串口未打开，无法发送数据");
        return false;
    }
    
    if (!sendData(data, length)) {
        LOG_ERROR("发送数据失败");
        return false;
    }
    
    return true;
}

bool SerialComm::receive(std::vector<uint8_t>& data, size_t maxLength) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_isOpen) {
        LOG_ERROR("串口未打开，无法接收数据");
        return false;
    }
    
    data.resize(maxLength);
    size_t received = 0;
    
    if (!receiveData(data.data(), maxLength, received)) {
        data.clear();
        return false;
    }
    
    data.resize(received);
    return received > 0;
}

size_t SerialComm::available() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_isOpen) {
        return 0;
    }
    
    return getAvailableBytes();
}

void SerialComm::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_isOpen) {
        return;
    }
    
    // 清空接收缓冲区
    uint8_t buffer[1024];
    size_t received = 0;
    while (receiveData(buffer, sizeof(buffer), received) && received > 0) {
        // 继续读取直到缓冲区为空
    }
}

#ifdef _WIN32
// Windows实现
bool SerialComm::openPort() {
    std::string portName = "\\\\.\\" + m_port;
    
    m_handle = CreateFileA(
        portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );
    
    if (m_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    
    if (!GetCommState(static_cast<HANDLE>(m_handle), &dcb)) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    
    dcb.BaudRate = m_baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    
    if (!SetCommState(static_cast<HANDLE>(m_handle), &dcb)) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    if (!SetCommTimeouts(static_cast<HANDLE>(m_handle), &timeouts)) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    
    return true;
}

void SerialComm::closePort() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool SerialComm::sendData(const uint8_t* data, size_t length) {
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_handle), data, 
                   static_cast<DWORD>(length), &written, nullptr)) {
        return false;
    }
    return written == length;
}

bool SerialComm::receiveData(uint8_t* data, size_t maxLength, size_t& received) {
    DWORD read = 0;
    if (!ReadFile(static_cast<HANDLE>(m_handle), data, 
                  static_cast<DWORD>(maxLength), &read, nullptr)) {
        received = 0;
        return false;
    }
    received = read;
    return true;
}

size_t SerialComm::getAvailableBytes() const {
    DWORD errors = 0;
    COMSTAT status;
    if (!ClearCommError(static_cast<HANDLE>(m_handle), &errors, &status)) {
        return 0;
    }
    return status.cbInQue;
}

#else
// Linux/Unix实现
bool SerialComm::openPort() {
    int fd = ::open(m_port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    
    termios options;
    if (tcgetattr(fd, &options) != 0) {
        ::close(fd);
        m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        return false;
    }
    
    // 设置波特率
    speed_t speed = B115200;
    switch (m_baudRate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: speed = B115200; break;
    }
    
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    // 设置数据位、停止位、校验位
    options.c_cflag &= ~PARENB;  // 无校验
    options.c_cflag &= ~CSTOPB;  // 1个停止位
    options.c_cflag &= ~CSIZE;   // 清除数据位设置
    options.c_cflag |= CS8;      // 8个数据位
    options.c_cflag |= (CLOCAL | CREAD);  // 本地连接，接收使能
    
    // 原始模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    
    // 设置超时
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;  // 0.1秒超时
    
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        ::close(fd);
        m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        return false;
    }
    
    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);
    
    return true;
}

void SerialComm::closePort() {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
    if (fd >= 0) {
        ::close(fd);
        m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
}

bool SerialComm::sendData(const uint8_t* data, size_t length) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
    ssize_t written = ::write(fd, data, length);
    return written == static_cast<ssize_t>(length);
}

bool SerialComm::receiveData(uint8_t* data, size_t maxLength, size_t& received) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
    ssize_t bytesRead = ::read(fd, data, maxLength);
    if (bytesRead < 0) {
        received = 0;
        return false;
    }
    received = bytesRead;
    return true;
}

size_t SerialComm::getAvailableBytes() const {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
    int available = 0;
    if (::ioctl(fd, FIONREAD, &available) == 0) {
        return available;
    }
    return 0;
}

#endif

} // namespace rm_auto_attack
