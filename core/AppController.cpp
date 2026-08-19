#include "AppController.h"
#include "web/WebService.h"
#include "web/WebApiController.h"
#include "web/TrackDisplayWebBridge.h"
#include "web/TaskApiRoutes.h"
#include "web/SystemEventApiRoutes.h"
#include "web/TrackHistoryApiRoutes.h"
#include "web/EndpointApiRoutes.h"
#include "business/DeviceRegistry.h"
#include "business/TaskService.h"
#include "business/RuntimeSettingsService.h"
#include "business/TrackDisplayManager.h"
#include "data/TrackStorage.h"
#include "EndpointConfigLoader.h"
#include "bus/SystemBus.h"
#include "GenericPackets.h"
#include "protocol/StructureReceive.h"
#include "BusWorkModeSender.h"
#include "RadarWorkModeController.h"
#include "log/Logger.h"
#include "log/LogContext.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <cstring>

/**
 * @brief 创建尚未装配子系统的组合根；构造阶段不访问网络、配置或数据库。
 */
AppController::AppController(QObject* parent)
    : QObject(parent)
{
}

AppController::~AppController()
{
    shutdown();
    SystemBus::getInstance().shutdown();
}

/**
 * @brief 退出采集运行态，但保留能够快速恢复所需的进程级基础设施。
 * @details 先清除 m_running 防止外部状态查询继续报告运行，再停止 DPH 定时器，最后关闭接收/发送端点。
 *          不取消总线订阅、不停止 Web、不关闭数据库，因此之后调用 start() 可直接恢复采集。
 */
void AppController::stopRuntime()
{
    if (!m_running)
        return;

    m_running = false;

    if (m_dphPublisher)
        m_dphPublisher->stop();

    if (m_commManager)
        m_commManager->stopAll();

    qDebug() << QStringLiteral("[AppController] 运行态已停止（通信/DPH），总线订阅与 Web 保持");
}

/**
 * @brief 按依赖关系的逆序执行进程级关闭。
 * @details 先置位幂等闩锁并阻止 Web 命令进入，再停止显示订阅与桥接；随后关闭采集、落盘最后一批
 *          延时指标、取消总线订阅、停止 Web 和航迹存储线程，最后断开数据库。SystemBus 在析构函数中
 *          等这些消费者全部退出后才关闭，避免队列回调访问已释放对象。
 */
void AppController::shutdown()
{
    if (m_shutdown)
        return;
    m_shutdown = true;

    if (m_webApi) {
        m_webApi->shutdown();
        m_webApi.reset();
    }

    if (m_trackDisplayManager) {
        m_trackDisplayManager->stop();
        m_trackDisplayManager->clear();
    }

    if (m_trackDisplayWebBridge) {
        m_trackDisplayWebBridge->stop();
        m_trackDisplayWebBridge.reset();
    }

    stopRuntime();

    if (m_latencyPersistTimer)
        m_latencyPersistTimer->stop();
    flushLatencyMetrics();

    if (m_busRouter) {
        m_busRouter->unsubscribe();
        qDebug() << QStringLiteral("[AppController] BusToCommRouter 已停止订阅");
    }

    if (m_runtimeSettingsSubId != 0) {
        SystemBus::getInstance().unsubscribe(m_runtimeSettingsSubId);
        m_runtimeSettingsSubId = 0;
    }

    if (m_webService) {
        m_webService->stop();
        m_webService.reset();
    }

    if (m_trackStorage) {
        m_trackStorage->stop();
        m_trackStorage.reset();
    }

    if (m_dataStore)
        m_dataStore->disconnect();

    qDebug() << QStringLiteral("[AppController] 进程级 shutdown 完成");
}

/**
 * @brief 按数据访问、通信、处理器、业务服务、存储和显示的依赖顺序完成一次性装配。
 * @details 数据库相关能力允许降级，通信和核心处理器失败则终止初始化；通信端点在 start() 前保持关闭。
 */
bool AppController::initialize()
{
    // 协议结构体尺寸诊断 (static_assert 的基准值)
    qInfo() << "[Protocol] GeneralPackage =" << sizeof(GeneralPackage)
            << " DataPackageHeader =" << sizeof(DataPackageHeader)
            << " workStandby =" << sizeof(workStandby)
            << " TargetType =" << sizeof(TargetType)
            << " host_computer =" << sizeof(host_computer);

    if (!initializeDataAccess()) {
        return false;
    }

    if (!initializeCommunication()) {
        qCritical() << QStringLiteral("[AppController] 通信层初始化失败");
        return false;
    }

    if (!initializeProcessorPipeline()) {
        qCritical() << QStringLiteral("[AppController] 业务装配初始化失败");
        return false;
    }

    if (!initializeBusinessLogic()) {
        qCritical() << QStringLiteral("[AppController] 业务逻辑层初始化失败");
        return false;
    }

    // 初始化设备台账
    if (m_dataStore && m_dataStore->isConnected()) {
        m_deviceRegistry = std::make_unique<DeviceRegistry>(m_dataStore.get());
        if (!m_deviceRegistry->ensureSchema()) {
            qWarning() << "[AppController] 设备表创建失败，设备台账不可用";
        } else {
            m_deviceRegistry->ensureDefaultControllableRadar();
            auto devices = m_deviceRegistry->list();
            qDebug() << "[AppController] 设备台账已就绪，共" << devices.size() << "台设备";
            for (const auto& d : devices) {
                qDebug() << "  -" << d.id << d.name << "endpointId=" << d.endpointId
                         << "kind=" << static_cast<int>(d.kind);
            }
        }
    } else {
        qWarning() << "[AppController] 数据库不可用，设备台账跳过";
    }

    if (m_dataStore && m_dataStore->isConnected()) {
        m_taskService = std::make_unique<TaskService>(m_dataStore.get(), m_deviceRegistry.get());
        if (!m_taskService->ensureSchema()) {
            qWarning() << "[AppController] 任务表创建失败，TaskService 不可用";
            m_taskService.reset();
        } else {
            qDebug() << "[AppController] TaskService 已就绪，共"
                     << m_taskService->list().size() << "个任务";
            m_taskService->refreshActiveTaskCache();
        }
    } else {
        qWarning() << "[AppController] 数据库不可用，TaskService 跳过";
    }

    if (m_deviceRegistry)
        m_deviceRegistry->refreshEndpointCache();

    if (m_radarProcessor && m_taskService)
        m_radarProcessor->setTaskService(m_taskService.get());

    if (m_dataStore && m_dataStore->isConnected()
        && m_dataStore->tableExists(QStringLiteral("tracks"))
        && !m_databaseConfig.isEmpty()) {
        m_trackStorage = std::make_unique<TrackStorage>();
        if (!m_trackStorage->start(m_databaseConfig)) {
            qWarning() << QStringLiteral("[AppController] TrackStorage 启动失败，将以无航迹归档模式运行");
            m_trackStorage.reset();
        }
    } else {
        qWarning() << QStringLiteral("[AppController] tracks 表或数据库不可用，航迹归档未启动");
    }

    m_runtimeSettings = std::make_unique<RuntimeSettingsService>();
    qDebug() << "[AppController] 运行时参数服务已就绪";

    // 航迹显示管理器
    m_trackDisplayManager = std::make_unique<TrackDisplayManager>(this);
    m_trackDisplayManager->setLatencyMonitor(&m_pipelineLatency);
    wireTrackDisplayPipeline();
    // 航迹订阅属于 core 基座，必须先于通信端点启动；WebBridge 可稍后接入。
    m_trackDisplayManager->start();

    printStartupDiagnostics();

    qDebug() << QStringLiteral("[AppController] 初始化完成");
    return true;
}

