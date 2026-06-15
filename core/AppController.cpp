#include "AppController.h"
#include "web/WebService.h"
#include "web/WebApiController.h"
#include "business/DeviceRegistry.h"
#include "business/RuntimeSettingsService.h"
#include "business/TrackDisplayManager.h"
#include "EndpointConfigLoader.h"
#include "bus/SystemBus.h"
#include "GenericPackets.h"
#include "protocol/StructureReceive.h"
#include "BusWorkModeSender.h"
#include "RadarWorkModeController.h"
#include "log/Logger.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <cstring>

AppController::AppController(QObject* parent)
    : QObject(parent)
{
}

AppController::~AppController()
{
    stop();
}

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

    m_runtimeSettings = std::make_unique<RuntimeSettingsService>();
    qDebug() << "[AppController] 运行时参数服务已就绪";

    // 航迹显示管理器
    m_trackDisplayManager = std::make_unique<TrackDisplayManager>(this);
    wireTrackDisplayPipeline();

    qDebug() << QStringLiteral("[AppController] 初始化完成");
    return true;
}

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

    return true;
}

bool AppController::connectDatabaseFromConfig(QJsonObject dbConfig)
{
    const QString dbPath = dbConfig.value(QStringLiteral("database")).toString();
    if (!dbPath.isEmpty()) {
        dbConfig.insert(QStringLiteral("database"), resolveDatabasePath(dbPath));
    }

    return m_dataStore && m_dataStore->connectFromConfig(dbConfig);
}

QString AppController::resolveDatabasePath(const QString& dbPath) const
{
    if (dbPath.isEmpty() || QDir::isAbsolutePath(dbPath)) {
        return dbPath;
    }

    QDir projectRoot(m_configManager.configDir());
    projectRoot.cdUp();
    return projectRoot.absoluteFilePath(dbPath);
}

bool AppController::ensureDatabaseSchema()
{
    if (!m_dataStore || !m_dataStore->isConnected()) {
        return false;
    }

    const bool systemEventsOk = ensureSystemEventsSchema();
    const bool tracksOk = ensureTracksSchema();
    return systemEventsOk && tracksOk;
}

bool AppController::ensureSystemEventsSchema()
{
    static const QString kSystemEventsDdl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS system_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_ms INTEGER NOT NULL,"
        "  category TEXT NOT NULL,"
        "  source TEXT,"
        "  detail TEXT,"
        "  size INTEGER"
        ")"
    );

    bool ok = true;
    if (!m_dataStore->ensureTable(QStringLiteral("system_events"), kSystemEventsDdl)) {
        qWarning() << QStringLiteral("[AppController] system_events 表初始化失败");
        ok = false;
    }
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

    return ok;
}

bool AppController::ensureTracksSchema()
{
    static const QString kTracksDdl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_ms INTEGER NOT NULL,"
        "  source TEXT,"
        "  scene_num INTEGER,"
        "  target_id INTEGER,"
        "  range_m REAL,"
        "  azimuth_rad REAL,"
        "  elevation_rad REAL,"
        "  radial_speed REAL,"
        "  altitude_m REAL,"
        "  x REAL, y REAL, z REAL"
        ")"
    );

    bool ok = true;
    if (!m_dataStore->ensureTable(QStringLiteral("tracks"), kTracksDdl)) {
        qWarning() << QStringLiteral("[AppController] tracks 表初始化失败");
        ok = false;
    }
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

    return ok;
}

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
    qDebug() << QStringLiteral("[AppController] 所有服务已启动");
}

