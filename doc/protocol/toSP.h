/**
 * @file toSP.h
 * @brief 交付 SP 的独立纯 C11 协议头：公共封装及 0x0303 心跳载荷。
 * @details 从 DcmpFrameHeader 阅读；线上为44字节头、84字节载荷、4字节尾，共132字节。
 * F_UI_TotalLength 排除自身及起止标志，心跳必须填120。消息类型字段必须填 DCMP_SP_HEARTBEAT。
 * 调用方先以 {0} 初始化各对象，再填写头标志、长度、类型、版本、时间及包尾；备份字保持零。
 * 本文件无 Qt/C++ 或其他工程依赖；无通信、线程和编解码。必须按约定线字节序逐字段编码，
 * 不应直接发送对象内存。日期编码尚未明确，保留原始字；在线超时由接收端单调时钟判断。
 */
#ifndef DCMP_DELIVERY_TO_SP_H
#define DCMP_DELIVERY_TO_SP_H
#include <stdint.h>
#include <stddef.h>

/** @brief 公共起始标志。 */
#define DCMP_HEADER_MAGIC UINT32_C(0xA1A11A1A)
/** @brief 公共结束标志。 */
#define DCMP_TAIL_MAGIC UINT32_C(0xB1B2A1A2)
/** @brief 当前已确认的协议版本数值。 */
#define DCMP_PROTOCOL_VERSION UINT32_C(20260908)
/** @brief SP 发给 DCMP 的心跳消息类型，字段宽度为32位。 */
#define DCMP_SP_HEARTBEAT UINT32_C(0x0303)
/** @brief 公共头线上字节数。 */
#define DCMP_HEADER_WIRE_SIZE UINT32_C(44)
/** @brief 公共包尾线上字节数。 */
#define DCMP_TAIL_WIRE_SIZE UINT32_C(4)
/** @brief 声明长度中的固定部分，不含长度字段自身和起止标志。 */
#define DCMP_DECLARED_FIXED_SIZE UINT32_C(36)
/** @brief SP 心跳载荷长度。 */
#define DCMP_SP_HEARTBEAT_PAYLOAD_SIZE UINT32_C(84)

/** @brief 公共头；C 不支持字段默认值，必须由调用方初始化并填写公共常量。 */
typedef struct DcmpFrameHeader
{
    uint32_t F_UI_Package_Head; ///< 偏移 0：起始标志。
    uint32_t F_UI_TotalLength; ///< 偏移 4：36+N 字节，不含F_UI_TotalLength、起始标志和包尾。
    uint32_t F_UI_MessageType; ///< 偏移 8：消息类型；默认零未指定。
    uint32_t F_UI_Protocol_Version; ///< 偏移 12：协议版本，非软件版本。
    uint32_t F_UI_GPS_Time_DMY; ///< 偏移 16：外部 GPS 日期原始字，年月日编码待明确。
    uint32_t F_UI_GPS_Time_SMH; ///< 偏移 20：低 16 位秒（60/65536 s），随后 8 位分、8 位时。
    uint32_t F_UI_SYS_Time_DMY; ///< 偏移 24：电脑日期原始字，年月日编码待明确。
    uint32_t F_UI_SYS_Time_SMH; ///< 偏移 28：北京时间原始字；不是 UTC 或单调时钟。
    uint32_t F_UI_DCMP_Version; ///< 偏移 32：显控软件版本原始字，编码待明确。
    uint32_t F_UI_DCMP_Time_DMY; ///< 偏移 36：编译日期原始字；表格名称与时分秒说明有冲突，暂不转换。
    uint32_t F_UI_DCMP_Time_SMH; ///< 偏移 40：编译时间原始字，表格尚未明确编码。
} DcmpFrameHeader;

/** @brief 公共包尾；放在完整载荷之后，packetTail 必须填写 DCMP_TAIL_MAGIC。 */
typedef struct DcmpFrameTail
{
    uint32_t packetTail; ///< 线上偏移44+N，四字节结束标志。
} DcmpFrameTail;

/** @brief 0x0303 的84字节心跳载荷；备份区不得混入未约定的业务字段。 */
typedef struct SpHeartbeatPayload
{
    uint16_t spOnline; ///< 偏移0：1在线，0不在线，其他值非法。
    uint16_t fpDataMask; ///< 偏移2：bit0～3代表阵面1～4有FP数据；bit4～15必须为零。
    uint32_t bak[20]; ///< 偏移4：80字节备份区，发送前置零。
} SpHeartbeatPayload;

_Static_assert(sizeof(DcmpFrameHeader) == 44, "公共头必须为44字节");
_Static_assert(_Alignof(DcmpFrameHeader) == 4, "公共头必须自然四字节对齐");
_Static_assert(sizeof(DcmpFrameTail) == 4, "包尾必须为4字节");
_Static_assert(sizeof(SpHeartbeatPayload) == 84, "心跳载荷必须为84字节");
_Static_assert(offsetof(SpHeartbeatPayload, fpDataMask) == 2, "旗标偏移必须为2");
_Static_assert(offsetof(SpHeartbeatPayload, bak) == 4, "备份区偏移必须为4");
#endif