/**
 * @brief 定位配置目录、读取 database.json、连接数据库并创建核心表结构。
 */
bool AppController::initializeDataAccess()
{
    if (!m_configManager.loadConfigDir()) {
        qCritical() << QStringLiteral("[AppController] 配置目录加载失败");
        return false;
    }

    m_dataStore = std::make_unique<DataStore>(this);

    const QJsonObject dbConfig = m_configManager.getConfig(QStringLiteral("database"));
    if (dbConfig.isEmpty()) {
        qWarning() << QStringLiteral("[AppController] 未找到 database.json，跳过数据库连接");
        return true;
    }

    if (!connectDatabaseFromConfig(dbConfig)) {
        qWarning() << QStringLiteral("[AppController] 数据库连接失败，将以无持久化模式运行");
        return true;
    }

    if (!ensureDatabaseSchema()) {
        qWarning() << QStringLiteral("[AppController] 数据库表结构初始化不完整，程序继续运行");
    }
    startLatencyPersistence();

    return true;
}

/**
 * @brief 解析 SQLite 相对路径后创建 DataStore；连接失败时保留无数据库降级模式。
 */
bool AppController::connectDatabaseFromConfig(QJsonObject dbConfig)
{
    const QString driver = dbConfig.value(QStringLiteral("driver"))
                               .toString(QStringLiteral("QSQLITE"));
    const QString dbPath = dbConfig.value(QStringLiteral("database")).toString();
    if (driver.compare(QStringLiteral("QSQLITE"), Qt::CaseInsensitive) == 0
        && !dbPath.isEmpty()) {
        dbConfig.insert(QStringLiteral("database"), resolveDatabasePath(dbPath));
    }

    const bool connected = m_dataStore && m_dataStore->connectFromConfig(dbConfig);
    if (connected)
        m_databaseConfig = dbConfig;
    return connected;
}

/**
 * @brief 将 SQLite 相对路径锚定到配置目录，绝对路径和非文件数据库名原样返回。
 */
QString AppController::resolveDatabasePath(const QString& dbPath) const
{
    if (dbPath.isEmpty() || QDir::isAbsolutePath(dbPath)) {
        return dbPath;
    }

    QDir projectRoot(m_configManager.configDir());
    projectRoot.cdUp();
    return projectRoot.absoluteFilePath(dbPath);
}

/**
 * @brief 依次确保系统事件、航迹和管线指标 schema，就单项失败记录告警并汇总结果。
 */
bool AppController::ensureDatabaseSchema()
{
    if (!m_dataStore || !m_dataStore->isConnected()) {
        return false;
    }

    const bool systemEventsOk = ensureSystemEventsSchema();
    const bool tracksOk = ensureTracksSchema();
    const bool pipelineMetricsOk = ensurePipelineMetricsSchema();
    return systemEventsOk && tracksOk && pipelineMetricsOk;
}

/**
 * @brief 创建按阶段保存累计数和分位数的 pipeline_metrics 表及时间索引。
 */
bool AppController::ensurePipelineMetricsSchema()
{
    const bool postgres = m_dataStore->database().driverName().startsWith(
        QStringLiteral("QPSQL"));
    const QString ddl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS pipeline_metrics ("
        "  id %1,"
        "  window_end_ms BIGINT NOT NULL,"
        "  stage TEXT NOT NULL,"
        "  unit TEXT NOT NULL DEFAULT 'us',"
        "  sample_count BIGINT NOT NULL,"
        "  window_sample_count BIGINT NOT NULL,"
        "  min_us BIGINT NOT NULL,"
        "  avg_us BIGINT NOT NULL,"
        "  p50_us BIGINT NOT NULL,"
        "  p95_us BIGINT NOT NULL,"
        "  p99_us BIGINT NOT NULL,"
        "  max_us BIGINT NOT NULL"
        ")").arg(postgres
                      ? QStringLiteral("BIGSERIAL PRIMARY KEY")
                      : QStringLiteral("INTEGER PRIMARY KEY AUTOINCREMENT"));

    bool ok = m_dataStore->ensureTable(QStringLiteral("pipeline_metrics"), ddl);
    if (postgres) {
        ok = m_dataStore->execute(QStringLiteral(
            "ALTER TABLE pipeline_metrics "
            "ALTER COLUMN window_end_ms TYPE BIGINT, "
            "ALTER COLUMN sample_count TYPE BIGINT, "
            "ALTER COLUMN window_sample_count TYPE BIGINT, "
            "ALTER COLUMN min_us TYPE BIGINT, ALTER COLUMN avg_us TYPE BIGINT, "
            "ALTER COLUMN p50_us TYPE BIGINT, ALTER COLUMN p95_us TYPE BIGINT, "
            "ALTER COLUMN p99_us TYPE BIGINT, ALTER COLUMN max_us TYPE BIGINT")) && ok;
    }
    ok = m_dataStore->execute(QStringLiteral(
             "CREATE INDEX IF NOT EXISTS idx_pipeline_metrics_stage_time "
             "ON pipeline_metrics(stage, window_end_ms)")) && ok;
    return ok;
}