void AppController::stop()
{
    // Web API shutdown 独立于 m_running：即使 core 通信层未启动也要清理
    if (m_webApi) {
        m_webApi->shutdown();
        m_webApi.reset();
    }

    if (m_trackDisplayManager) {
        m_trackDisplayManager->stop();
        m_trackDisplayManager->clear();
    }

    if (!m_running) {
        return;
    }

    m_running = false;

    if (m_dphPublisher) {
        m_dphPublisher->stop();
    }

    if (m_busRouter) {
        m_busRouter->unsubscribe();
        qDebug() << QStringLiteral("[AppController] BusToCommRouter 已停止订阅");
    }

    // 停止通信层
    if (m_commManager) {
        m_commManager->stopAll();
    }

    // 取消运行时参数订阅
    if (m_runtimeSettingsSubId != 0) {
        SystemBus::getInstance().unsubscribe(m_runtimeSettingsSubId);
        m_runtimeSettingsSubId = 0;
    }

    // 关闭 SystemBus
    SystemBus::getInstance().shutdown();

    // 断开数据库连接
    if (m_dataStore) {
        m_dataStore->disconnect();
    }

    qDebug() << QStringLiteral("[AppController] 所有服务已停止");
}

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

void AppController::connectCoreSignals()
{
    const bool connected = connect(m_commManager.get(), &CommunicationManager::errorOccurred,
                                   this, &AppController::onCommError);
    if (!connected) {
        qWarning() << QStringLiteral("[AppController] CommunicationManager::errorOccurred 连接失败");
    }
}

void AppController::configureRadarProcessor(const QJsonObject& processorConfig)
{
    m_radarProcessor = std::make_unique<RadarDataProcessor>(
        m_commManager.get(), m_dataStore.get(), this);

    if (!registerConfiguredEndpointSources(processorConfig)) {
        qDebug() << QStringLiteral("[AppController] 端点源映射使用内置默认值");
        registerDefaultEndpointSources();
    }

    qDebug() << QStringLiteral("[AppController] RadarDataProcessor 已创建并订阅 Comm::dataReceived");
}

void AppController::wireTrackDisplayPipeline()
{
    if (!m_radarProcessor || !m_trackDisplayManager) {
        qWarning() << "[AppController] 航迹显示链路未接通: processor 或 TrackDisplayManager 不可用";
        return;
    }

    QObject::disconnect(m_radarProcessor.get(), &RadarDataProcessor::trackPointReady,
                        nullptr, nullptr);
    QObject::connect(m_radarProcessor.get(), &RadarDataProcessor::trackPointReady,
                     m_trackDisplayManager.get(), &TrackDisplayManager::addPoint);

    if (m_deviceRegistry)
        m_radarProcessor->setDeviceRegistry(m_deviceRegistry.get());
    else
        qWarning() << "[AppController] DeviceRegistry 不可用，TrackPoint 将无法映射 deviceId";

    qDebug() << "[AppController] RadarDataProcessor → TrackDisplayManager 航迹显示链路已接通";
}

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

void AppController::registerDefaultEndpointSources()
{
    m_radarProcessor->registerEndpointSource(QStringLiteral("radar_cpu"), DEVICE_RADAR_NH_KU_1);
}

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

