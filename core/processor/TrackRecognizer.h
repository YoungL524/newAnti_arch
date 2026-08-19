/**
 * @file TrackRecognizer.h
 * @brief 定义标准航迹发布前必须经过的同步识别扩展点。
 * @details 当前实现是基座占位识别器：确认 Pending 航迹已走过识别阶段，但不猜测目标类型，因此
 *          recognitionType 保持 Unknown。以后接入真实算法时保持本接口和 TrackPacket 状态机不变，
 *          避免显示、存储和插件消费者依赖某个具体算法实现。
 */

#pragma once

/**
 * @brief 最小同步识别器，负责完成识别状态机而不修改航迹业务字段。
 *
 * 该模块仅在 `recognitionStatus` 为 `Pending` 时将状态置为 `Completed`，
 * 并填充 `recognizedSteadyNs` 为单调时钟时间。其它字段保持不变。
 * 识别类型保持 `Unknown`，符合任务 "最小同步识别模块" 的要求。
 */

#include "TrackTypes.h"

class TrackRecognizer
{
public:
    /**
     * @brief 对给定的 TrackPacket 执行同步识别。
     *
     * 只有当 packet.recognitionStatus == RecognitionStatus::Pending 时才会进行处理。
     * 处理成功后会:
     *   - 将 recognitionStatus 设置为 Completed
     *   - 保持 recognitionType 为 Unknown（保持不变）
     *   - 使用 monotonicNowNs() 填充 recognizedSteadyNs，满足 >0 且不小于 normalizedSteadyNs（若已设置）
     *
     * @param[in,out] packet 待识别航迹；仅允许修改识别状态、类型和识别完成时间。
     * @return true 识别成功并修改了 packet；false 未进行任何修改（状态非 Pending）。
     */
    bool recognize(TrackPacket& packet) const;
};
