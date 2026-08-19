#include "Endpoint.h"
/**
 * @brief 保存端点配置快照，并把具体端点纳入调用方指定的 QObject 所有权树。
 * @details 构造阶段不创建 socket，因此配置加载和对象装配不会提前占用端口。
 */
Endpoint::Endpoint(const EndpointConfig& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
{
}