/**
 * @brief 创建低频指标落库定时器，仅在数据库和表可用时启用。
 */
void AppController::startLatencyPersistence()
{
    if (!m_dataStore || !m_dataStore->isConnected() || m_latencyPersistTimer)
        return;

    m_latencyPersistTimer = new QTimer(this);
    m_latencyPersistTimer->setInterval(10000);
    connect(m_latencyPersistTimer, &QTimer::timeout,
            this, &AppController::flushLatencyMetrics);
    m_latencyPersistTimer->start();
}

/**
 * @brief 只持久化自上次写入后样本数发生变化的阶段快照，避免周期性重复行。
 */
void AppController::flushLatencyMetrics()
{
    if (!m_dataStore || !m_dataStore->isConnected()
        || !m_dataStore->tableExists(QStringLiteral("pipeline_metrics"))) {
        return;
    }

    const auto snapshot = m_pipelineLatency.snapshot();
    const qint64 windowEndMs = QDateTime::currentMSecsSinceEpoch();
    QVector<QVariantMap> rows;
    QMap<QString, uint64_t> persistedCounts;

    for (auto it = snapshot.stages.cbegin(); it != snapshot.stages.cend(); ++it) {
        const auto& stats = it.value();
        if (stats.sampleCount == 0
            || stats.sampleCount == m_lastPersistedLatencyCounts.value(it.key(), 0)) {
            continue;
        }

        QVariantMap row;
        row.insert(QStringLiteral("window_end_ms"), windowEndMs);
        row.insert(QStringLiteral("stage"), it.key());
        row.insert(QStringLiteral("unit"), snapshot.unit);
        row.insert(QStringLiteral("sample_count"), static_cast<qulonglong>(stats.sampleCount));
        row.insert(QStringLiteral("window_sample_count"),
                   static_cast<qulonglong>(stats.windowSampleCount));
        row.insert(QStringLiteral("min_us"), stats.minUs);
        row.insert(QStringLiteral("avg_us"), stats.avgUs);
        row.insert(QStringLiteral("p50_us"), stats.p50Us);
        row.insert(QStringLiteral("p95_us"), stats.p95Us);
        row.insert(QStringLiteral("p99_us"), stats.p99Us);
        row.insert(QStringLiteral("max_us"), stats.maxUs);
        rows.append(std::move(row));
        persistedCounts.insert(it.key(), stats.sampleCount);
    }

    if (rows.isEmpty())
        return;
    if (!m_dataStore->insertBatch(QStringLiteral("pipeline_metrics"), rows)) {
        qWarning() << QStringLiteral("[Latency] pipeline_metrics 批量写入失败: %1 行")
                          .arg(rows.size());
        return;
    }

    for (auto it = persistedCounts.cbegin(); it != persistedCounts.cend(); ++it)
        m_lastPersistedLatencyCounts.insert(it.key(), it.value());

    qInfo().noquote() << QStringLiteral("[Latency] 已保存 pipeline_metrics: %1 个阶段")
                            .arg(rows.size());

    const auto displayIt = snapshot.stages.constFind(QStringLiteral("ingressToDisplay"));
    if (displayIt != snapshot.stages.cend()
        && persistedCounts.contains(QStringLiteral("ingressToDisplay"))) {
        qInfo().noquote() << QStringLiteral(
            "[Latency] ingressToDisplay count=%1 p50=%2us p95=%3us p99=%4us max=%5us")
            .arg(displayIt->sampleCount)
            .arg(displayIt->p50Us)
            .arg(displayIt->p95Us)
            .arg(displayIt->p99Us)
            .arg(displayIt->maxUs);
    }
}

/**
 * @brief 创建可按任务、设备、类别和时间检索的 system_events 表及兼容索引。
 */
bool AppController::ensureSystemEventsSchema()
{
    const bool postgres = m_dataStore->database().driverName().startsWith(
        QStringLiteral("QPSQL"));
    const QString kSystemEventsDdl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS system_events ("
        "  id %1,"
        "  ts_ms BIGINT NOT NULL,"
        "  category TEXT NOT NULL,"
        "  source TEXT,"
        "  detail TEXT,"
        "  size INTEGER,"
        "  task_id TEXT,"
        "  device_id TEXT"
        ")"
    ).arg(postgres
              ? QStringLiteral("BIGSERIAL PRIMARY KEY")
              : QStringLiteral("INTEGER PRIMARY KEY AUTOINCREMENT"));

    bool ok = true;
    if (!m_dataStore->ensureTable(QStringLiteral("system_events"), kSystemEventsDdl)) {
        qWarning() << QStringLiteral("[AppController] system_events 表初始化失败");
        ok = false;
    }
    if (postgres) {
        ok = m_dataStore->execute(QStringLiteral(
            "ALTER TABLE system_events ALTER COLUMN ts_ms TYPE BIGINT")) && ok;
    }

    auto ensureSystemEventsColumn = [this, postgres](const QString& column,
                                                      const QString& type) -> bool {
        const auto cols = postgres
            ? m_dataStore->query(QStringLiteral(
                  "SELECT column_name AS name FROM information_schema.columns "
                  "WHERE table_schema = current_schema() AND table_name = 'system_events'"))
            : m_dataStore->query(QStringLiteral("PRAGMA table_info(system_events)"));
        for (const auto& col : cols) {
            if (col.value(QStringLiteral("name")).toString() == column)
                return true;
        }
        if (!m_dataStore->execute(
                QStringLiteral("ALTER TABLE system_events ADD COLUMN %1 %2").arg(column, type))) {
            qWarning() << QStringLiteral("[AppController] system_events 表添加列失败: %1").arg(column);
            return false;
        }
        return true;
    };
    ok = ensureSystemEventsColumn(QStringLiteral("task_id"), QStringLiteral("TEXT")) && ok;
    ok = ensureSystemEventsColumn(QStringLiteral("device_id"), QStringLiteral("TEXT")) && ok;

    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sysevt_ts ON system_events(ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] system_events 时间索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sysevt_cat ON system_events(category)"))) {
        qWarning() << QStringLiteral("[AppController] system_events 分类索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sysevt_task_ts ON system_events(task_id, ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] system_events task_ts 索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sysevt_device_ts ON system_events(device_id, ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] system_events device_ts 索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_sysevt_task_device_ts ON system_events(task_id, device_id, ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] system_events task_device_ts 索引初始化失败");
        ok = false;
    }

    return ok;
}

