#pragma once

#include <QObject>
#include <QJsonObject>
#include <memory>
#include "CommunicationManager.h"
#include "SystemBus.h"
#include "RadarDataProcessor.h"
#include "BusToCommRouter.h"
#include "DphPublisher.h"
#include "ConfigManager.h"
#include "DataStore.h"
#include "LatencyStats.h"

class RadarWorkModeController;
class IWorkModeCommandSender;
class HttpServer;

/**
 * @brief 系统健康报告
 * @details healthReport() 的返回类型，聚合各子系统运行状态
 */
struct AppHealth {
    struct SystemBusStatus {
        bool running = false;
        int subscriptionCount = 0;
    };
    struct CommStatus {
        int totalEndpoints = 0;
        int openEndpoints = 0;
    };
    struct ProcessingStatus {
        uint64_t generalPackageCount = 0;
        uint64_t trackInsertCount = 0;
        uint64_t routedCount = 0;
    };
    struct DatabaseStatus {
        bool configured = false;
        bool connected = false;
    };

    SystemBusStatus systemBus;
    CommStatus comm;
    ProcessingStatus processing;
    DatabaseStatus database;
    LatencyStats uplinkLatency;
    LatencyStats downlinkLatency;
    qint64 timestampMs = 0;          ///< 报告生成时间（currentMSecsSinceEpoch）
};

/**
 * @brief 应用总控类（非GUI）
 * @details 从旧的 NewAntiDrone(QMainWindow) 中抽出业务逻辑部分
 *          负责初始化通信层、SystemBus、数据处理等核心服务
 *          不依赖 Qt Widgets，可在 headless 环境运行
 *
 * 职责：
 * - 管理 CommunicationManager 生命周期
 * - 管理 RadarDataProcessor 生命周期（正向流解析/写库）
 * - 管理 BusToCommRouter 生命周期（反向流路由）
 * - 周期性发送雷达数据包头 (DPH)
 * - 统一协调各层组件启动/停止
 */
class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    /**
     * @brief 初始化核心服务
     * @return 是否初始化成功
     */
    bool initialize();

    /**
     * @brief 启动所有服务
     */
    void start();

    /**
     * @brief 停止所有服务
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
     * @brief 关联 HttpServer（必须在 initialize() 之后、start() 之前调用）
     */
    void setHttpServer(HttpServer* server);

    /**
     * @brief 启动 Web 服务（注册 REST 路由 + WS 定时器 + subscribeToBus）
     */
    void startWebService();

    /**
     * @brief 注册 REST API 路由（GET/POST handler + Logger 回调 → WS）
     */
    void registerApiRoutes();

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
     * @param source 事件来源
     * @param detail 事件详情
     */
    void insertSystemEvent(const QString& category,
                           const QString& source,
                           const QString& detail);

    /**
     * @brief 初始化业务逻辑层（工作模式控制器等）
     * @return 是否成功
     */
    bool initializeBusinessLogic();

private:
    ConfigManager m_configManager;                          ///< 配置管理器（数据访问层）
    std::unique_ptr<DataStore> m_dataStore;                 ///< 数据存储（数据访问层）
    std::unique_ptr<CommunicationManager> m_commManager;    ///< 通信管理器
    std::unique_ptr<RadarDataProcessor> m_radarProcessor;   ///< 正向流处理器（阶段 A）
    std::unique_ptr<BusToCommRouter> m_busRouter;           ///< 反向流路由器（阶段 B）
    std::unique_ptr<DphPublisher> m_dphPublisher;           ///< DPH 定时发送器（阶段 D3）
    std::unique_ptr<IWorkModeCommandSender> m_workModeSender;    ///< 工作模式命令发送器
    std::unique_ptr<RadarWorkModeController> m_workModeController; ///< 工作模式状态机
    uint16_t m_reverseTargetDevice = DEVICE_RADAR_NH_KU_1;
    bool m_dphEnabled = true;
    bool m_running = false;

    HttpServer* m_httpServer = nullptr;   ///< Web 服务（外部注入，由 main 创建）
    QTimer* m_statusTimer = nullptr;      ///< WS 状态广播定时器
};
