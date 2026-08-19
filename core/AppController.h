/**
 * @file AppController.h
 * @brief 定义无界面的应用组合根，统一装配核心实时链路及其外围服务。
 * @details AppController 是进程启动后创建各子系统的唯一入口。它从配置文件和数据库恢复运行环境，
 *          依次建立“UDP 接收 → 厂家协议解析 → DataPacket 总线 → 航迹显示/存储”正向链路，
 *          以及“业务命令 → DataPacket 总线 → UDP 发送”反向链路。类本身不解析协议、不保存航迹、
 *          不生成 Web DTO，只负责对象所有权、依赖注入、启动顺序、降级策略和反向关闭顺序。
 *
 * @par 新人阅读入口
 * 先阅读 initialize() 了解对象装配顺序，再阅读 start()/stop() 区分“数据采集运行态”和
 * Web、数据库、总线等进程级服务；最终退出顺序见 shutdown()。
 */

#pragma once

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <memory>
#include "AppTypes.h"
#include "CommunicationManager.h"
#include "SystemBus.h"
#include "RadarDataProcessor.h"
#include "BusToCommRouter.h"
#include "DphPublisher.h"
#include "ConfigManager.h"
#include "DataStore.h"
#include "LatencyStats.h"
#include "PipelineLatencyMonitor.h"

class RadarWorkModeController;
class IWorkModeCommandSender;
class DeviceRegistry;
class TaskService;
class RuntimeSettingsService;
class TrackDisplayManager;
class TrackDisplayWebBridge;
class TrackStorage;
class QTimer;

namespace web { class WebService; }
class WebApiController;

// WebServiceResult and AppHealth are in AppTypes.h

/**
 * @brief 应用总控类（非GUI）
 * @details 该类不依赖 Qt Widgets，可在无界面环境运行。除 SystemBus 单例外，核心组件主要由
 *          unique_ptr 持有；传给子组件的裸指针均为非拥有关系，生命周期由本类的成员声明顺序及
 *          shutdown() 的显式停止顺序保证。
 *
 * 职责：
 * - 管理 CommunicationManager 生命周期
 * - 管理 RadarDataProcessor 生命周期（正向流解析/发布）
 * - 管理 TrackStorage 生命周期（独立有界归档分支）
 * - 管理 BusToCommRouter 生命周期（反向流路由）
 * - 周期性发送雷达数据包头 (DPH)
 * - 统一协调各层组件启动/停止
 */
class AppController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 创建尚未初始化的应用组合根。
     * @param[in] parent Qt 父对象；通常为空，由 QCoreApplication 所在作用域管理本对象。
     * @note 构造函数不访问配置、数据库或网络，实际装配由 initialize() 完成。
     */
    explicit AppController(QObject* parent = nullptr);
    /** @brief 执行进程级关闭，并在所有订阅者退出后关闭 SystemBus。 */
    ~AppController() override;

    /**
     * @brief 按依赖顺序装配核心服务，但不打开通信端点。
     * @details 依次初始化数据访问、通信对象、处理/路由管线、业务服务、设备与任务台账、航迹存储和
     *          显示管理器。数据库不可用时允许部分功能降级；配置、通信对象或核心处理管线失败则返回失败。
     * @return 全部不可降级的核心组件装配成功时返回 true。
     * @post 成功后可调用 start() 打开采集链路，也可先调用 startWebService() 建立管理界面。
     */
    bool initialize();

    /**
     * @brief 打开全部通信端点并启动 50 ms DPH 周期发送，进入数据采集运行态。
     * @note 可重复调用；已经运行时不重复打开端点或创建定时任务。
     */
    void start();

    /**
     * @brief 停止数据采集运行态，同时保留总线订阅、Web 服务和数据库连接。
     * @details 用于任务暂停或设备维护后的快速恢复；它与进程退出使用的 shutdown() 不同。
     */
    void stop();

    /**
     * @brief 获取通信管理器
     * @return 通信管理器指针
     */
    CommunicationManager* commManager() const { return m_commManager.get(); }

    /**
     * @brief 获取系统总线
     * @return 系统总线引用
     */
    SystemBus& systemBus() { return SystemBus::getInstance(); }

    /**
     * @brief 获取配置管理器
     * @return 配置管理器引用（用于业务模块读取配置）
     */
    ConfigManager& configManager() { return m_configManager; }

    /**
     * @brief 获取数据存储
     * @return 数据存储指针；若数据库未连接成功则可能为 nullptr 或 isConnected()==false
     */
    DataStore* dataStore() const { return m_dataStore.get(); }

    /**
     * @brief 获取雷达工作模式控制器
     * @return 控制器指针；初始化完成前为 nullptr
     */
    RadarWorkModeController* workModeController() const { return m_workModeController.get(); }

    /**
     * @brief 获取系统健康报告
     * @return 各子系统运行状态快照
     */
    AppHealth healthReport() const;

    /**
     * @brief 启动 Web 服务（创建 web::WebService + WebApiController）
     * @return 启动结果（含实际端口号）
     */
    WebServiceResult startWebService(quint16 httpPort = 8088, quint16 wsPort = 8089);

    /** @brief 返回当前持久化的 endpoints.json 根对象，不代表端点此刻一定已打开。 */
    QJsonObject endpointsConfigJson();
    /**
     * @brief 校验、热重载并持久化完整端点配置。
     * @param[in] root 新的 endpoints.json 根对象。
     * @param[out] errorOut 可选错误文本接收位置。
     * @return 运行时重载和配置落盘均成功时返回 true；失败时尽量恢复旧运行时配置。
     */
    bool updateEndpointsConfig(const QJsonObject& root, QString* errorOut = nullptr);
    /** @brief 合并一个端点后复用完整配置更新事务，路径 id 与请求体 id 不一致时拒绝更新。 */
    bool updateEndpointConfig(const QString& id, const QJsonObject& endpoint, QString* errorOut = nullptr);

