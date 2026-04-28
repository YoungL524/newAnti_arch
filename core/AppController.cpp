#include "AppController.h"
#include "EndpointConfigLoader.h"
#include "GenericPackets.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QDateTime>
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

bool AppController::sendWorkModeCommand(uint32_t mode)
{
    if (!m_busRouter) {
        qWarning() << QStringLiteral("[AppController] 反向流路由器未初始化，无法发送 workmode");
        return false;
    }

    workStandby cmd;
    memset(&cmd, 0, sizeof(workStandby));
    cmd.Work_Standby_Mode = mode;
    // 本机位置占位值（真实部署由配置/界面提供）
    cmd.Lon_Coordinates = 116.3974;
    cmd.Lat_Coordinates = 39.9093;
    cmd.Altitude = 100.0;

    DataPacket packet;
    packet.sourceType = UI_CONFIGWIDGET;
    packet.dispatchTarget = m_reverseTargetDevice;
    packet.packetSequence = static_cast<uint32_t>(SystemBus::getInstance().getPublishedCount() + 1);
    packet.timeStamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());

    if (!packDataToPayload(packet, cmd)) {
        qWarning() << QStringLiteral("[AppController] workStandby 打包失败，无法发布到总线");
        return false;
    }

    const bool ok = SystemBus::getInstance().publish(std::move(packet));

    qDebug() << QStringLiteral("[AppController] 已发布 workmode=%1 (%2) 到 SystemBus, %3 字节, %4")
                .arg(mode)
                .arg(mode == 1 ? QStringLiteral("工作") : QStringLiteral("待机"))
                .arg(sizeof(workStandby))
                .arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"));
    return ok;
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
