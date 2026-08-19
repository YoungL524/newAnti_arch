/**
 * @file GenericPackets.h
 * @brief 定义 SystemBus 上唯一的顶层消息 DataPacket 及其类型安全载荷工具。
 * @details 厂家协议对象进入核心后必须转换为这里定义的统一消息；显示、存储、Web 和反向控制均只交换
 *          DataPacket。command 表示业务语义，DataPayload 的 variant 表示真实载荷类型，二者必须一致。
 *          小型协议结构按值保存，大型业务对象使用 shared_ptr<const T> 在多个订阅分支间只读共享。
 *
 * @par 扩展新命令的步骤
 * 在 DataCommand、DataPayload、getPayloadTypeConstant<T>() 和 DataPacket::hasConsistentPayload() 四处同时登记，
 * 并增加协议布局/总线往返测试；遗漏任一处都会导致 pack 或 publish 明确失败，而不是退化为 void 指针。
 */

#ifndef GENERICPACKETS_H
#define GENERICPACKETS_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <variant>
#include <QByteArray>
#include <QString>
#include <QDateTime>
#include <QMetaType>
#include "StructureReceive.h"

struct RadarRuntimeSettings;
struct TrackPacket;
struct TrackDisplayUpdate;
struct TrackInteractionCommand;

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
 * @brief 判断设备类型是否为雷达
 * @details 枚举范围的集中判断点。新增雷达设备时需同步更新此函数。
 *          被 BusToCommRouter 用于过滤非反向流数据（雷达→上位机的上报数据）。
 */
inline bool isRadarDevice(uint16_t sourceType) {
    return sourceType >= DEVICE_RADAR_NH_KU_1 && sourceType <= DEVICE_RADAR_NH_S_2;
}

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
    UI_TABLEWIDGET = 15,
    DISPATCH_TRACK_DISPLAY_MANAGER = 16 ///< 航迹显示管理（内部服务目标）
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
 * @note 编号区间：0-99 协议层，100-199 系统/业务 payload
 */
const uint32_t PAYLOAD_TYPE_UNKNOWN = 0;
const uint32_t PAYLOAD_TYPE_WORK_STANDBY = 1;
const uint32_t PAYLOAD_TYPE_TARGET_TYPE = 2;
const uint32_t PAYLOAD_TYPE_LOG_TEXT = 100;
const uint32_t PAYLOAD_TYPE_ERROR = 101;        ///< 错误/告警数据
const uint32_t PAYLOAD_TYPE_CONFIG = 102;       ///< 配置数据
const uint32_t PAYLOAD_TYPE_DPH_PARAMS_CHANGED = 103; ///< 运行时参数变更
const uint32_t PAYLOAD_TYPE_TRACK_DISPLAY_UPDATE = 104; ///< 航迹显示初始快照/增量（shared payload）
const uint32_t PAYLOAD_TYPE_TRACK_INTERACTION_COMMAND = 105; ///< 航迹交互命令（shared payload）
const uint32_t PAYLOAD_TYPE_TRACK_PACKET = 106; ///< 标准权威航迹（shared payload）

/// 命令字沿用旧 payloadType 数值，避免配置和路由同时重写。
enum class DataCommand : uint32_t {
    Unknown = PAYLOAD_TYPE_UNKNOWN,
    WorkStandby = PAYLOAD_TYPE_WORK_STANDBY,
    Target = PAYLOAD_TYPE_TARGET_TYPE,
    Log = PAYLOAD_TYPE_LOG_TEXT,
    Error = PAYLOAD_TYPE_ERROR,
    Config = PAYLOAD_TYPE_CONFIG,
    DphParamsChanged = PAYLOAD_TYPE_DPH_PARAMS_CHANGED,
    TrackDisplayUpdate = PAYLOAD_TYPE_TRACK_DISPLAY_UPDATE,
    TrackInteractionCommand = PAYLOAD_TYPE_TRACK_INTERACTION_COMMAND,
    Track = PAYLOAD_TYPE_TRACK_PACKET,
};

