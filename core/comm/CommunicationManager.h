/**
 * @file CommunicationManager.h
 * @brief 管理全部通信端点的所有权、批量生命周期、热重载和接收汇聚。
 * @details 上游 AppController 用 EndpointConfig 创建端点；下游 RadarDataProcessor 只订阅本类统一的
 *          dataReceived 信号。管理器不解析协议，也不决定业务路由。端点对象以本类为 QObject parent，
 *          所有增删、打开、关闭和发送操作应在本类线程执行。
 */

#pragma once
#include "CommTypes.h"
#include "Endpoint.h"
#include <QHash>
#include <QObject>
#include <QMap>

/**
 * @brief 端点集合的唯一拥有者及通信层门面。
 */
class CommunicationManager : public QObject
{
    Q_OBJECT

public:
    /** @brief 创建空端点集合；网络资源直到 startAll() 或热重载打开阶段才分配。 */
    explicit CommunicationManager(QObject* parent = nullptr);
    /** @brief 关闭全部端点并释放由本对象拥有的 Endpoint。 */
    ~CommunicationManager() override;

    /**
     * @brief 按协议创建一个尚未打开的端点并接入统一信号。
     * @return ID 非空且不重复、协议受支持并成功创建时返回 true。
     */
    bool addEndpoint(const EndpointConfig& config);
    /** @brief 关闭并删除指定端点；ID 不存在时不产生副作用。 */
    void removeEndpoint(const QString& id);
    /**
     * @brief 用完整配置集合替换现有端点，供 endpoints.json 热重载使用。
     * @param[in] configs 新的完整端点配置，而不是增量差异。
     * @param[in] openAfter 为 true 时创建完成后立即打开全部端点。
     * @return 所有配置均成功创建，且要求打开时所有端点均打开成功才返回 true。
     * @warning 本函数本身不负责恢复旧配置；事务回滚由 AppController 保存旧快照后执行。
     */
    bool reloadEndpoints(const QVector<EndpointConfig>& configs, bool openAfter);
    /** @brief 返回端点的非拥有指针；删除或热重载后立即失效，调用方不得缓存。 */
    Endpoint* endpoint(const QString& id) const;

    /**
     * @brief 把一个已编码的完整报文发送到端点下的具名目标。
     * @return 端点存在、已打开、目标存在且底层发送完整成功时返回 true。
     */
    bool sendTo(const QString& endpointId, const QString& targetKey, const QByteArray& data);

    /** @brief 依次打开全部端点；任一失败仍继续尝试其余端点，最终返回是否全部成功。 */
    bool startAll();
    /** @brief 关闭全部端点但保留配置和对象，以便后续 startAll() 恢复采集。 */
    void stopAll();

    /** @brief 返回当前配置中的端点总数，不代表已打开数量。 */
    int endpointCount() const { return m_endpoints.size(); }
    /** @brief 逐个查询 isOpen() 并返回当前已打开端点数。 */
    int openEndpointCount() const;
    /** @brief 仅当 ID 存在且对应 Endpoint::isOpen() 为 true 时返回 true。 */
    bool isEndpointOpen(const QString& endpointId) const;
    /** @brief 生成端点 ID 到即时打开状态的诊断快照。 */
    QHash<QString, bool> endpointOpenStates() const;

signals:
    /**
     * @brief 接收到来自端点的数据（转发自 Endpoint::dataReceived）
     * @param endpointId 来源端点的 id
     * @param data       原始字节
     * @param receivedMs 数据到达时间戳（来自 Endpoint 的接收时刻）
     * @param receivedSteadyNs 单调时钟接收时间，仅用于进程内耗时
     */
    void dataReceived(const QString& endpointId, const QByteArray& data,
                      qint64 receivedMs, qint64 receivedSteadyNs);
    /** @brief 转发端点错误并附加 endpointId，使上层能够关联设备和任务。 */
    void errorOccurred(const QString& endpointId, const QString& message);

private:
    QMap<QString, Endpoint*> m_endpoints; ///< ID 有序映射到本对象拥有的端点；热重载会使全部旧指针失效。

    /** @brief 按 Protocol 实例化具体 Endpoint；不支持的协议返回 nullptr 并报告错误。 */
    Endpoint* createEndpoint(const EndpointConfig& config);
};
