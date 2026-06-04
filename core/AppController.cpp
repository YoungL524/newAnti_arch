#include "AppController.h"
#include "HttpServer.h"
#include "EndpointConfigLoader.h"
#include "GenericPackets.h"
#include "BusWorkModeSender.h"
#include "RadarWorkModeController.h"
#include "log/Logger.h"
#include <QTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QUrl>
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
// Web 服务 (HttpServer + REST API + WS 定时器)
// ════════════════════════════════════════════════════════════════

void AppController::setHttpServer(HttpServer* server)
{
    m_httpServer = server;
}

void AppController::registerApiRoutes()
{
    if (!m_httpServer) return;

    auto wmc = m_workModeController.get();

    // GET /api/status
    m_httpServer->handleGet(QStringLiteral("/api/status"), [this, wmc](const QJsonObject &) {
        QJsonObject s;
        auto health = healthReport();
        s[QStringLiteral("ok")] = true;
        s[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        s[QStringLiteral("uptime")] = QDateTime::currentSecsSinceEpoch();
        s[QStringLiteral("endpoints")] = health.comm.openEndpoints;
        s[QStringLiteral("dbConnected")] = health.database.connected;

        QJsonArray devices;
        if (wmc) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
            dev[QStringLiteral("state")] = RadarWorkModeController::stateName(wmc->state());
            auto st = wmc->state();
            dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                               || st == RadarWorkModeController::State::Working);
            devices.append(dev);
        }
        s[QStringLiteral("devices")] = devices;
        auto st2 = wmc ? wmc->state() : RadarWorkModeController::State::Standby;
        s[QStringLiteral("acquisition")] = (st2 == RadarWorkModeController::State::Starting
                                         || st2 == RadarWorkModeController::State::Working);
        s[QStringLiteral("state")] = wmc ? RadarWorkModeController::stateName(wmc->state()) : QStringLiteral("Unknown");
        return s;
    });

    // GET /api/logs
    m_httpServer->handleGet(QStringLiteral("/api/logs"), [](const QJsonObject &) {
        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        QJsonArray arr;
        auto recent = Logger::instance().recentLogs(100);
        for (const auto& p : recent) {
            QJsonObject item;
            item[QStringLiteral("level")] = p.first;
            item[QStringLiteral("message")] = p.second;
            item[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
            arr.append(item);
        }
        r[QStringLiteral("logs")] = arr;
        return r;
    });

    if (wmc) {
        // POST /api/start
        m_httpServer->handlePost(QStringLiteral("/api/start"), [wmc](const QJsonObject &body) {
            static qint64 lastCmdMs = 0;
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastCmdMs < 500) { QJsonObject r; r[QStringLiteral("ok")] = true; return r; }
            lastCmdMs = now;
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            bool shouldStart = ids.isEmpty();
            for (const auto &v : ids) {
                if (v.toString() == QStringLiteral("radar_cpu")) { shouldStart = true; break; }
            }
            if (shouldStart) wmc->requestWork();
            QJsonObject r; r[QStringLiteral("ok")] = true; return r;
        });

        // POST /api/stop
        m_httpServer->handlePost(QStringLiteral("/api/stop"), [wmc](const QJsonObject &body) {
            static qint64 lastCmdMs = 0;
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastCmdMs < 500) { QJsonObject r; r[QStringLiteral("ok")] = true; return r; }
            lastCmdMs = now;
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            bool shouldStop = ids.isEmpty();
            for (const auto &v : ids) {
                if (v.toString() == QStringLiteral("radar_cpu")) { shouldStop = true; break; }
            }
            if (shouldStop) wmc->requestStandby();
            QJsonObject r; r[QStringLiteral("ok")] = true; return r;
        });
    }
}

void AppController::startWebService()
{
    if (!m_httpServer) return;

    // 注册路由
    registerApiRoutes();

    // 输出 HTTP server 日志
    QObject::connect(m_httpServer, &HttpServer::serverLog,
                     [](const QString &level, const QString &msg) {
        if (level == QStringLiteral("error"))
            qWarning().noquote() << QStringLiteral("[HTTP]") << msg;
        else
            qDebug().noquote() << QStringLiteral("[HTTP]") << msg;
    });

    // 订阅 SystemBus → WebSocket 推送航迹
    m_httpServer->subscribeToBus();

    // Logger 回调 → WS 实时日志
    Logger::instance().setExternalCallback([this](const QString& level, const QString& msg) {
        if (level == QStringLiteral("DEBUG")) return;
        QJsonObject m;
        m[QStringLiteral("type")] = QStringLiteral("log");
        m[QStringLiteral("level")] = level;
        m[QStringLiteral("message")] = msg;
        m[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
        QMetaObject::invokeMethod(m_httpServer, [this, m]() {
            m_httpServer->broadcast(m);
        }, Qt::QueuedConnection);
    });

    // WS 定时推送状态
    auto wmc = m_workModeController.get();
    m_statusTimer = new QTimer(this);
    QObject::connect(m_statusTimer, &QTimer::timeout, [this, wmc]() {
        if (!m_httpServer) return;
        auto health = healthReport();
        QJsonObject msg;
        msg[QStringLiteral("type")] = QStringLiteral("status");
        msg[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        msg[QStringLiteral("upTime")] = health.timestampMs;

        QJsonArray devices;
        if (wmc) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
            dev[QStringLiteral("state")] = RadarWorkModeController::stateName(wmc->state());
            auto st = wmc->state();
            dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                               || st == RadarWorkModeController::State::Working);
            devices.append(dev);
        }
        msg[QStringLiteral("devices")] = devices;
        auto st3 = wmc ? wmc->state() : RadarWorkModeController::State::Standby;
        msg[QStringLiteral("acquisition")] = (st3 == RadarWorkModeController::State::Starting
                                           || st3 == RadarWorkModeController::State::Working);
        msg[QStringLiteral("state")] = wmc ? RadarWorkModeController::stateName(wmc->state()) : QStringLiteral("Unknown");

        m_httpServer->broadcast(msg);
    });
    m_statusTimer->start(1000);

    qInfo() << "[AppController] Web 服务已启动";
}
