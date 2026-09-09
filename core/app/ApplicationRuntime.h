/**
 * @file ApplicationRuntime.h
 * @brief 统一拥有心跳链依赖，执行启动回滚和逆序关闭。
 * @details 从start阅读；输入部署端口，下游为Endpoint、Adapter、状态owner及WebReader。
 * 本类只装配生命周期，不解析协议或执行Command，所有方法在Qt主线程调用。
 */
#pragma once
#include "core/datadomain/CoreDataDomain.h"
#include "core/comm/UdpEndpoint.h"
#include "core/protocol/DcmpProtocolAdapter.h"
#include "core/producer/HeartbeatStateOwner.h"
#include "core/web/StateWebSocketService.h"
namespace core::app {
/** @brief 具体组合根，成员按依赖顺序构造，借用者先析构。 */
class ApplicationRuntime final {
public:
    /** @brief 建立依赖但不绑定网络，默认容量4096航迹和1024事件。 */
    ApplicationRuntime();
    /** @brief 停止接收及期限检查，关闭Web后释放依赖。 */
    ~ApplicationRuntime();
    /** @brief 启动回环UDP/Web端口；零允许测试分配，失败回滚已启动资源。 */
    bool start(quint16 internalPort = 7788, quint16 statePort = 8090);
    /** @brief 幂等停止全部运行资源，必须在主线程调用。 */
    void stop();
    /** @brief 返回最近启动错误。 */
    QString lastError() const { return m_error; }
    /** @brief 返回UDP实际端口，停止后为零。 */
    quint16 internalPort() const { return m_internal.localPort(); }
private:
    core::domain::CoreDataDomain m_domain; ///< 唯一强类型数据域。
    core::execution::ExecutionLane m_lane; ///< 共享主线程调度。
    core::producer::HeartbeatStateOwner m_heartbeat; ///< 唯一设备状态owner。
    core::protocol::DcmpProtocolAdapter m_adapter; ///< 同步协议入口。
    core::comm::UdpEndpoint m_internal; ///< 唯一内部UDP端点。
    core::web::StateWebSocketService m_web; ///< 只读状态Reader。
    QString m_error; ///< 最近启动失败原因。
    bool m_running = false; ///< 重复启动保护。
};
}