/**
 * @brief 创建与 TrackPacket 字段和单位一致的 tracks 表，并迁移历史缺失列。
 */
bool AppController::ensureTracksSchema()
{
    const bool postgres = m_dataStore->database().driverName().startsWith(
        QStringLiteral("QPSQL"));
    const QString kTracksDdl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks ("
        "  id %1,"
        "  ts_ms BIGINT NOT NULL,"
        "  source TEXT,"
        "  task_id TEXT,"
        "  device_id TEXT,"
        "  scene_num INTEGER,"
        "  target_id INTEGER,"
        "  range_m REAL,"
        "  azimuth_rad REAL,"
        "  elevation_rad REAL,"
        "  radial_speed REAL,"
        "  altitude_m REAL,"
        "  x REAL, y REAL, z REAL"
        ")"
    ).arg(postgres
              ? QStringLiteral("BIGSERIAL PRIMARY KEY")
              : QStringLiteral("INTEGER PRIMARY KEY AUTOINCREMENT"));

    bool ok = true;
    if (!m_dataStore->ensureTable(QStringLiteral("tracks"), kTracksDdl)) {
        qWarning() << QStringLiteral("[AppController] tracks 表初始化失败");
        ok = false;
    }
    if (postgres) {
        ok = m_dataStore->execute(QStringLiteral(
            "ALTER TABLE tracks ALTER COLUMN ts_ms TYPE BIGINT")) && ok;
    }

    auto ensureTracksColumn = [this, postgres](const QString& column,
                                               const QString& type) -> bool {
        const auto cols = postgres
            ? m_dataStore->query(QStringLiteral(
                  "SELECT column_name AS name FROM information_schema.columns "
                  "WHERE table_schema = current_schema() AND table_name = 'tracks'"))
            : m_dataStore->query(QStringLiteral("PRAGMA table_info(tracks)"));
        for (const auto& col : cols) {
            if (col.value(QStringLiteral("name")).toString() == column)
                return true;
        }
        if (!m_dataStore->execute(
                QStringLiteral("ALTER TABLE tracks ADD COLUMN %1 %2").arg(column, type))) {
            qWarning() << QStringLiteral("[AppController] tracks 表添加列失败: %1").arg(column);
            return false;
        }
        return true;
    };
    ok = ensureTracksColumn(QStringLiteral("task_id"), QStringLiteral("TEXT")) && ok;
    ok = ensureTracksColumn(QStringLiteral("device_id"), QStringLiteral("TEXT")) && ok;

    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_ts ON tracks(ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] tracks 时间索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_target ON tracks(target_id)"))) {
        qWarning() << QStringLiteral("[AppController] tracks 目标索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_task_ts ON tracks(task_id, ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] tracks task_ts 索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_task_device ON tracks(task_id, device_id)"))) {
        qWarning() << QStringLiteral("[AppController] tracks task_device 索引初始化失败");
        ok = false;
    }
    if (!m_dataStore->execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_tracks_task_device_ts ON tracks(task_id, device_id, ts_ms)"))) {
        qWarning() << QStringLiteral("[AppController] tracks task_device_ts 索引初始化失败");
        ok = false;
    }

    return ok;
}

/**
 * @brief 打开通信端点，成功后启动 DPH；重复调用或未初始化时不重复改变状态。
 */
void AppController::start()
{
    if (m_running) {
        return;
    }

    // 校验 SystemBus 状态（单例，构造时已自动启动工作线程）
    if (!SystemBus::getInstance().isRunning()) {
        qCritical() << QStringLiteral("[AppController] SystemBus 未处于运行状态，无法启动服务");
        return;
    }
    qDebug() << QStringLiteral("[AppController] SystemBus 已就绪, 订阅数: %1")
                .arg(SystemBus::getInstance().getSubscriptionCount());

    // 启动通信层所有端点
    if (m_commManager) {
        m_commManager->startAll();
    }

    if (m_busRouter) {
        qDebug() << QStringLiteral("[AppController] BusToCommRouter 已就绪, 已路由=%1")
                    .arg(m_busRouter->routedCount());
    }

    if (m_dphPublisher && m_dphEnabled) {
        m_dphPublisher->start();
    }

    m_running = true;
    qDebug() << QStringLiteral("[AppController] 运行态已启动");
}

/**
 * @brief 委托 stopRuntime() 暂停采集，保留管理面和核心订阅供快速恢复。
 */
void AppController::stop()
{
    stopRuntime();
}

/**
 * @brief 从 endpoints.json 解析并创建全部未打开端点，至少一个有效端点才算成功。
 */
bool AppController::initializeCommunication()
{
    m_commManager = std::make_unique<CommunicationManager>(this);

    // 通过 ConfigManager 定位端点配置文件
    QString configPath = m_configManager.configFilePath(QStringLiteral("endpoints"));
    if (configPath.isEmpty() || !QFile::exists(configPath)) {
        qCritical() << QStringLiteral("[AppController] 未找到 endpoints.json: %1").arg(configPath);
        return false;
    }

    qDebug() << QStringLiteral("[AppController] 端点配置: %1")
                .arg(QDir::toNativeSeparators(configPath));

    // 配置校验
    QJsonObject endpointsJson = m_configManager.getConfig(QStringLiteral("endpoints"));
    for (const auto& err : ConfigManager::validateConfig(QStringLiteral("endpoints"), endpointsJson))
        qWarning().noquote() << QStringLiteral("[Config] endpoints: %1").arg(err);

    // 加载端点配置（EndpointConfigLoader 保留为端点专用解析器）
    QVector<EndpointConfig> configs = EndpointConfigLoader::loadFromJson(configPath);
    if (configs.isEmpty()) {
        qCritical() << QStringLiteral("[AppController] 未加载到任何端点配置");
        return false;
    }

    return addConfiguredEndpoints(configs);
}

/**
 * @brief 逐项加入通信管理器并汇总成功数量，允许记录单端点配置错误后继续诊断其余项。
 */