private slots:
    /**
     * @brief 处理通信层错误
     * @param endpointId 端点ID
     * @param message 错误信息
     * @details 仅处理错误事件（写 system_events + 发布错误到总线）；
     *          正向业务数据已由 RadarDataProcessor 接管。
     */
    void onCommError(const QString& endpointId, const QString& message);

private:
    /**
     * @brief 初始化数据访问层
     * @return 是否成功完成配置目录定位与数据存储初始化
     */
    bool initializeDataAccess();

    /**
     * @brief 初始化通信层（从 JSON 配置加载端点）
     * @return 是否成功
     */
    bool initializeCommunication();

    /**
     * @brief 初始化业务装配层
     * @return 是否成功完成 processor.json 相关装配
     */
    bool initializeProcessorPipeline();

    /**
     * @brief 根据数据库配置建立连接
     * @param dbConfig 数据库配置对象
     * @return 是否连接成功
     */
    bool connectDatabaseFromConfig(QJsonObject dbConfig);

    /**
     * @brief 解析数据库文件路径
     * @param dbPath 原始配置路径
     * @return 转换后的绝对路径或原始路径
     */
    QString resolveDatabasePath(const QString& dbPath) const;

    /**
     * @brief 初始化数据库表结构
     * @return 是否全部成功；失败时会记录日志但不阻塞启动
     */
    bool ensureDatabaseSchema();

    /**
     * @brief 确保 system_events 表及索引存在
     * @return 是否成功
     */
    bool ensureSystemEventsSchema();

    /**
     * @brief 确保 tracks 表及索引存在
     * @return 是否成功
     */
    bool ensureTracksSchema();

    bool ensurePipelineMetricsSchema();
    void startLatencyPersistence();
    void flushLatencyMetrics();

    /**
     * @brief 将端点配置加入通信管理器
     * @param configs 端点配置列表
     * @return 是否至少成功添加一个端点
     */
    bool addConfiguredEndpoints(const QVector<EndpointConfig>& configs);

    /**
     * @brief 连接核心内部信号
     */
    void connectCoreSignals();

    /**
     * @brief 按 processor.json 配置正向流处理器
     * @param processorConfig processor.json 内容
     */
    void configureRadarProcessor(const QJsonObject& processorConfig);

    /**
     * @brief 按 processor.json 配置反向流路由器
     * @param processorConfig processor.json 内容
     */
    void configureBusRouter(const QJsonObject& processorConfig);

    /**
     * @brief 按 processor.json 配置 DPH 发布器
     * @param processorConfig processor.json 内容
     */
    void configureDphPublisher(const QJsonObject& processorConfig);

    /**
     * @brief 注册默认端点源映射
     */
    void registerDefaultEndpointSources();

    /**
     * @brief 注册 processor.json 中声明的端点源映射
     * @param processorConfig processor.json 内容
     * @return 是否至少注册了一条映射
     */
    bool registerConfiguredEndpointSources(const QJsonObject& processorConfig);

    /**
     * @brief 注册默认反向流路由
     */
    void registerDefaultRoutes();

    /**
     * @brief 注册 processor.json 中声明的反向流路由
     * @param processorConfig processor.json 内容
     * @return 是否至少注册了一条路由
     */
    bool registerConfiguredRoutes(const QJsonObject& processorConfig);

    /**
     * @brief 解析 processor.json 中的默认路由
     * @param processorConfig processor.json 内容
     * @return 默认路由配置
     */
    BusToCommRouter::Route resolveDefaultRoute(const QJsonObject& processorConfig) const;

    /**
     * @brief 写入一条 system_events 事件记录
     * @param category 事件分类
     * @param source 事件来源（如 endpointId）
     * @param detail 事件详情
     * @param deviceId 业务设备 id（可空）
     * @param taskId 当前任务 id（可空）
     */
    void insertSystemEvent(const QString& category,
                           const QString& source,
                           const QString& detail,
                           const QString& deviceId = {},
                           const QString& taskId = {});

    /**
     * @brief 初始化业务逻辑层（工作模式控制器等）
     * @return 是否成功
     */
    bool initializeBusinessLogic();

    /**
     * @brief 在 DeviceRegistry / TrackDisplayManager 就绪后接通航迹显示链路
     */
    void wireTrackDisplayPipeline();
    void printStartupDiagnostics() const;

    /** @brief stop() 的幂等实现，只关闭通信端点和 DPH 定时发送。 */
    void stopRuntime();
    /**
     * @brief 释放 AppController 持有的 Web/TrackDisplay/订阅等资源（幂等）。
     * @note 不是运行态 stop（见 stop()/stopRuntime()）；不会关闭 SystemBus。
     *       SystemBus::shutdown() 仅在析构最终阶段调用（进程退出级）。
     */
    void shutdown();

