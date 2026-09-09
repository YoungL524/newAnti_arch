/**
 * @file DcmpProtocolTypes.h
 * @brief 定义统一线协议的公共头、包尾和消息类型，供协议 Adapter 构造及解释报文。
 * @details 依据 doc/protocol/1_第一版_统一报文封装-20260908.xlsx。阅读从 DcmpFrameHeader 开始：
 * 线上顺序为公共头（44 字节）、载荷（N 字节）、包尾（4 字节）。本文件不实现载荷、编解码、
 * 超时判断或业务路由；这些边界临时值对象不得发布到 CoreDataDomain。
 * 对象由调用方持有，无共享可变状态。自然对齐不等于线协议编码，禁止直接发送对象内存。
 */
#pragma once

#include <QtGlobal>
#include <type_traits>

namespace core::protocol {

/**
 * @brief 工作表指定的 32 位线协议消息类型，不作为进程内分发标识。
 * DCMP-00;DP-01;FPGA-02;SP-03;
 */
enum class DcmpMessageType : quint32
{
    DcmpToDpControl = 0x0001U, ///< DCMP 向 DP 发送控制指令。
    DpToFpControl = 0x0102U,   ///< DP 向 FPGA 转发控制指令。
    FpgaToSpData = 0x0203U,    ///< FPGA 向 SP 返回状态及 AD 数据。
    SpToDpPoint = 0x0301U,    ///< SP 向 DP 返回数据；不代表上位机存在点迹 Topic。
    DpToDcmpTrack = 0x0100U,  ///< DP 向 DCMP 返回状态及航迹。
    DpHeartbeat = 0x0101U,    ///< DP 向 DCMP 发送心跳。
    SpHeartbeat = 0x0303U     ///< SP 向 DCMP 发送心跳。
};

/**
 * @brief 所有消息共用的 44 字节公共头，包含起始标志及十个系统信息字。
 * @details F_UI_TotalLength 不计入自身，不含起始标志和包尾，因此值为 36+N；实际报文为 48+N。
 * N 必须是 4 的倍数，载荷补齐字节置零。默认对象尚未指定消息类型和有效时间，不代表可发送业务报文。
 * 日期、软件版本及编译时间编码未完全明确，暂保留原始字，不推测转换为 UTC。
 */
struct DcmpFrameHeader
{
    static constexpr quint32 wireSize = 44U; ///< 公共头线上字节数，包含起始标志。
    static constexpr quint32 payloadOffset = wireSize; ///< 载荷起始偏移，等于公共头线上字节数。
    static constexpr quint32 declaredFixedSize = 36U; ///< 长度字段计入的固定字节数。
    static constexpr quint32 lengthExcludedSize = 12U; ///< 总报文字节数中不计入 F_UI_TotalLength 的起始标志、长度字段和包尾字节数。
    static constexpr quint32 expectedMagic = 0xA1A11A1AU; ///< 工作表定义的起始标志。
    static constexpr quint32 expectedVersion = 20260908U; ///< 工作表定义的协议版本数值。

    static constexpr quint32 magicOffset = 0U; ///< 起始标志在线偏移。
    static constexpr quint32 totalLengthOffset = 4U; ///< 声明长度在线偏移。
    static constexpr quint32 messageTypeOffset = 8U; ///< 消息类型在线偏移。
    static constexpr quint32 versionOffset = 12U; ///< 协议版本在线偏移。
    static constexpr quint32 systemInfoOffset = 16U; ///< 七个系统信息字的起始偏移。
    static constexpr quint32 systemInfoWordCount = 7U; ///< 原样保留的系统信息字数量。

    quint32 F_UI_Package_Head = expectedMagic; ///< 偏移 0：起始标志。
    quint32 F_UI_TotalLength = declaredFixedSize; ///< 偏移 4：36+N 字节，不含F_UI_TotalLength、起始标志和包尾。
    DcmpMessageType F_UI_MessageType {}; ///< 偏移 8：消息类型；默认零未指定。
    quint32 F_UI_Protocol_Version = expectedVersion; ///< 偏移 12：协议版本，非软件版本。
    quint32 F_UI_GPS_Time_DMY = 0; ///< 偏移 16：外部 GPS 日期原始字，年月日编码待明确。
    quint32 F_UI_GPS_Time_SMH = 0; ///< 偏移 20：低 16 位秒（60/65536 s），随后 8 位分、8 位时。
    quint32 F_UI_SYS_Time_DMY = 0; ///< 偏移 24：电脑日期原始字，年月日编码待明确。
    quint32 F_UI_SYS_Time_SMH = 0; ///< 偏移 28：北京时间原始字；不是 UTC 或单调时钟。
    quint32 F_UI_DCMP_Version = 0; ///< 偏移 32：显控软件版本原始字，编码待明确。
    quint32 F_UI_DCMP_Time_DMY = 0; ///< 偏移 36：编译日期原始字；表格名称与时分秒说明有冲突，暂不转换。
    quint32 F_UI_DCMP_Time_SMH = 0; ///< 偏移 40：编译时间原始字，表格尚未明确编码。
};

/**
 * @brief 紧随可变长载荷之后的公共包尾。
 * @details 线上偏移为 44+N，不能把它直接紧贴公共头存放后当作有载荷整包发送。
 */
struct DcmpFrameTail
{
    static constexpr quint32 wireSize = 4U; ///< 包尾线上字节数，不计入 F_UI_TotalLength。
    static constexpr quint32 expectedMagic = 0xB1B2A1A2U; ///< 工作表指定的结束标志。
    quint32 packetTail = expectedMagic; ///< 偏移 44+N：结束标志，无备份字。
};

static_assert(sizeof(DcmpFrameHeader) == 44U, "公共头必须保持 44 字节");
static_assert(alignof(DcmpFrameHeader) == alignof(quint32), "公共头必须自然对齐");
static_assert(sizeof(DcmpFrameTail) == 4U, "包尾必须保持 4 字节");
static_assert(std::is_standard_layout<DcmpFrameHeader>::value
              && std::is_trivially_copyable<DcmpFrameHeader>::value,
              "公共头必须是无动态资源的标准布局值对象");

} // namespace core::protocol