/// @brief 设备 / 服务源类型
/// @note 编号区间：10-49 雷达设备，50-199 保留，200-299 内部服务，300+ runtime/control target
const uint16_t SOURCE_LOG_MANAGER = 200;
const uint16_t SOURCE_ERROR_MANAGER = 201;      ///< 错误管理器
const uint16_t SOURCE_CONFIG_MANAGER = 202;     ///< 配置管理器
const uint16_t SOURCE_COMMUNICATION = 203;      ///< 通信层
const uint16_t SOURCE_DATA_PROCESSOR = 204;     ///< 数据处理层
const uint16_t SOURCE_RUNTIME_SETTINGS = 205;   ///< 运行时参数服务
const uint16_t SOURCE_TRACK_DISPLAY = 206;      ///< 航迹显示管理
const uint16_t SOURCE_WEB_API = 207;            ///< Web API 接入层发布操作员命令时使用的源标识。

const size_t MAX_ERROR_DESC_SIZE = 256;

/** @brief 可经总线传播并最终写日志或推送 Web 的结构化错误。 */
struct ErrorPayload {
    ErrorType type = ERR_NONE;            ///< 便于接收方分类处理的错误类别。
    ErrorLevel level = ERR_LEVEL_INFO;    ///< 对应日志严重级别和告警展示策略。
    QString description;                  ///< 面向运维的错误文本，创建时限制为 MAX_ERROR_DESC_SIZE-1 个字符。
    QString sourceFile;                   ///< 可选的短文件名，不包含本机绝对路径。
    uint32_t sourceLine = 0;              ///< sourceFile 中的源码行号；未知时为 0。
};

using DataPayload = std::variant<
    std::monostate,
    workStandby,
    TargetType,
    QByteArray,
    ErrorPayload,
    std::shared_ptr<const RadarRuntimeSettings>,
    std::shared_ptr<const TrackPacket>,
    std::shared_ptr<const TrackDisplayUpdate>,
    std::shared_ptr<const TrackInteractionCommand>>;

/**
 * @brief SystemBus 唯一顶层消息
 * @details command 决定消息语义，std::variant 保证载荷类型安全。
 *          不再内嵌固定 rawData，也不再需要 void 对象载荷。
 */
struct DataPacket {
    static constexpr uint32_t Magic = 0x5A5A6B6B;
    static constexpr uint16_t SchemaVersion = 1;

    uint16_t sourceType = DEVICE_UNKNOWN; ///< 设备或内部服务来源，用于源类型订阅和链路归属。
    uint16_t dispatchTarget = UI_UNKNOWN; ///< 逻辑分发目标，用于目标订阅；不是网络地址。
    uint32_t packetSequence = 0;          ///< 来源范围内递增的包序号；允许回绕，不作为全局主键。
    uint64_t timeStamp = 0;               ///< 业务产生或接收时间，Unix 时间戳，单位毫秒。
    uint64_t traceId = 0;                 ///< 跨处理阶段保持不变的链路跟踪 ID；0 表示尚未分配。
    DataCommand command = DataCommand::Unknown; ///< 决定消息语义及 payload 必须持有的 variant 分支。
    DataPayload payload;                  ///< 类型安全载荷；共享指针分支必须非空且指向不可变对象。

    /** @brief 返回用于日志、配置和兼容旧 payloadType 编号的命令整数值。 */
    uint32_t commandValue() const noexcept {
        return static_cast<uint32_t>(command);
    }

    /**
     * @brief 校验 command 与 variant 当前分支是否构成已登记的合法组合。
     * @return 值载荷类型匹配，或共享载荷类型匹配且指针非空时返回 true。
     * @note SystemBus 在入队前强制调用本函数，防止错误类型传播到异步订阅者。
     */
    bool hasConsistentPayload() const noexcept {
        switch (command) {
        case DataCommand::Unknown:
            return std::holds_alternative<std::monostate>(payload);
        case DataCommand::WorkStandby:
            return std::holds_alternative<workStandby>(payload);
        case DataCommand::Target:
            return std::holds_alternative<TargetType>(payload);
        case DataCommand::Log:
        case DataCommand::Config:
            return std::holds_alternative<QByteArray>(payload);
        case DataCommand::Error:
            return std::holds_alternative<ErrorPayload>(payload);
        case DataCommand::DphParamsChanged:
            return std::holds_alternative<std::shared_ptr<const RadarRuntimeSettings>>(payload)
                && static_cast<bool>(std::get<std::shared_ptr<const RadarRuntimeSettings>>(payload));
        case DataCommand::TrackDisplayUpdate:
            return std::holds_alternative<std::shared_ptr<const TrackDisplayUpdate>>(payload)
                && static_cast<bool>(std::get<std::shared_ptr<const TrackDisplayUpdate>>(payload));
        case DataCommand::TrackInteractionCommand:
            return std::holds_alternative<std::shared_ptr<const TrackInteractionCommand>>(payload)
                && static_cast<bool>(std::get<std::shared_ptr<const TrackInteractionCommand>>(payload));
        case DataCommand::Track:
            return std::holds_alternative<std::shared_ptr<const TrackPacket>>(payload)
                && static_cast<bool>(std::get<std::shared_ptr<const TrackPacket>>(payload));
        }
        return false;
    }

