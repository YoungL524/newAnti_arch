#include "core/config/DeviceConfigService.h"

#include "core/archive/SqliteArchiveRepository.h"
#include "core/persistence/SqliteRuntimeRepository.h"
#include "core/model/StableIdRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QVector>

#include <cmath>
#include <limits>

namespace core::config {

/**
 * @brief 只保存两个同线程持久化依赖，不加载配置或分配运行时句柄。
 */
DeviceConfigService::DeviceConfigService(
    core::persistence::SqliteRuntimeRepository &runtimeRepository,
    core::archive::SqliteArchiveRepository &archiveRepository,
    core::model::StableIdRegistry &stableIds)
    : m_runtimeRepository(runtimeRepository)
    , m_archiveRepository(archiveRepository)
    , m_stableIds(stableIds)
{
}

/**
 * @brief 先完整读取并验证全部最新 revision，再依次分配句柄和登记归档映射。
 * @details 数据库是唯一真源，任何一条损坏配置都会中止启动，而不是静默退回 JSON/INI 或跳过设备。
 */
bool DeviceConfigService::loadFromDatabase()
{
    if (!m_activeDevices.isEmpty() || !m_latestDeviceIds.isEmpty()) {
        m_lastError = QStringLiteral("设备配置已经加载，禁止重复重建运行时句柄");
        return false;
    }
    QVector<DeviceConfiguration> configurations;
    QVector<qint64> revisionIds;
    if (!m_runtimeRepository.loadCurrentDevices(configurations, revisionIds)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    if (configurations.size() != revisionIds.size()) {
        m_lastError = QStringLiteral("设备配置与 revision 主键数量不一致");
        return false;
    }
    QVector<QString> endpointKeys;
    QVector<quint8> enabledFaceMasks;
    endpointKeys.reserve(configurations.size());
    enabledFaceMasks.reserve(configurations.size());
    for (const DeviceConfiguration &configuration : configurations) {
        QString endpointKey;
        quint8 enabledFaceMask = 0;
        if (!validateConfiguration(configuration, endpointKey, enabledFaceMask)) {
            return false;
        }
        endpointKeys.push_back(endpointKey);
        enabledFaceMasks.push_back(enabledFaceMask);
    }

    for (qsizetype index = 0; index < configurations.size(); ++index) {
        const auto deviceId = allocateDeviceId();
        const auto endpointId = allocateEndpointId();
        if (!deviceId.isSet() || !endpointId.isSet()) {
            return false;
        }
        if (!m_archiveRepository.bindDevice(deviceId, revisionIds[index])
            || !m_archiveRepository.bindEndpoint(endpointId, endpointKeys[index])
            || !m_stableIds.registerDevice(deviceId, configurations[index].deviceUuid)) {
            m_lastError = m_archiveRepository.lastError();
            if (m_lastError.isEmpty()) m_lastError = QStringLiteral("稳定设备身份登记冲突");
            return false;
        }
        const DeviceConfiguration &configuration = configurations[index];
        m_activeDevices.insert(
            deviceId.value(),
            ActiveDevice {revisionIds[index], endpointId, endpointKeys[index],
                          configuration.protocolAdapterKey, configuration.arrayType,
                          enabledFaceMasks[index]});
        m_latestDeviceIds.insert(configuration.deviceUuid, deviceId);
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 在写数据库前完成通用字段校验，提交成功后才分配并公开新的运行时句柄。
 * @details 新 revision 永远获得新 DeviceId/EndpointId，使已经发布的旧会话身份不会被配置热替换污染。
 */
bool DeviceConfigService::saveAndActivate(
    DeviceConfiguration &configuration,
    core::model::DeviceId &deviceId,
    core::model::EndpointId &trackEndpointId)
{
    deviceId = core::model::DeviceId();
    trackEndpointId = core::model::EndpointId();
    QString endpointKey;
    quint8 enabledFaceMask = 0;
    if (!validateConfiguration(configuration, endpointKey, enabledFaceMask)) {
        return false;
    }
    qint64 revisionId = 0;
    if (!m_runtimeRepository.saveDeviceRevision(configuration, revisionId)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    const auto allocatedDeviceId = allocateDeviceId();
    const auto allocatedEndpointId = allocateEndpointId();
    if (!allocatedDeviceId.isSet() || !allocatedEndpointId.isSet()) {
        return false;
    }
    if (!m_archiveRepository.bindDevice(allocatedDeviceId, revisionId)
        || !m_archiveRepository.bindEndpoint(allocatedEndpointId, endpointKey)
        || !m_stableIds.registerDevice(allocatedDeviceId, configuration.deviceUuid)) {
        m_lastError = m_archiveRepository.lastError();
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("稳定设备身份登记冲突");
        return false;
    }
    m_activeDevices.insert(
        allocatedDeviceId.value(),
        ActiveDevice {revisionId, allocatedEndpointId, endpointKey,
                      configuration.protocolAdapterKey, configuration.arrayType,
                      enabledFaceMask});
    m_latestDeviceIds.insert(configuration.deviceUuid, allocatedDeviceId);
    deviceId = allocatedDeviceId;
    trackEndpointId = allocatedEndpointId;
    m_lastError.clear();
    return true;
}

/**
 * @brief 通过同一SQLite适配器读取当前台账，并复用保存入口的完整配置校验防止输出损坏数据。
 */
bool DeviceConfigService::listCurrent(QVector<DeviceConfiguration> &configurations)
{
    QVector<DeviceConfiguration> loaded;
    QVector<qint64> revisionIds;
    if (!m_runtimeRepository.loadCurrentDevices(loaded, revisionIds)
        || loaded.size() != revisionIds.size()) {
        m_lastError = m_runtimeRepository.lastError();
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("设备配置与revision主键数量不一致");
        return false;
    }
    for (const DeviceConfiguration &configuration : loaded) {
        QString endpointKey;
        quint8 enabledFaceMask = 0;
        if (!validateConfiguration(configuration, endpointKey, enabledFaceMask)) return false;
    }
    configurations = std::move(loaded);
    m_lastError.clear();
    return true;
}

/**
 * @brief 提交删除revision后只撤销UUID到最新可选句柄的映射，不破坏旧事实所需的稳定身份解析。
 */
bool DeviceConfigService::softDelete(
    const QUuid &deviceUuid, quint32 expectedRevision,
    const QString &changedBy, const QString &changeReason)
{
    if (!m_runtimeRepository.softDeleteDevice(
            deviceUuid, expectedRevision, changedBy, changeReason)) {
        m_lastError = m_runtimeRepository.lastError();
        return false;
    }
    m_latestDeviceIds.remove(deviceUuid);
    m_lastError.clear();
    return true;
}

/**
 * @brief 先通过稳定 UUID 找到最新 DeviceId，再从 ActiveDevice 取出同 revision 的 EndpointId。
 */
bool DeviceConfigService::resolveLatestDevice(
    const QUuid &deviceUuid,
    core::model::DeviceId &deviceId,
    core::model::EndpointId &trackEndpointId) const
{
    deviceId = core::model::DeviceId();
    trackEndpointId = core::model::EndpointId();
    const auto latest = m_latestDeviceIds.constFind(deviceUuid);
    if (deviceUuid.isNull() || latest == m_latestDeviceIds.cend()) {
        return false;
    }
    const auto active = m_activeDevices.constFind(latest->value());
    if (active == m_activeDevices.cend()) {
        return false;
    }
    deviceId = *latest;
    trackEndpointId = active->trackEndpointId;
    return true;
}

/**
 * @brief 从不可变 ActiveDevice 映射读取数据库 revision，不访问 SQLite。
 */
bool DeviceConfigService::resolveDeviceRevision(
    core::model::DeviceId deviceId,
    qint64 &deviceRevisionId) const
{
    deviceRevisionId = 0;
    const auto found = m_activeDevices.constFind(deviceId.value());
    if (!deviceId.isSet() || found == m_activeDevices.cend()) {
        return false;
    }
    deviceRevisionId = found->deviceRevisionId;
    return true;
}

/**
 * @brief 只有同一 ActiveDevice 中冻结的端点句柄匹配时才返回稳定 endpointKey。
 */
bool DeviceConfigService::resolveTrackEndpoint(
    core::model::DeviceId deviceId,
    core::model::EndpointId endpointId,
    QString &endpointKey) const
{
    endpointKey.clear();
    const auto found = m_activeDevices.constFind(deviceId.value());
    if (!deviceId.isSet() || !endpointId.isSet() || found == m_activeDevices.cend()
        || found->trackEndpointId != endpointId) {
        return false;
    }
    endpointKey = found->trackEndpointKey;
    return true;
}

/**
 * @brief 在设备/端点身份成立后检查协议键非空，阻止“只有架构占位配置”的设备开始收数。
 */
bool DeviceConfigService::isCollectionReady(
    core::model::DeviceId deviceId,
    core::model::EndpointId endpointId) const
{
    const auto found = m_activeDevices.constFind(deviceId.value());
    return deviceId.isSet() && endpointId.isSet() && found != m_activeDevices.cend()
        && found->trackEndpointId == endpointId
        && !found->protocolAdapterKey.trimmed().isEmpty();
}

/**
 * @brief 从最新UUID映射读取冻结能力，不访问SQLite，也不允许任务选择已禁用阵面。
 */
bool DeviceConfigService::acceptsTaskBinding(
    const QUuid &deviceUuid,
    RadarArrayType arrayType,
    quint8 faceMask) const
{
    const auto latest = m_latestDeviceIds.constFind(deviceUuid);
    if (deviceUuid.isNull() || faceMask == 0 || latest == m_latestDeviceIds.cend()) return false;
    const auto active = m_activeDevices.constFind(latest->value());
    return active != m_activeDevices.cend() && active->arrayType == arrayType
        && (faceMask & static_cast<quint8>(~active->enabledFaceMask)) == 0;
}

/**
 * @brief 返回最近失败文本的值副本，成功调用后为空。
 */
QString DeviceConfigService::lastError() const
{
    return m_lastError;
}

/**
 * @brief 校验整机类型、阵面安装参数、姿态和端点，并要求唯一 track_receive UDP 端点。
 * @details 主链只接受单面阵或完整四面阵；厂家私有 protocolConfig 仍只要求是 JSON object。
 */
bool DeviceConfigService::validateConfiguration(
    const DeviceConfiguration &configuration,
    QString &trackEndpointKey,
    quint8 &enabledFaceMask)
{
    trackEndpointKey.clear();
    enabledFaceMask = 0;
    const bool finitePose = std::isfinite(configuration.longitudeDeg)
        && std::isfinite(configuration.latitudeDeg) && std::isfinite(configuration.altitudeM)
        && std::isfinite(configuration.absoluteHeadingDeg)
        && std::isfinite(configuration.pitchDeg) && std::isfinite(configuration.rollDeg);
    if (configuration.displayName.trimmed().isEmpty()
        || configuration.manufacturer.trimmed().isEmpty()
        || configuration.model.trimmed().isEmpty()
        || !finitePose || !std::isfinite(configuration.maxRangeM)
        || configuration.maxRangeM <= 0.0
        || configuration.longitudeDeg < -180.0 || configuration.longitudeDeg > 180.0
        || configuration.latitudeDeg < -90.0 || configuration.latitudeDeg > 90.0
        || configuration.absoluteHeadingDeg < 0.0
        || configuration.absoluteHeadingDeg >= 360.0
        || configuration.pitchDeg < -90.0 || configuration.pitchDeg > 90.0
        || configuration.rollDeg < -180.0 || configuration.rollDeg >= 180.0) {
        m_lastError = QStringLiteral("设备身份、作用距离或经纬高/绝对姿态不符合合同");
        return false;
    }

    QJsonParseError faceError;
    const QJsonDocument faceDocument = QJsonDocument::fromJson(
        configuration.facesJson.toUtf8(), &faceError);
    const int expectedFaces = configuration.arrayType == RadarArrayType::SingleFace ? 1
        : configuration.arrayType == RadarArrayType::FourFace ? 4 : 0;
    if (faceError.error != QJsonParseError::NoError || !faceDocument.isArray()
        || expectedFaces == 0 || faceDocument.array().size() != expectedFaces) {
        m_lastError = QStringLiteral("阵面配置必须与单面阵或四面阵整机类型严格一致");
        return false;
    }
    quint8 seenFaces = 0;
    for (const QJsonValue &value : faceDocument.array()) {
        if (!value.isObject()) {
            m_lastError = QStringLiteral("每个阵面配置必须是 JSON object");
            return false;
        }
        const QJsonObject face = value.toObject();
        const int faceIndex = face.value(QStringLiteral("faceIndex")).toInt(-1);
        const double boresight = face.value(QStringLiteral("boresightAzimuthDeg")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        const double scanHalfAngle = face.value(QStringLiteral("scanHalfAngleDeg")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        const bool valid = faceIndex >= 0 && faceIndex < expectedFaces
            && (seenFaces & (quint8(1U) << faceIndex)) == 0
            && std::isfinite(boresight) && boresight >= 0.0 && boresight < 360.0
            && std::isfinite(scanHalfAngle) && scanHalfAngle > 0.0 && scanHalfAngle <= 180.0
            && face.value(QStringLiteral("enabled")).isBool();
        if (!valid) {
            m_lastError = QStringLiteral("阵面编号、波束中心、扫描半角或启用状态无效");
            return false;
        }
        seenFaces = static_cast<quint8>(seenFaces | (quint8(1U) << faceIndex));
        if (face.value(QStringLiteral("enabled")).toBool()) {
            enabledFaceMask = static_cast<quint8>(enabledFaceMask | (quint8(1U) << faceIndex));
        }
    }
    const quint8 expectedMask = static_cast<quint8>((quint8(1U) << expectedFaces) - 1U);
    if (seenFaces != expectedMask) {
        m_lastError = QStringLiteral("阵面编号必须从0连续覆盖整机全部阵面");
        return false;
    }
    if (enabledFaceMask == 0) {
        m_lastError = QStringLiteral("设备至少需要启用一个阵面才能加入任务");
        return false;
    }

    QJsonParseError endpointError;
    const QJsonDocument endpointDocument = QJsonDocument::fromJson(
        configuration.endpointsJson.toUtf8(), &endpointError);
    if (endpointError.error != QJsonParseError::NoError || !endpointDocument.isArray()) {
        m_lastError = QStringLiteral("endpointsJson 必须是合法 JSON 数组");
        return false;
    }
    QSet<QString> keys;
    int trackReceiveCount = 0;
    for (const QJsonValue &value : endpointDocument.array()) {
        if (!value.isObject()) {
            m_lastError = QStringLiteral("每个 endpoint 必须是 JSON object");
            return false;
        }
        const QJsonObject endpoint = value.toObject();
        const QString key = endpoint.value(QStringLiteral("endpointKey")).toString().trimmed();
        const QString role = endpoint.value(QStringLiteral("role")).toString();
        const QString transport = endpoint.value(QStringLiteral("transport")).toString();
        const int localPort = endpoint.value(QStringLiteral("localPort")).toInt(-1);
        const int remotePort = endpoint.value(QStringLiteral("remotePort")).toInt(-1);
        if (key.isEmpty() || keys.contains(key) || transport != QStringLiteral("udp")
            || localPort < 1 || localPort > 65535 || remotePort < 0 || remotePort > 65535) {
            m_lastError = QStringLiteral("endpoint 键、UDP 传输或端口范围无效");
            return false;
        }
        keys.insert(key);
        if (role == QStringLiteral("track_receive")) {
            ++trackReceiveCount;
            trackEndpointKey = key;
        }
    }
    if (trackReceiveCount != 1) {
        m_lastError = QStringLiteral("第一版设备必须且只能配置一个 track_receive 端点");
        return false;
    }

    QJsonParseError protocolError;
    const QJsonDocument protocolDocument = QJsonDocument::fromJson(
        configuration.protocolConfigJson.toUtf8(), &protocolError);
    if (protocolError.error != QJsonParseError::NoError || !protocolDocument.isObject()) {
        m_lastError = QStringLiteral("protocolConfigJson 必须是合法 JSON object");
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 单调分配 DeviceId；达到 quint32 最大值后返回零并要求重启进程重建注册域。
 */
core::model::DeviceId DeviceConfigService::allocateDeviceId()
{
    if (m_nextDeviceId == 0) {
        m_lastError = QStringLiteral("DeviceId 运行时编号已经耗尽");
        return core::model::DeviceId();
    }
    const quint32 value = m_nextDeviceId;
    m_nextDeviceId = value == std::numeric_limits<quint32>::max() ? 0 : value + 1;
    return core::model::DeviceId::fromRaw(value);
}

/**
 * @brief 单调分配 EndpointId；不回收旧 revision 句柄，避免晚到 Sample 被解释成另一端点。
 */
core::model::EndpointId DeviceConfigService::allocateEndpointId()
{
    if (m_nextEndpointId == 0) {
        m_lastError = QStringLiteral("EndpointId 运行时编号已经耗尽");
        return core::model::EndpointId();
    }
    const quint32 value = m_nextEndpointId;
    m_nextEndpointId = value == std::numeric_limits<quint32>::max() ? 0 : value + 1;
    return core::model::EndpointId::fromRaw(value);
}

} // namespace core::config
