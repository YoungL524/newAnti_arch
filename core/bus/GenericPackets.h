#ifndef GENERICPACKETS_H
#define GENERICPACKETS_H

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <QByteArray>
#include <QString>
#include <QDateTime>
#include "StructureReceive.h"

#pragma pack(push, 1) // 强制单字节对齐

/**
 * @brief 设备类型枚举
 * @details 用于标识系统中的各种设备
 */
enum DeviceType {
    DEVICE_UNKNOWN = 0,
    DEVICE_RADAR_NH_KU_1 = 1,
    DEVICE_RADAR_NH_KU_2 = 2,
    DEVICE_RADAR_NH_X_1 = 3,
    DEVICE_RADAR_NH_X_2 = 4,
    DEVICE_RADAR_NH_S_1 = 5,
    DEVICE_RADAR_NH_S_2 = 6
};

/**
 * @brief UI组件类型枚举
 * @details 用于标识数据的分发目标，Phase3加入GUI后使用
 */
enum UIWidget {
    UI_UNKNOWN = 10,
    UI_OUTPUTWIDGET = 11,
    UI_RADARWIDGET = 12,
    UI_CONFIGWIDGET = 13,
    UI_STATUSWIDGET = 14,
    UI_TABLEWIDGET = 15
};

/**
 * @brief 错误类型枚举
 */
enum ErrorType {
    ERR_NONE = 20,           ///< 无错误
    ERR_NETWORK = 21,        ///< 网络错误
    ERR_TIMEOUT = 22,        ///< 超时错误
    ERR_INVALID_DATA = 23,   ///< 无效数据
    ERR_SYSTEM = 24,         ///< 系统错误
    ERR_BUS = 25,            ///< 总线错误
    ERR_PROCESSOR = 26,      ///< 数据处理错误
    ERR_CONFIG = 27,         ///< 配置错误
    ERR_HARDWARE = 28        ///< 硬件错误
};

/**
 * @brief 错误级别枚举（对应日志级别）
 */
enum ErrorLevel {
    ERR_LEVEL_DEBUG = 0,     ///< 调试信息
    ERR_LEVEL_INFO = 1,      ///< 一般信息
    ERR_LEVEL_WARNING = 2,   ///< 警告
    ERR_LEVEL_ERROR = 3,     ///< 错误
    ERR_LEVEL_CRITICAL = 4   ///< 严重错误
};

/**
 * @brief 载荷类型常量
 */
const uint32_t PAYLOAD_TYPE_UNKNOWN = 0;
const uint32_t PAYLOAD_TYPE_WORK_STANDBY = 1;
const uint32_t PAYLOAD_TYPE_TARGET_TYPE = 2;
const uint32_t PAYLOAD_TYPE_GENERAL_PACKAGE = 3;
const uint32_t PAYLOAD_TYPE_LOG_TEXT = 100;
const uint32_t PAYLOAD_TYPE_ERROR = 101;        ///< 错误/告警数据
const uint32_t PAYLOAD_TYPE_CONFIG = 102;       ///< 配置数据

const uint16_t SOURCE_LOG_MANAGER = 200;
const uint16_t SOURCE_ERROR_MANAGER = 201;      ///< 错误管理器
const uint16_t SOURCE_CONFIG_MANAGER = 202;     ///< 配置管理器
const uint16_t SOURCE_COMMUNICATION = 203;      ///< 通信层
const uint16_t SOURCE_DATA_PROCESSOR = 204;     ///< 数据处理层

/**
 * @brief 数据包大小常量
 */
const size_t MAX_PAYLOAD_SIZE = 4096;
const size_t MAX_ERROR_DESC_SIZE = 256;

/**
 * @brief 通用数据包结构
 * @details 系统总线上流转的统一数据格式
 */
struct DataPacket {
    uint16_t sourceType;           ///< 数据源类型（可以是DeviceType或UIWidget）
    uint16_t dispatchTarget;       ///< 分发目标
    uint32_t packetSequence;       ///< 包序列号
    uint64_t timeStamp;           ///< 时间戳
    uint16_t errorType;           ///< 错误类型（ErrorType枚举）
    uint16_t errorLevel;          ///< 错误级别（ErrorLevel枚举）
    char errorDescription[MAX_ERROR_DESC_SIZE]; ///< 错误描述
    char sourceFile[64];          ///< 源文件名（用于错误追踪）
    uint32_t sourceLine;          ///< 源文件行号（用于错误追踪）
    
    /**
     * @brief 载荷联合体
     */
    union PayloadUnion {
        uint8_t rawData[MAX_PAYLOAD_SIZE];              ///< 原始字节数据
        GeneralPackage generalPackage;                  ///< CPU数据总包（向后兼容）
        
        PayloadUnion() { memset(this, 0, sizeof(PayloadUnion)); }
        ~PayloadUnion() {}
    } payload;
    
    uint32_t payloadSize;                               ///< 实际载荷大小
    uint32_t payloadType;                               ///< 载荷数据类型标识
    
    /**
     * @brief 构造函数
     */
    DataPacket() : sourceType(DEVICE_UNKNOWN), 
                   dispatchTarget(UI_UNKNOWN), 
                   packetSequence(0), 
                   timeStamp(0), 
                   errorType(ERR_NONE),
                   errorLevel(ERR_LEVEL_INFO),
                   sourceLine(0),
                   payloadSize(0),
                   payloadType(0) {
        memset(errorDescription, 0, sizeof(errorDescription));
        memset(sourceFile, 0, sizeof(sourceFile));
        memset(&payload, 0, sizeof(payload));
    }
};

