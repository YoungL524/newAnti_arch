#pragma once

#include "IWorkModeCommandSender.h"

/**
 * @file BusWorkModeSender.h
 * @brief 通过 SystemBus 发布 workStandby 的运行期 sender 实现
 * @details 将 mode 打包为 workStandby 结构体，并以 DataPacket 形式发布到
 *          SystemBus，由 BusToCommRouter 路由到对应端点。
 *
 *          本类不订阅 SystemBus，也不持有任何资源；可由 AppController
 *          作为成员或栈对象持有。
 */
class BusWorkModeSender : public IWorkModeCommandSender
{
public:
    BusWorkModeSender() = default;
    ~BusWorkModeSender() override = default;

    bool sendWorkMode(uint16_t deviceId, uint32_t mode) override;
};