bool AppController::addConfiguredEndpoints(const QVector<EndpointConfig>& configs)
{
    bool hasEndpoint = false;
    for (const EndpointConfig& cfg : configs) {
        if (!m_commManager->addEndpoint(cfg)) {
            qWarning() << QStringLiteral("[AppController] 添加端点失败: %1").arg(cfg.id);
            continue;
        }

        hasEndpoint = true;
    }

    if (!hasEndpoint) {
        qCritical() << QStringLiteral("[AppController] 所有端点加载均失败");
    }

    return hasEndpoint;
}

/**
 * @brief 读取 processor.json 后装配上行处理器、反向路由和周期 DPH，并接通核心信号。
 */
bool AppController::initializeProcessorPipeline()
{
    connectCoreSignals();

    const QJsonObject processorConfig = m_configManager.getConfig(QStringLiteral("processor"));
    if (processorConfig.isEmpty()) {
        qWarning() << QStringLiteral("[AppController] processor.json 为空或不存在，将使用内置默认装配");
    } else {
        for (const auto& err : ConfigManager::validateConfig(QStringLiteral("processor"), processorConfig))
            qWarning().noquote() << QStringLiteral("[Config] processor: %1").arg(err);
    }

    configureRadarProcessor(processorConfig);
    configureBusRouter(processorConfig);
    configureDphPublisher(processorConfig);

    return true;
}

/**
 * @brief 将通信错误接入系统事件和总线错误链路；业务数据由 RadarDataProcessor 独占处理。
 */
void AppController::connectCoreSignals()
{
    const bool connected = connect(m_commManager.get(), &CommunicationManager::errorOccurred,
                                   this, &AppController::onCommError);
    if (!connected) {
        qWarning() << QStringLiteral("[AppController] CommunicationManager::errorOccurred 连接失败");
    }
}

/**
 * @brief 创建上行处理器，注入延时监控并注册配置或默认 endpoint/sourceType 映射。
 */
void AppController::configureRadarProcessor(const QJsonObject& processorConfig)
{
    m_radarProcessor = std::make_unique<RadarDataProcessor>(
        m_commManager.get(), this);
    m_radarProcessor->setLatencyMonitor(&m_pipelineLatency);

    if (!registerConfiguredEndpointSources(processorConfig)) {
        qDebug() << QStringLiteral("[AppController] 端点源映射使用内置默认值");
        registerDefaultEndpointSources();
    }

    qDebug() << QStringLiteral("[AppController] RadarDataProcessor 已创建并订阅 Comm::dataReceived");
}

/**
 * @brief 在设备、任务服务就绪后向上行处理器和显示管理器注入归属解析依赖。
 */
void AppController::wireTrackDisplayPipeline()
{
    if (!m_radarProcessor || !m_trackDisplayManager) {
        qWarning() << "[AppController] 航迹显示链路未接通: processor 或 TrackDisplayManager 不可用";
        return;
    }

    if (m_deviceRegistry)
        m_radarProcessor->setDeviceRegistry(m_deviceRegistry.get());
    else
        qWarning() << "[AppController] DeviceRegistry 不可用，TrackPacket 将无法映射 deviceId";

    if (m_taskService)
        m_trackDisplayManager->setTaskService(m_taskService.get());

    qDebug() << "[AppController] RadarDataProcessor → DataPacket{Track} → SystemBus → TrackDisplayManager 链路已装配";
}

/**
 * @brief 输出端点、数据库、任务和核心订阅状态，帮助定位启动降级而不改变运行状态。
 */
void AppController::printStartupDiagnostics() const
{
    qInfo() << "======== Startup Diagnostics ========";
    qInfo() << "configDir:" << m_configManager.configDir();

    const QJsonObject dbCfg = m_configManager.getConfig(QStringLiteral("database"));
    const QString dbName = dbCfg.value(QStringLiteral("database")).toString();
    const QString dbDriver = dbCfg.value(QStringLiteral("driver"))
                                 .toString(QStringLiteral("QSQLITE"));
    if (!dbName.isEmpty()) {
        qInfo() << "database:"
                << (dbDriver.compare(QStringLiteral("QSQLITE"), Qt::CaseInsensitive) == 0
                        ? resolveDatabasePath(dbName)
                        : dbName);
    }

    const QJsonObject epCfg = m_configManager.getConfig(QStringLiteral("endpoints"));
    for (const QJsonValue& v : epCfg.value(QStringLiteral("endpoints")).toArray()) {
        const QJsonObject ep = v.toObject();
        qInfo() << "endpoint:" << ep.value(QStringLiteral("id")).toString()
                << ep.value(QStringLiteral("localAddress")).toString()
                << ep.value(QStringLiteral("localPort")).toInt();
    }

    if (m_deviceRegistry) {
        for (const auto& d : m_deviceRegistry->list()) {
            if (d.kind != DeviceKind::Radar)
                continue;
            const QString mapped = m_deviceRegistry->cachedDeviceIdForEndpoint(d.endpointId);
            qInfo() << "device:" << d.id << "name:" << d.name
                    << "endpointId:" << d.endpointId << "cache->" << mapped;
        }
    }

    if (m_taskService) {
        if (const auto ctx = m_taskService->getActiveTask())
            qInfo() << "activeTask:" << ctx->id << "devices:" << ctx->deviceIds;
        else
            qInfo() << "activeTask: (none)";
    }
    qInfo() << "=====================================";
}

/**
 * @brief 解析 processor.json 的端点来源表，跳过缺失 ID 或无效类型并返回是否登记过有效项。
 */
bool AppController::registerConfiguredEndpointSources(const QJsonObject& processorConfig)
{
    bool hasEndpointSourceConfig = false;
    const QJsonArray endpointSources = processorConfig.value(QStringLiteral("endpointSources")).toArray();
    for (const QJsonValue& value : endpointSources) {
        const QJsonObject obj = value.toObject();
        const QString endpointId = obj.value(QStringLiteral("endpointId")).toString();
        const int sourceType = obj.value(QStringLiteral("sourceType")).toInt(-1);
        if (endpointId.isEmpty() || sourceType < 0) {
            continue;
        }

        m_radarProcessor->registerEndpointSource(endpointId, static_cast<uint16_t>(sourceType));
        hasEndpointSourceConfig = true;
    }

    return hasEndpointSourceConfig;
}

/**
 * @brief 为默认 radar_cpu 端点登记内置雷达来源类型，供最小配置启动。
 */