void AppController::registerDefaultRoutes()
{
    m_busRouter->registerRoute(PAYLOAD_TYPE_WORK_STANDBY,
                               { QStringLiteral("radar_cpu"), QStringLiteral("workmode") });
    m_busRouter->registerRoute(PAYLOAD_TYPE_TARGET_TYPE,
                               { QStringLiteral("radar_cpu"), QStringLiteral("workmode") });
    m_busRouter->registerRoute(PAYLOAD_TYPE_GENERAL_PACKAGE,
                               { QStringLiteral("radar_cpu"), QStringLiteral("workmode") });
}

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
        // open 计数需遍历端点；目前 CommManager 无公开遍历接口，
        // 用 totalEndpoints 近似，未来可细化
        report.comm.openEndpoints = m_commManager->endpointCount();
    }

    // 数据处理状态
    if (m_radarProcessor) {
        report.processing.generalPackageCount = m_radarProcessor->generalPackageCount();
        report.processing.trackInsertCount = m_radarProcessor->trackInsertCount();
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

void AppController::onCommError(const QString& endpointId, const QString& message)
{
    qWarning() << QStringLiteral("[AppController] 通信错误: 端点='%1' - %2")
                  .arg(endpointId, message);

    insertSystemEvent(QStringLiteral("error"), endpointId, message);

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
                                      const QString& detail)
{
    if (!m_dataStore || !m_dataStore->isConnected()) {
        return;
    }

    QVariantMap row;
    row.insert(QStringLiteral("ts_ms"), QDateTime::currentMSecsSinceEpoch());
    row.insert(QStringLiteral("category"), category);
    row.insert(QStringLiteral("source"), source);
    row.insert(QStringLiteral("detail"), detail);
    if (!m_dataStore->insert(QStringLiteral("system_events"), row)) {
        qWarning() << QStringLiteral("[AppController] 写入 system_events 失败: %1/%2")
                      .arg(category, source);
    }
}

// ════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════
// Web 服务 (组合根持有 web::WebService)
// ════════════════════════════════════════════════════════════════

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
        [this]() { return healthReport(); }, m_trackDisplayManager.get(), this);
    m_webApi->registerRestRoutes();
    m_webApi->connectWebPush();

    // SystemBus → DphPublisher bridge (RuntimeSettings 事件，防止重复订阅)
    if (m_runtimeSettingsSubId == 0 && m_runtimeSettings && m_dphPublisher) {
        m_runtimeSettingsSubId = SystemBus::getInstance().subscribeBySource(SOURCE_RUNTIME_SETTINGS,
            [this](const DataPacket* pkt) {
                if (!pkt || pkt->payloadType != PAYLOAD_TYPE_DPH_PARAMS_CHANGED) return;
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

    // TrackDisplayManager snapshot → WebSocket 推送
    if (m_trackDisplayManager && m_webService) {
        auto* ws = m_webService.get();
        QObject::connect(m_trackDisplayManager.get(), &TrackDisplayManager::snapshotReady,
                         ws, [ws](const TrackDisplaySnapshot& snap) {
            QJsonObject msg;
            msg[QStringLiteral("type")] = QStringLiteral("tracks");
            msg[QStringLiteral("mode")] = QStringLiteral("snapshot");
            msg[QStringLiteral("timestamp")] = snap.timestamp;
            QJsonArray tracks;
            for (const auto& dp : snap.tracks) {
                QJsonObject t;
                t[QStringLiteral("trackId")]    = static_cast<int>(dp.trackId);
                t[QStringLiteral("deviceId")]   = dp.deviceId;
                t[QStringLiteral("timestamp")]  = dp.timestamp;
                t[QStringLiteral("range")]      = dp.range;
                t[QStringLiteral("azimuth")]    = dp.azimuth;
                t[QStringLiteral("elevation")]  = dp.elevation;
                t[QStringLiteral("radialSpeed")]= dp.radialSpeed;
                t[QStringLiteral("rcs")]        = dp.rcs;
                t[QStringLiteral("altitude")]   = dp.altitude;
                t[QStringLiteral("x")]          = dp.x;
                t[QStringLiteral("y")]          = dp.y;
                t[QStringLiteral("z")]          = dp.z;
                t[QStringLiteral("pointIndex")] = static_cast<int>(dp.pointIndex);
                QJsonObject style;
                style[QStringLiteral("color")]   = dp.style.color;
                style[QStringLiteral("size")]    = dp.style.size;
                style[QStringLiteral("opacity")] = dp.style.opacity;
                t[QStringLiteral("style")] = style;
                QJsonObject interaction;
                interaction[QStringLiteral("selected")]        = dp.interaction.selected;
                interaction[QStringLiteral("trackingEnabled")] = dp.interaction.trackingEnabled;
                t[QStringLiteral("interaction")] = interaction;
                tracks.append(t);
            }
            msg[QStringLiteral("tracks")] = tracks;
            ws->broadcast(msg);
        });
        m_trackDisplayManager->start();
        qDebug() << "[AppController] TrackDisplayManager → WS snapshot 推送已就绪";
    }

    qInfo() << "[AppController] Web 服务已启动";
    return {true, httpPort, wsPort};
}
