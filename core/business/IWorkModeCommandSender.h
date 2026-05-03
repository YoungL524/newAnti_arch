#pragma once

#include <cstdint>

/**
 * @file IWorkModeCommandSender.h
 * @brief 雷达工作模式命令发送器抽象接口
 * @details RadarWorkModeController 通过本接口下发命令，
 *          运行期实现为 BusWorkModeSender（发布到 SystemBus），
 *          测试期实现为 FakeWorkModeCommandSender（直接记录调用）。
 *
 *          通过抽象解耦后，RadarWorkModeController 不依赖 SystemBus、
 *          CommunicationManager、Qt 网络等组件，可在任意线程独立测试。
 */
class IWorkModeCommandSender
{
public:
    virtual ~IWorkModeCommandSender() = default;

    /**
     * @brief 向指定雷达设备下发工作模式命令
     * @param deviceId 目标雷达设备类型（DeviceType 常量，例如 DEVICE_RADAR_NH_KU_1）
     * @param mode 1=工作, 0=待机
     * @return 是否成功提交（true 仅表示命令已进入下行链路；不保证设备已确认）
     */
    virtual bool sendWorkMode(uint16_t deviceId, uint32_t mode) = 0;
};