void AppController::registerDefaultEndpointSources()
{
    m_radarProcessor->registerEndpointSource(QStringLiteral("radar_cpu"), DEVICE_RADAR_NH_KU_1);
}

/**
 * @brief 创建反向路由器、装配默认和显式路由，并订阅目标雷达设备类型。
 */
void AppController::configureBusRouter(const QJsonObject& processorConfig)
{
    m_busRouter = std::make_unique<BusToCommRouter>(m_commManager.get(), this);
    m_busRouter->setDefaultRoute(resolveDefaultRoute(processorConfig));

    if (!registerConfiguredRoutes(processorConfig)) {
        qDebug() << QStringLiteral("[AppController] 反向流路由使用内置默认值");
        registerDefaultRoutes();
    }

    m_reverseTargetDevice = static_cast<uint16_t>(processorConfig.value(QStringLiteral("reverseTargetDevice"))
                                                      .toInt(DEVICE_RADAR_NH_KU_1));
    const uint32_t subscriptionId = m_busRouter->subscribe(m_reverseTargetDevice);
    if (subscriptionId == 0) {
        qWarning() << QStringLiteral("[AppController] BusToCommRouter 订阅 SystemBus 失败");
    }

    qDebug() << QStringLiteral("[AppController] BusToCommRouter 已创建并订阅 SystemBus 反向流");
}

/**
 * @brief 读取完整默认端点/目标对；任一字段缺失时整体回退 radar_cpu/workmode。
 */
BusToCommRouter::Route AppController::resolveDefaultRoute(const QJsonObject& processorConfig) const
{
    const QJsonObject defaultRouteObj = processorConfig.value(QStringLiteral("defaultRoute")).toObject();
    const QString endpointId = defaultRouteObj.value(QStringLiteral("endpointId")).toString();
    const QString targetName = defaultRouteObj.value(QStringLiteral("targetName")).toString();
    if (!endpointId.isEmpty() && !targetName.isEmpty()) {
        return { endpointId, targetName };
    }

    return { QStringLiteral("radar_cpu"), QStringLiteral("workmode") };
}

/**
 * @brief 登记配置中的 payloadType 路由，缺失类型、端点或目标名的条目被跳过。
 */
bool AppController::registerConfiguredRoutes(const QJsonObject& processorConfig)
{
    bool hasRouteConfig = false;
    const QJsonArray routes = processorConfig.value(QStringLiteral("routes")).toArray();
    for (const QJsonValue& value : routes) {
        const QJsonObject obj = value.toObject();
        const int payloadType = obj.value(QStringLiteral("payloadType")).toInt(-1);
        const QString endpointId = obj.value(QStringLiteral("endpointId")).toString();
        const QString targetName = obj.value(QStringLiteral("targetName")).toString();
        if (payloadType < 0 || endpointId.isEmpty() || targetName.isEmpty()) {
            continue;
        }

        m_busRouter->registerRoute(static_cast<uint16_t>(payloadType), { endpointId, targetName });
        hasRouteConfig = true;
    }

    return hasRouteConfig;
}

/**
 * @brief 为工作/待机和目标控制协议登记 radar_cpu/workmode 内置路由。
 */
void AppController::registerDefaultRoutes()
{
    m_busRouter->registerRoute(PAYLOAD_TYPE_WORK_STANDBY,
                               { QStringLiteral("radar_cpu"), QStringLiteral("workmode") });
    m_busRouter->registerRoute(PAYLOAD_TYPE_TARGET_TYPE,
                               { QStringLiteral("radar_cpu"), QStringLiteral("workmode") });
}

/**
 * @brief 创建 DPH 定时发送器并应用 enabled、周期和端点目标配置，默认周期 50 ms。
 */
void AppController::configureDphPublisher(const QJsonObject& processorConfig)
{
    m_dphPublisher = std::make_unique<DphPublisher>(m_commManager.get(), this);
    const QJsonObject dphPublisherConfig = processorConfig.value(QStringLiteral("dphPublisher")).toObject();
    m_dphEnabled = dphPublisherConfig.value(QStringLiteral("enabled")).toBool(true);
    const int dphIntervalMs = dphPublisherConfig.value(QStringLiteral("intervalMs")).toInt(50);
    const QString dphEndpointId = dphPublisherConfig.value(QStringLiteral("endpointId")).toString(QStringLiteral("radar_cpu"));
    const QString dphTargetName = dphPublisherConfig.value(QStringLiteral("targetName")).toString(QStringLiteral("dph"));

    m_dphPublisher->setRoute(dphEndpointId, dphTargetName);
    m_dphPublisher->setInterval(dphIntervalMs);
    if (!m_dphEnabled) {
        m_dphPublisher->stop();
    }

    qDebug() << QStringLiteral("[AppController] DphPublisher 已创建: %1/%2, interval=%3ms, enabled=%4")
                .arg(dphEndpointId, dphTargetName)
                .arg(dphIntervalMs)
                .arg(m_dphEnabled ? QStringLiteral("true") : QStringLiteral("false"));
}

/**
 * @brief 创建总线命令发送器和雷达模式状态机，并接通回显确认、拒绝和诊断信号。
 */
bool AppController::initializeBusinessLogic()
{
    // 工作模式命令发送器：直接发布到 SystemBus
    m_workModeSender = std::make_unique<BusWorkModeSender>();

    // 工作模式状态机
    m_workModeController = std::make_unique<RadarWorkModeController>(
        m_reverseTargetDevice, m_workModeSender.get(), this);
    m_workModeController->setTransitionTimeoutMs(300);

    // 状态变化 → 日志（未来 UI 阶段改为更新状态栏/指示灯）
    connect(m_workModeController.get(), &RadarWorkModeController::stateChanged,
            this, [](RadarWorkModeController::State oldState,
                     RadarWorkModeController::State newState) {
        // WS 已通过 Logger 回调推送，此处不重复输出
    });

    // 命令被拒绝 → 日志（UI 阶段可改为 toast/状态提示）
    connect(m_workModeController.get(), &RadarWorkModeController::requestRejected,
            this, [](RadarWorkModeController::RejectReason reason,
                     RadarWorkModeController::State current) {
        qWarning() << QStringLiteral("[AppController] 命令被拒绝: %1 (当前状态=%2)")
                       .arg(RadarWorkModeController::rejectReasonName(reason),
                            RadarWorkModeController::stateName(current));
    });

    // 命令成功下发
    connect(m_workModeController.get(), &RadarWorkModeController::workModeCommandIssued,
            this, [](uint16_t deviceId, uint32_t mode) {
        qDebug() << QStringLiteral("[AppController] 已下发 workmode=%1 到 device=%2")
                    .arg(mode).arg(deviceId);
    });

    // 数据流 → 状态机关联：雷达回显替代超时
    if (m_radarProcessor) {
        connect(m_radarProcessor.get(), &RadarDataProcessor::radarWorkModeEcho,
                m_workModeController.get(), &RadarWorkModeController::onWorkModeEcho);
        qDebug() << "[AppController] RadarDataProcessor ↔ RadarWorkModeController 已关联";
    }

    qDebug() << QStringLiteral("[AppController] 工作模式控制器已就绪, device=%1")
                .arg(m_reverseTargetDevice);
    return true;
}

