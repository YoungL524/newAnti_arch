/**
 * @file DcmpProtocolAdapter.h
 * @brief 将完整UDP报文校验并解码为DP/SP心跳或DP航迹载荷，供对应owner同步处理。
 * @details UdpEndpoint的唯一接收回调调用onDatagram；组合根闭包负责端点及来源绑定。
 * 输出为短期DcmpFrameHeader及强类型协议引用，不发布协议DTO到Topic，不持有socket或计时状态。
 * 新人从onDatagram及Result阅读拒绝规则；所有回调与调用方同线程，生命周期覆盖本Adapter。
 */
#pragma once
#include "core/protocol/DcmpPayloads.h"
#include <functional>

namespace core::protocol {
/** @brief 无消息缓存的内部UDP协议边界；一个实例由所属接收线程调用，不进行业务路由或超时判断。 */
class DcmpProtocolAdapter final
{
public:
    /** @brief 本次解析结果；只有Accepted表示已把有效载荷交给对应处理函数。 */
    enum class Result
    {
        Accepted, ///< 合法心跳已经同步交付。
        InvalidLength, ///< 空指针、截断、超长、非四字节载荷或声明长度不符。
        InvalidMagic, ///< 起始标志错误。
        UnsupportedVersion, ///< 协议版本不受支持。
        InvalidTail, ///< 包尾错误。
        UnsupportedMessage, ///< 完整封装有效，但当前未实现该消息类型。
        InvalidHeartbeat, ///< 在线值或阵面位图超出协议范围。
        InvalidTrack, ///< 阵面、消亡标志或显示必需的航迹字段违反协议合同。
        NoHandler ///< 对应类型处理函数未配置，未发生业务交付。
    };
    /** @brief DP强类型同步入口；公共头及载荷引用仅在回调内有效，末两参数为接收UTC毫秒及steady纳秒。 */
    using DpHandler = std::function<void(const DcmpFrameHeader &, const DpHeartbeatPayload &, qint64, qint64)>;
    /** @brief SP强类型同步入口；引用不得保留或queued转发，回调须有界且不得抛异常。 */
    using SpHandler = std::function<void(const DcmpFrameHeader &, const SpHeartbeatPayload &, qint64, qint64)>;
    /** @brief DP航迹同步入口；阵面索引为0～3，源序号来自FPGA包计数，协议对象只在回调期有效。 */
    using TrackHandler = std::function<void(const DcmpFrameHeader &, quint8, quint32,
        const DpToDcmpTrackPayload::DpTrack &, qint64, qint64)>;
    /** @brief 保存三个明确类型的处理入口；允许缺省某一类型，接收该类型时返回NoHandler。 */
    DcmpProtocolAdapter(DpHandler dpHandler, SpHandler spHandler, TrackHandler trackHandler = {});
    /**
     * @brief 校验完整封装并同步解码已支持载荷，失败不触碰业务owner。
     * @param data 调用期有效的原始字节，不要求四字节地址对齐。
     * @param size 本次数据报实际长度，范围48～65507；各消息类型必须匹配自己的固定长度。
     * @param receiveUtcMs 端点取得报文时的UTC毫秒，原样交给owner。
     * @param receiveSteadyNs 端点取得报文时的单调纳秒，不能以报文系统时间代替。
     * @return 分类结果；未知类型不猜测载荷，也不触发任何业务状态变化。
     */
    Result onDatagram(const char *data, qint64 size, qint64 receiveUtcMs, qint64 receiveSteadyNs) const;
private:
    DpHandler m_dpHandler; ///< 唯一DP心跳入口，无订阅列表或消息缓存。
    SpHandler m_spHandler; ///< 唯一SP心跳入口，不决定执行线程。
    TrackHandler m_trackHandler; ///< 唯一DP航迹协议入口，不负责运行时身份绑定或Topic发布。
};
} // namespace core::protocol