private:
    PipelineLatencyMonitor m_pipelineLatency;              ///< 聚合接收、处理、显示和发送链路的进程内单调时钟延时。
    QMap<QString, uint64_t> m_lastPersistedLatencyCounts;  ///< 各阶段上次落库的样本数，用于跳过没有新样本的周期。
    QTimer* m_latencyPersistTimer = nullptr;               ///< 由本对象作为 Qt parent 持有的指标落库定时器。
    ConfigManager m_configManager;                          ///< 配置管理器（数据访问层）
    QJsonObject m_databaseConfig;                           ///< 已解析绝对路径的线程连接配置
    std::unique_ptr<DataStore> m_dataStore;                 ///< 数据存储（数据访问层）
    std::unique_ptr<CommunicationManager> m_commManager;    ///< 通信管理器
    std::unique_ptr<RadarDataProcessor> m_radarProcessor;   ///< 正向流处理器（阶段 A）
    std::unique_ptr<BusToCommRouter> m_busRouter;           ///< 反向流路由器（阶段 B）
    std::unique_ptr<DphPublisher> m_dphPublisher;           ///< 以配置周期发送协议大包头；默认周期为 50 ms。
    std::unique_ptr<IWorkModeCommandSender> m_workModeSender;    ///< 工作模式命令发送器
    std::unique_ptr<RadarWorkModeController> m_workModeController; ///< 工作模式状态机
    std::unique_ptr<DeviceRegistry> m_deviceRegistry;              ///< 设备台账
    std::unique_ptr<TaskService> m_taskService;                  ///< 任务台账
    std::unique_ptr<RuntimeSettingsService> m_runtimeSettings;      ///< 运行时参数
    std::unique_ptr<TrackDisplayManager> m_trackDisplayManager;      ///< 航迹显示管理
    std::unique_ptr<TrackDisplayWebBridge> m_trackDisplayWebBridge;  ///< 航迹初始快照/增量总线→WS
    std::unique_ptr<TrackStorage> m_trackStorage;                    ///< 有界异步航迹归档
    uint16_t m_reverseTargetDevice = DEVICE_RADAR_NH_KU_1; ///< 反向命令默认目标设备类型。
    bool m_dphEnabled = true;                              ///< processor.json 是否启用周期 DPH。
    bool m_running = false;                                ///< 通信端点和 DPH 是否处于运行态。
    bool m_shutdown = false;                               ///< 进程级关闭幂等闩锁；置位后不再重复释放资源。

    std::unique_ptr<web::WebService> m_webService;       ///< HTTP/WebSocket 传输服务；不拥有业务服务。
    std::unique_ptr<WebApiController> m_webApi;          ///< REST 路由及状态推送适配层。
    SubscriptionId m_runtimeSettingsSubId = 0;           ///< 运行参数变更到 DPH 的总线订阅；0 表示未订阅。
};