/**
 * @brief 从总线、端点、处理器、存储、路由和数据库采样一次性健康快照。
 */
AppHealth AppController::healthReport() const
{
    AppHealth report;
    report.timestampMs = QDateTime::currentMSecsSinceEpoch();

    // SystemBus 状态
    const auto& bus = SystemBus::getInstance();
    report.systemBus.running = bus.isRunning();
    report.systemBus.subscriptionCount = bus.getSubscriptionCount();

    // 通信端点状态
    if (m_commManager) {
        report.comm.totalEndpoints = m_commManager->endpointCount();
        report.comm.openEndpoints = m_commManager->openEndpointCount();
        report.comm.endpointOpen = m_commManager->endpointOpenStates();
    }

    // 数据处理状态
    if (m_radarProcessor) {
        report.processing.generalPackageCount = m_radarProcessor->generalPackageCount();
    }
    if (m_trackStorage) {
        report.processing.trackInsertCount = m_trackStorage->stats().persisted;
    }
    if (m_busRouter) {
        report.processing.routedCount = m_busRouter->routedCount();
    }

    // 数据库状态
    report.database.configured = (m_dataStore != nullptr);
    report.database.connected = m_dataStore && m_dataStore->isConnected();

    // 延时统计
    if (m_radarProcessor) {
        report.uplinkLatency = m_radarProcessor->uplinkLatency();
    }
    if (m_busRouter) {
        report.downlinkLatency = m_busRouter->downlinkLatency();
    }

    return report;
}

/**
 * @brief 将端点错误关联到设备和活动任务，同时写 system_events 并发布结构化总线错误。
 */
void AppController::onCommError(const QString& endpointId, const QString& message)
{
    QString deviceId;
    if (m_deviceRegistry)
        deviceId = m_deviceRegistry->cachedDeviceIdForEndpoint(endpointId);

    QString taskId;
    if (m_taskService && !deviceId.isEmpty())
        taskId = m_taskService->resolveTaskIdForDevice(deviceId);

    LogContext::Scope logScope(deviceId, taskId);
    qWarning() << QStringLiteral("[AppController] 通信错误: 端点='%1' - %2")
                  .arg(endpointId, message);

    insertSystemEvent(QStringLiteral("error"), endpointId, message, deviceId, taskId);

    // 将错误发布到 SystemBus
    DataPacket errorPacket = createErrorPacket(
        ERR_LEVEL_ERROR, ERR_NETWORK,
        SOURCE_COMMUNICATION, message);
    if (!SystemBus::getInstance().publish(errorPacket)) {
        qWarning() << QStringLiteral("[AppController] 通信错误事件发布到 SystemBus 失败");
    }
}

void AppController::insertSystemEvent(const QString& category,
                                      const QString& source,
                                      const QString& detail,
                                      const QString& deviceId,
                                      const QString& taskId)
{
    if (!m_dataStore || !m_dataStore->isConnected()) {
        return;
    }

    QVariantMap row;
    row.insert(QStringLiteral("ts_ms"), QDateTime::currentMSecsSinceEpoch());
    row.insert(QStringLiteral("category"), category);
    row.insert(QStringLiteral("source"), source);
    row.insert(QStringLiteral("detail"), detail);
    if (!taskId.isEmpty())
        row.insert(QStringLiteral("task_id"), taskId);
    if (!deviceId.isEmpty())
        row.insert(QStringLiteral("device_id"), deviceId);
    if (!m_dataStore->insert(QStringLiteral("system_events"), row)) {
        qWarning() << QStringLiteral("[AppController] 写入 system_events 失败: %1/%2")
                      .arg(category, source);
    }
}

// ════════════════════════════════════════════════════════════════
// 通信端点配置
// ════════════════════════════════════════════════════════════════

/**
 * @brief 从 ConfigManager 缓存读取完整端点配置根对象。
 */
QJsonObject AppController::endpointsConfigJson()
{
    return m_configManager.getConfig(QStringLiteral("endpoints"));
}

/**
 * @brief 校验设备绑定后热重载完整端点集合，再落盘；任一步失败尝试恢复旧运行时配置。
 */
bool AppController::updateEndpointsConfig(const QJsonObject& root, QString* errorOut)
{
    auto setError = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
        qWarning().noquote() << QStringLiteral("[AppController] 端点配置更新失败: %1").arg(msg);
        return false;
    };

    const QStringList validationErrors = ConfigManager::validateConfig(QStringLiteral("endpoints"), root);
    if (!validationErrors.isEmpty())
        return setError(validationErrors.join(QStringLiteral("; ")));

    QSet<QString> newIds;
    for (const auto& val : root[QStringLiteral("endpoints")].toArray()) {
        const QString id = val.toObject()[QStringLiteral("id")].toString();
        if (id.isEmpty())
            return setError(QStringLiteral("端点 id 不能为空"));
        if (newIds.contains(id))
            return setError(QStringLiteral("端点 id 重复: %1").arg(id));
        newIds.insert(id);
    }

    if (m_deviceRegistry) {
        for (const auto& d : m_deviceRegistry->list()) {
            if (!d.endpointId.isEmpty() && !newIds.contains(d.endpointId))
                return setError(QStringLiteral("端点 %1 仍被设备 %2 绑定，无法删除")
                                    .arg(d.endpointId, d.id));
        }
    }

    const QVector<EndpointConfig> configs = EndpointConfigLoader::parseFromJsonObject(root);
    if (configs.isEmpty())
        return setError(QStringLiteral("至少需要一个有效端点"));

    if (!m_commManager)
        return setError(QStringLiteral("通信管理器未初始化"));

    const QJsonObject previousRoot = endpointsConfigJson();
    const QVector<EndpointConfig> previousConfigs =
        EndpointConfigLoader::parseFromJsonObject(previousRoot);
    auto rollbackRuntime = [this, &previousConfigs]() {
        return !previousConfigs.isEmpty()
            && m_commManager->reloadEndpoints(previousConfigs, m_running);
    };

    if (!m_commManager->reloadEndpoints(configs, m_running)) {
        const bool restored = rollbackRuntime();
        return setError(restored
            ? QStringLiteral("通信端点热重载失败，已恢复原配置")
            : QStringLiteral("通信端点热重载失败，且原配置恢复失败"));
    }

    if (!m_configManager.saveConfig(QStringLiteral("endpoints"), root)) {
        const bool restored = rollbackRuntime();
        return setError(restored
            ? QStringLiteral("写入 endpoints.json 失败，运行时已恢复原配置")
            : QStringLiteral("写入 endpoints.json 失败，且运行时原配置恢复失败"));
    }

    qInfo().noquote() << QStringLiteral("[AppController] 端点配置已保存并重载，共 %1 个端点").arg(configs.size());
    return true;
}