/**
 * @brief 获取类型对应的载荷类型常量
 * @tparam T 结构体类型
 * @return 对应的载荷类型常量
 */
template<typename T>
inline uint32_t getPayloadTypeConstant() {
    return PAYLOAD_TYPE_UNKNOWN;
}

template<>
inline uint32_t getPayloadTypeConstant<workStandby>() {
    return PAYLOAD_TYPE_WORK_STANDBY;
}

template<>
inline uint32_t getPayloadTypeConstant<TargetType>() {
    return PAYLOAD_TYPE_TARGET_TYPE;
}

template<>
inline uint32_t getPayloadTypeConstant<GeneralPackage>() {
    return PAYLOAD_TYPE_GENERAL_PACKAGE;
}

/**
 * @brief 将任意结构体打包到DataPacket的rawData中
 * @tparam T 结构体类型（必须是trivially copyable）
 * @param packet 目标数据包
 * @param data 要打包的结构体数据
 * @return 是否成功打包
 */
template<typename T>
inline bool packDataToPayload(DataPacket& packet, const T& data) {
    static_assert(std::is_trivially_copyable<T>::value, "Type must be trivially copyable");
    
    if (sizeof(T) > MAX_PAYLOAD_SIZE) {
        return false;
    }
    
    memset(&packet.payload, 0, sizeof(packet.payload));
    packet.payloadSize = sizeof(T);
    packet.payloadType = getPayloadTypeConstant<T>();
    memcpy(packet.payload.rawData, &data, sizeof(T));
    
    return true;
}

/**
 * @brief 从DataPacket的rawData中解包任意结构体
 * @tparam T 结构体类型（必须是trivially copyable）
 * @param packet 源数据包
 * @param data 输出的结构体数据
 * @return 是否成功解包
 */
template<typename T>
inline bool unpackDataFromPayload(const DataPacket& packet, T& data) {
    static_assert(std::is_trivially_copyable<T>::value, "Type must be trivially copyable");
    
    if (packet.payloadSize != sizeof(T)) {
        return false;
    }
    
    memcpy(&data, packet.payload.rawData, sizeof(T));
    return true;
}

/**
 * @brief 检查DataPacket中是否包含指定类型的数据
 * @tparam T 结构体类型
 * @param packet 数据包
 * @return 是否包含指定类型的数据
 */
template<typename T>
inline bool containsDataType(const DataPacket& packet) {
    return (packet.payloadSize == sizeof(T)) && 
           (packet.payloadType == getPayloadTypeConstant<T>());
}

/**
 * @brief 序列化DataPacket到QByteArray
 * @param packet 要序列化的数据包
 * @return 二进制数据
 */
inline QByteArray serializeDataPacket(const DataPacket& packet) {
    QByteArray buffer;
    buffer.resize(static_cast<int>(sizeof(DataPacket)));
    memcpy(buffer.data(), &packet, sizeof(DataPacket));
    return buffer;
}

/**
 * @brief 从QByteArray反序列化DataPacket
 * @param data 二进制数据
 * @return 反序列化的数据包，如果数据不足则返回空包
 */
inline DataPacket deserializeDataPacket(const QByteArray& data) {
    DataPacket packet;
    if (data.size() >= static_cast<int>(sizeof(DataPacket))) {
        memcpy(&packet, data.constData(), sizeof(DataPacket));
    }
    return packet;
}

/**
 * @brief 创建错误/日志数据包的便捷函数
 * @param level 错误级别
 * @param errorType 错误类型
 * @param sourceModule 源模块类型
 * @param message 错误消息
 * @param file 源文件名（使用__FILE__）
 * @param line 源文件行号（使用__LINE__）
 * @return 填充好的DataPacket
 */
inline DataPacket createErrorPacket(ErrorLevel level, ErrorType errorType, 
                                    uint16_t sourceModule, const QString& message,
                                    const char* file = nullptr, int line = 0) {
    DataPacket packet;
    packet.sourceType = sourceModule;
    packet.dispatchTarget = UI_OUTPUTWIDGET;
    static uint32_t errorSequence = 0;
    packet.packetSequence = ++errorSequence;
    packet.timeStamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    packet.errorType = errorType;
    packet.errorLevel = level;
    
    // 复制错误描述
    QByteArray msgBytes = message.toUtf8();
    strncpy(packet.errorDescription, msgBytes.constData(), MAX_ERROR_DESC_SIZE - 1);
    packet.errorDescription[MAX_ERROR_DESC_SIZE - 1] = '\0';
    
    // 复制源文件信息
    if (file) {
        const char* fileName = strrchr(file, '\\');
        if (!fileName) fileName = strrchr(file, '/');
        const char* shortName = fileName ? (fileName + 1) : file;
        
        strncpy(packet.sourceFile, shortName, sizeof(packet.sourceFile) - 1);
        packet.sourceFile[sizeof(packet.sourceFile) - 1] = '\0';
    }
    packet.sourceLine = static_cast<uint32_t>(line);
    
    return packet;
}

/**
 * @brief 判断数据包是否为错误/日志包
 */
inline bool isErrorPacket(const DataPacket& packet) {
    return packet.errorType != ERR_NONE || packet.errorLevel >= ERR_LEVEL_WARNING;
}

#pragma pack(pop) // 恢复之前的对齐

#endif // GENERICPACKETS_H
