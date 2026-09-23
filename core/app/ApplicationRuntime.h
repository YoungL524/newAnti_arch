/**
 * @file ApplicationRuntime.h
 * @brief 统一拥有存储、心跳及航迹Web出口依赖，执行启动回滚和逆序关闭。
 * @details 从start阅读；上游提供数据库和部署端口，下游为单写存储线程、Endpoint、Adapter、状态owner、
 * 两个WebReader及唯一Gateway。本类只装配生命周期，不解析协议或执行Command，所有方法在Qt主线程调用。
 */
#pragma once
#include "core/datadomain/CoreDataDomain.h"
#include "core/app/ApplicationController.h"
#include "core/comm/UdpEndpoint.h"
#include "core/protocol/DcmpProtocolAdapter.h"
#include "core/control/DeviceController.h"
#include "core/producer/TrackIngressProducer.h"
#include "core/web/StateWebSocketService.h"
#include "core/web/EventWebSocketService.h"
#include "core/web/TrackWebSocketService.h"
#include "core/web/WebSocketGateway.h"
#include "core/web/DeviceRegistryHttpService.h"
#include "core/model/StableIdRegistry.h"
#include <QThread>
namespace core::app {
class StorageRuntime;
/** @brief 具体组合根，成员按依赖顺序构造，借用者先析构。 */
class ApplicationRuntime final {
public:
    /**
     * @brief 建立内存依赖但不打开数据库或绑定网络。
     * @param databasePath 正式 SQLite 配置真源及归档数据库文件路径。
     * @param applicationVersion 写入 application_runs 的非空应用版本。
     */
    ApplicationRuntime(QString databasePath, QString applicationVersion);
    /** @brief 停止接收及期限检查，关闭Web后释放依赖。 */
    ~ApplicationRuntime();
    /** @brief 启动回环UDP与V1 WebSocket端口；零允许测试分配，失败回滚已启动资源。 */
    bool start(quint16 internalPort = 7788, quint16 webSocketPort = 8089,
               quint16 deviceHttpPort = 8088);
    /** @brief 幂等停止全部运行资源，必须在主线程调用。 */
    void stop();
    /** @brief 返回最近启动错误。 */
    QString lastError() const { return m_error; }
    /** @brief 返回UDP实际端口，停止后为零。 */
    quint16 internalPort() const { return m_internal.localPort(); }
private:
    /** @brief 创建唯一存储线程并同步完成数据库、设备和任务身份加载。 */
    bool startStorage();
    /** @brief 在存储线程正常关闭 SQLite 后停止线程并清空本次运行稳定 ID。 */
    void stopStorage();

    core::domain::CoreDataDomain m_domain; ///< 唯一强类型数据域。
    core::model::StableIdRegistry m_stableIds; ///< SQLite Owner 写入、WebReader读取的稳定身份注册表。
    QString m_databasePath; ///< 正式 SQLite 文件路径，由 main 或测试显式提供。
    QString m_applicationVersion; ///< application_runs 记录的应用版本。
    QThread m_storageThread; ///< 最多一个 SQLite 单写线程；不承载实时 Topic 数据。
    StorageRuntime *m_storageRuntime = nullptr; ///< 仅由存储线程执行并随线程结束 deleteLater 的组合根。
    core::execution::ExecutionLane m_lane; ///< 共享主线程调度。
    core::control::DeviceController m_deviceController; ///< 唯一设备状态与后续命令owner。
    core::producer::TrackIngressProducer m_trackIngress; ///< 0x0100到TrackTopic的唯一Producer；未绑定会话时拒绝发布。
    core::app::ApplicationController m_applicationController; ///< 唯一采集业务编排入口；绑定本地会话但不发送设备命令。
    core::protocol::DcmpProtocolAdapter m_adapter; ///< 同步协议入口。
    core::comm::UdpEndpoint m_internal; ///< 唯一内部UDP端点。
    core::web::WebSocketGateway m_gateway; ///< /ws/v1唯一网络连接owner。
    core::web::DeviceRegistryHttpService m_deviceHttp; ///< 设备与任务登记的低频HTTP边界，不进入数据域。
    core::web::StateWebSocketService m_stateWeb; ///< DeviceState只读Reader。
    core::web::TrackWebSocketService m_trackWeb; ///< TrackTopic独立cursor只读Reader。
    core::web::EventWebSocketService m_eventWeb; ///< EventTopic独立cursor只读Reader；与归档共享事实，不复制历史。
    QString m_error; ///< 最近启动失败原因。
    bool m_running = false; ///< 重复启动保护。
};
}