/**
 * @brief 合并单端点请求为完整配置事务，强制路径 ID 与正文 ID 一致。
 */
bool AppController::updateEndpointConfig(const QString& id, const QJsonObject& endpointObj, QString* errorOut)
{
    auto setError = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
        qWarning().noquote() << QStringLiteral("[AppController] 单端点更新失败: %1").arg(msg);
        return false;
    };

    if (id.isEmpty())
        return setError(QStringLiteral("端点 id 不能为空"));

    const QString bodyId = endpointObj[QStringLiteral("id")].toString();
    if (!bodyId.isEmpty() && bodyId != id)
        return setError(QStringLiteral("路径 id 与 body id 不一致"));

    QJsonObject mergedEndpoint = endpointObj;
    mergedEndpoint[QStringLiteral("id")] = id;

    QJsonObject current = endpointsConfigJson();
    QJsonArray endpoints = current[QStringLiteral("endpoints")].toArray();
    QJsonArray updated;
    bool found = false;
    for (const auto& val : endpoints) {
        QJsonObject obj = val.toObject();
        if (obj[QStringLiteral("id")].toString() == id) {
            updated.append(mergedEndpoint);
            found = true;
        } else {
            updated.append(obj);
        }
    }
    if (!found)
        updated.append(mergedEndpoint);

    QJsonObject root;
    root[QStringLiteral("endpoints")] = updated;
    return updateEndpointsConfig(root, errorOut);
}

// ════════════════════════════════════════════════════════════════
// Web 服务 (组合根持有 web::WebService)
// ════════════════════════════════════════════════════════════════

/**
 * @brief 启动 HTTP/WS，注册业务路由和状态推送，再接通运行参数桥及显示快照/增量链路。
 */
WebServiceResult AppController::startWebService(quint16 httpPort, quint16 wsPort)
{
    m_webService = std::make_unique<web::WebService>(this);
    auto result = m_webService->start(httpPort, wsPort);
    if (!result.ok) return {};

    m_webApi = std::make_unique<WebApiController>(
        m_webService.get(), m_workModeController.get(),
        m_deviceRegistry.get(), m_runtimeSettings.get(),
        [dph = m_dphPublisher.get()]() -> DphStatus {
            DphStatus st;
            if (dph) { st.currentSceneNumber = dph->sceneNumber(); st.sendCount = dph->sendCount(); }
            return st;
        },
        [this]() { return healthReport(); },
        [this]() { return m_pipelineLatency.snapshot(); },
        m_taskService.get(),
        this);
    m_webApi->registerRestRoutes();
    m_webApi->connectWebPush();

    if (m_taskService)
        TaskApiRoutes::registerRoutes(m_webService.get(), m_taskService.get());

    EndpointApiRoutes::registerRoutes(m_webService.get(), this);

    if (m_dataStore && m_dataStore->isConnected()) {
        SystemEventApiRoutes::registerRoutes(m_webService.get(), m_dataStore.get());
        TrackHistoryApiRoutes::registerRoutes(m_webService.get(), m_dataStore.get());
    }

    // SystemBus → DphPublisher bridge (RuntimeSettings 事件，防止重复订阅)
    if (m_runtimeSettingsSubId == 0 && m_runtimeSettings && m_dphPublisher) {
        m_runtimeSettingsSubId = SystemBus::getInstance().subscribeBySource(SOURCE_RUNTIME_SETTINGS,
            [this](const DataPacket* pkt) {
                if (!pkt || pkt->command != DataCommand::DphParamsChanged) return;
                RadarRuntimeSettings s;
                if (!unpackDataFromPayload(*pkt, s)) return;
                QMetaObject::invokeMethod(this, [this, s]() {
                    m_dphPublisher->applyRuntimeSettings(s);
                }, Qt::QueuedConnection);
            });
        if (m_runtimeSettingsSubId == 0) {
            qWarning() << "[AppController] RuntimeSettings → DphPublisher bridge 订阅失败";
        } else {
            qDebug() << "[AppController] RuntimeSettings → DphPublisher bridge 已就绪";
        }
    }

    // TrackDisplayUpdate 总线事件 → WebSocket；新连接由 bridge 单独补发当前快照。
    if (m_webService) {
        m_trackDisplayWebBridge = std::make_unique<TrackDisplayWebBridge>(m_webService.get(), this);
        m_trackDisplayWebBridge->setLatencyMonitor(&m_pipelineLatency);
        if (m_trackDisplayManager) {
            connect(m_trackDisplayWebBridge.get(), &TrackDisplayWebBridge::resyncRequired,
                    m_trackDisplayManager.get(), &TrackDisplayManager::publishReset,
                    Qt::QueuedConnection);
        }
        m_trackDisplayWebBridge->start();
    }
    if (m_trackDisplayManager) {
        m_trackDisplayManager->start();
        m_trackDisplayManager->publishReset();
        qDebug() << "[AppController] TrackDisplayManager 初始快照 + 定时增量 → SystemBus 已就绪";
    }

    qInfo() << "[AppController] Web 服务已启动";
    return {true, httpPort, wsPort};
}