    /** @brief 只读取得大型共享载荷；类型不匹配时返回空 shared_ptr，不抛出异常。 */
    template<typename T>
    std::shared_ptr<const T> sharedPayloadAs() const {
        const auto value = std::get_if<std::shared_ptr<const T>>(&payload);
        return value ? *value : std::shared_ptr<const T>{};
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

/**
 * @brief 将已登记类型写入载荷，并同步设置与其匹配的命令字。
 * @details 协议小对象按值复制；业务大对象复制到 shared_ptr<const T>，供总线多个分支共享。
 * @return 类型已通过 getPayloadTypeConstant<T>() 登记时返回 true；未知类型不修改为有效命令并返回 false。
 */
template<typename T>
inline bool packDataToPayload(DataPacket& packet, const T& data) {
    const uint32_t command = getPayloadTypeConstant<T>();
    if (command == PAYLOAD_TYPE_UNKNOWN)
        return false;

    packet.command = static_cast<DataCommand>(command);
    if constexpr (std::is_same<T, workStandby>::value
                  || std::is_same<T, TargetType>::value
                  || std::is_same<T, QByteArray>::value
                  || std::is_same<T, ErrorPayload>::value) {
        packet.payload = data;
    } else {
        packet.payload = std::make_shared<const T>(data);
    }
    return true;
}


/**
 * @brief 同时校验命令字和 variant 分支后，将载荷复制到调用方对象。
 * @return 两层类型检查均通过时返回 true；共享指针为空或类型不匹配时保持失败。
 */
template<typename T>
inline bool unpackDataFromPayload(const DataPacket& packet, T& data) {
    if (packet.commandValue() != getPayloadTypeConstant<T>())
        return false;

    if constexpr (std::is_same<T, workStandby>::value
                  || std::is_same<T, TargetType>::value
                  || std::is_same<T, QByteArray>::value
                  || std::is_same<T, ErrorPayload>::value) {
        const auto value = std::get_if<T>(&packet.payload);
        if (!value)
            return false;
        data = *value;
    } else {
        const auto value = std::get_if<std::shared_ptr<const T>>(&packet.payload);
        if (!value || !*value)
            return false;
        data = **value;
    }
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
    if (packet.commandValue() != getPayloadTypeConstant<T>())
        return false;
    if constexpr (std::is_same<T, workStandby>::value
                  || std::is_same<T, TargetType>::value
                  || std::is_same<T, QByteArray>::value
                  || std::is_same<T, ErrorPayload>::value) {
        return std::holds_alternative<T>(packet.payload);
    } else {
        const auto value = std::get_if<std::shared_ptr<const T>>(&packet.payload);
        return value && static_cast<bool>(*value);
    }
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
    static std::atomic<uint32_t> errorSequence{0};
    packet.packetSequence = ++errorSequence;
    packet.timeStamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    packet.command = DataCommand::Error;

    ErrorPayload error;
    error.type = errorType;
    error.level = level;
    error.description = message.left(static_cast<int>(MAX_ERROR_DESC_SIZE - 1));
    if (file) {
        const char* fileName = strrchr(file, '\\');
        if (!fileName) fileName = strrchr(file, '/');
        const char* shortName = fileName ? (fileName + 1) : file;
        error.sourceFile = QString::fromUtf8(shortName);
    }
    error.sourceLine = static_cast<uint32_t>(line);
    packet.payload = std::move(error);
    return packet;
}

/**
 * @brief 判断数据包是否为错误/日志包
 */
inline bool isErrorPacket(const DataPacket& packet) {
    if (packet.command != DataCommand::Error)
        return false;
    const auto error = std::get_if<ErrorPayload>(&packet.payload);
    return error && (error->type != ERR_NONE || error->level >= ERR_LEVEL_WARNING);
}

Q_DECLARE_METATYPE(DataPacket)

#endif // GENERICPACKETS_H
