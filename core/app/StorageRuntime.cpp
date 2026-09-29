#include "core/app/StorageRuntime.h"
#include "core/app/ApplicationController.h"

#include "core/archive/SqliteArchiveRepository.h"
#include "core/archive/ArchiveReader.h"
#include "core/producer/SystemEventPublisher.h"
#include "core/config/DeviceConfigService.h"
#include "core/execution/ExecutionLane.h"
#include "core/model/StableIdRegistry.h"
#include "core/persistence/SqliteRuntimeRepository.h"
#include "core/task/TaskService.h"
#include "core/task/RadarControlSettingsJson.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QThread>

#include <chrono>
#include <limits>
#include <utility>

namespace core::app {

namespace {

/**
 * @brief 将设备配置转换为前端设备台账唯一JSON表示，不暴露进程内CompactId。
 */
QJsonObject deviceJson(const core::config::DeviceConfiguration &configuration)
{
    QJsonObject object;
    object.insert(QStringLiteral("deviceId"), configuration.deviceUuid.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("revision"), QString::number(configuration.revision));
    object.insert(QStringLiteral("displayName"), configuration.displayName);
    object.insert(QStringLiteral("manufacturer"), configuration.manufacturer);
    object.insert(QStringLiteral("model"), configuration.model);
    object.insert(QStringLiteral("protocolAdapterKey"), configuration.protocolAdapterKey);
    object.insert(QStringLiteral("arrayType"), configuration.arrayType == core::config::RadarArrayType::FourFace
        ? QStringLiteral("fourFace") : QStringLiteral("singleFace"));
    object.insert(QStringLiteral("maxRangeM"), configuration.maxRangeM);
    object.insert(QStringLiteral("longitudeDeg"), configuration.longitudeDeg);
    object.insert(QStringLiteral("latitudeDeg"), configuration.latitudeDeg);
    object.insert(QStringLiteral("altitudeM"), configuration.altitudeM);
    object.insert(QStringLiteral("faces"), QJsonDocument::fromJson(configuration.facesJson.toUtf8()).array());
    object.insert(QStringLiteral("endpoints"), QJsonDocument::fromJson(configuration.endpointsJson.toUtf8()).array());
    object.insert(QStringLiteral("protocolConfig"), QJsonDocument::fromJson(configuration.protocolConfigJson.toUtf8()).object());
    return object;
}

/**
 * @brief 把持久化完成/归档标记与当前 TaskPhase 合成为唯一的 V1 status 文本。
 * @param task 任务台账快照。
 * @param phase 该任务在当前进程的运行阶段；非活动任务传 None。
 * @return `Active`、`Ready`、`Faulted`、`Completed`、`Archived` 或 `Draft` 之一。
 * @note 运行阶段优先于持久化标记：采集进行中的任务必须显示 Active，不能被降级为 Ready。
 */
QString taskStatusText(const core::task::TaskDefinition &task, core::model::TaskPhase phase)
{
    switch (phase) {
    case core::model::TaskPhase::Running: return QStringLiteral("Active");
    case core::model::TaskPhase::Ready: return QStringLiteral("Ready");
    case core::model::TaskPhase::Faulted: return QStringLiteral("Faulted");
    case core::model::TaskPhase::Completed: return QStringLiteral("Completed");
    case core::model::TaskPhase::None: break;
    }
    if (task.archivedUtcMs > 0) return QStringLiteral("Archived");
    if (task.completedUtcMs > 0) return QStringLiteral("Completed");
    return QStringLiteral("Draft");
}

/**
 * @brief 将任务稳定身份、当前运行阶段和设备阵面绑定转换为前端V1 JSON。
 * @param task 任务台账快照。
 * @param phase 该任务在当前进程的 TaskPhase；非活动任务传 None，此时按持久化标记显示。
 */
QJsonObject taskJson(const core::task::TaskDefinition &task,
                     core::model::TaskPhase phase = core::model::TaskPhase::None)
{
    QJsonArray deviceIds;
    QJsonObject faceMasks;
    for (const auto &binding : task.deviceBindings) {
        const QString uuid = binding.deviceUuid.toString(QUuid::WithoutBraces);
        deviceIds.append(uuid);
        faceMasks.insert(uuid, static_cast<int>(binding.faceMask));
    }
    return QJsonObject {
        {QStringLiteral("taskId"), task.taskUuid.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), task.taskName},
        {QStringLiteral("description"), task.description},
        {QStringLiteral("operatorName"), task.operatorName},
        {QStringLiteral("status"), taskStatusText(task, phase)},
        {QStringLiteral("radarArrayType"),
         task.radarArrayType == core::config::RadarArrayType::FourFace
             ? QStringLiteral("fourFace") : QStringLiteral("singleFace")},
        {QStringLiteral("deviceIds"), deviceIds},
        {QStringLiteral("deviceFaceMasks"), faceMasks},
        {QStringLiteral("sourceTemplateId"), task.sourceTemplateUuid.toString(QUuid::WithoutBraces)},
        {QStringLiteral("controlSettings"), core::task::radarControlSettingsToJson(task.controlSettings)},
        {QStringLiteral("createdAt"), task.createdUtcMs},
        {QStringLiteral("completedAt"), task.completedUtcMs > 0
            ? QJsonValue(task.completedUtcMs) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("archivedAt"), task.archivedUtcMs > 0
            ? QJsonValue(task.archivedUtcMs) : QJsonValue(QJsonValue::Null)}
    };
}

/**
 * @brief 解析创建任务所需的最小字段；设备能力与位图合法性留给TaskService统一判定。
 */
bool parseTask(const QByteArray &body, core::task::TaskDefinition &task, QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("请求体必须是JSON object");
        return false;
    }
    const QJsonObject object = document.object();
    task.taskName = object.value(QStringLiteral("name")).toString();
    task.operatorName = object.value(QStringLiteral("operatorName")).toString();
    task.description = object.value(QStringLiteral("description")).toString();
    task.sourceTemplateUuid = QUuid(object.value(QStringLiteral("sourceTemplateId")).toString());
    if (task.sourceTemplateUuid.isNull()) {
        error = QStringLiteral("sourceTemplateId必须是有效模板UUID");
        return false;
    }
    const QString arrayType = object.value(QStringLiteral("radarArrayType")).toString();
    if (arrayType != QStringLiteral("singleFace") && arrayType != QStringLiteral("fourFace")) {
        error = QStringLiteral("radarArrayType必须是singleFace或fourFace");
        return false;
    }
    task.radarArrayType = arrayType == QStringLiteral("fourFace")
        ? core::config::RadarArrayType::FourFace : core::config::RadarArrayType::SingleFace;
    const QJsonArray deviceIds = object.value(QStringLiteral("deviceIds")).toArray();
    const QJsonObject masks = object.value(QStringLiteral("deviceFaceMasks")).toObject();
    for (const QJsonValue &value : deviceIds) {
        const QUuid uuid(value.toString());
        const int mask = masks.value(value.toString()).toInt(-1);
        if (!value.isString() || uuid.isNull() || mask < 1 || mask > 15) {
            error = QStringLiteral("deviceIds或deviceFaceMasks无效");
            return false;
        }
        task.deviceBindings.push_back(
            core::task::TaskDeviceBinding {uuid, static_cast<quint8>(mask)});
    }
    return true;
}

/**
 * @brief 生成 V1 成功响应，业务结果统一放入 data object。
 */
QByteArray successJson(const QJsonObject &data = {})
{
    return QJsonDocument(QJsonObject {{QStringLiteral("ok"), true},
                                      {QStringLiteral("data"), data}})
        .toJson(QJsonDocument::Compact);
}

/**
 * @brief 生成 V1 失败响应，code 供 HTTP 边界稳定映射状态码，message 仅供展示。
 */
QByteArray errorJson(const QString &code, const QString &message)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject {{QStringLiteral("code"), code},
                                                {QStringLiteral("message"), message}}}})
        .toJson(QJsonDocument::Compact);
}

/**
 * @brief 把现有 Owner 错误收敛为低频 CRUD 稳定码，不将文本解析职责泄漏给 Web 层。
 */
QByteArray ownerErrorJson(const QString &message)
{
    const QString code = message.contains(QStringLiteral("revision"))
        ? QStringLiteral("REVISION_CONFLICT") : QStringLiteral("BUSINESS_REJECTED");
    return errorJson(code, message);
}

/**
 * @brief 从HTTP JSON建立低频完整设备配置；缺失字段保持非法默认值并由DeviceConfigService统一拒绝。
 */
bool parseDevice(const QByteArray &body, core::config::DeviceConfiguration &configuration,
                 QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("请求体必须是JSON object");
        return false;
    }
    const QJsonObject object = document.object();
    bool revisionOk = false;
    const qulonglong expectedRevision = object.value(QStringLiteral("expectedRevision"))
        .toString().toULongLong(&revisionOk);
    configuration.revision = revisionOk && expectedRevision <= std::numeric_limits<quint32>::max()
        ? static_cast<quint32>(expectedRevision) : 0U;
    configuration.displayName = object.value(QStringLiteral("displayName")).toString();
    configuration.manufacturer = object.value(QStringLiteral("manufacturer")).toString();
    configuration.model = object.value(QStringLiteral("model")).toString();
    configuration.protocolAdapterKey = object.value(QStringLiteral("protocolAdapterKey")).toString();
    configuration.arrayType = object.value(QStringLiteral("arrayType")).toString() == QStringLiteral("fourFace")
        ? core::config::RadarArrayType::FourFace : core::config::RadarArrayType::SingleFace;
    configuration.maxRangeM = object.value(QStringLiteral("maxRangeM")).toDouble(0.0);
    configuration.longitudeDeg = object.value(QStringLiteral("longitudeDeg")).toDouble();
    configuration.latitudeDeg = object.value(QStringLiteral("latitudeDeg")).toDouble();
    configuration.altitudeM = object.value(QStringLiteral("altitudeM")).toDouble();
    configuration.facesJson = QString::fromUtf8(QJsonDocument(
        object.value(QStringLiteral("faces")).toArray()).toJson(QJsonDocument::Compact));
    configuration.endpointsJson = QString::fromUtf8(QJsonDocument(
        object.value(QStringLiteral("endpoints")).toArray()).toJson(QJsonDocument::Compact));
    configuration.protocolConfigJson = QString::fromUtf8(QJsonDocument(
        object.value(QStringLiteral("protocolConfig")).toObject()).toJson(QJsonDocument::Compact));
    configuration.changedBy = object.value(QStringLiteral("changedBy")).toString(QStringLiteral("UI"));
    configuration.changeReason = object.value(QStringLiteral("changeReason")).toString();
    return true;
}

} // namespace

/**
 * @brief 仅转移低频启动参数；SQLite 对象延迟到所属存储线程创建。
 */
StorageRuntime::StorageRuntime(
    QString databasePath, QString applicationVersion,
    core::model::StableIdRegistry &stableIds,
    const core::domain::HistoryTopic<core::model::TrackPacket> &trackTopic,
    core::domain::EventTopic<core::model::SystemEvent> &eventTopic,
    core::domain::LatestState<core::model::TaskState> &taskState)
    : m_databasePath(std::move(databasePath))
    , m_applicationVersion(std::move(applicationVersion))
    , m_stableIds(stableIds)
    , m_trackTopic(trackTopic)
    , m_eventTopic(eventTopic)
    , m_taskState(taskState)
{
}

/**
 * @brief 在异常析构时释放纯 C++ Owner；正常路径已由 shutdown() 在正确线程清空。
 */
StorageRuntime::~StorageRuntime() = default;

/**
 * @brief 严格按连接、服务 Owner、存储 Lane 与 Reader 顺序构造，任一步失败都关闭当前 application run。
 */
bool StorageRuntime::initialize()
{
    if (QThread::currentThread() != thread()) {
        m_lastError = QStringLiteral("StorageRuntime 必须在所属存储线程初始化");
        return false;
    }
    if (m_archiveRepository) {
        m_lastError = QStringLiteral("StorageRuntime 已经初始化");
        return false;
    }
    if (m_databasePath.trimmed().isEmpty() || m_applicationVersion.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("数据库路径和应用版本不能为空");
        return false;
    }

    m_archiveRepository = std::make_unique<core::archive::SqliteArchiveRepository>(
        m_databasePath, m_applicationVersion);
    if (!m_archiveRepository->open()) {
        m_lastError = m_archiveRepository->lastError();
        m_archiveRepository.reset();
        return false;
    }
    m_runtimeRepository = std::make_unique<core::persistence::SqliteRuntimeRepository>(
        *m_archiveRepository);
    m_deviceService = std::make_unique<core::config::DeviceConfigService>(
        *m_runtimeRepository, *m_archiveRepository, m_stableIds);
    m_eventPublisher = std::make_unique<core::producer::SystemEventPublisher>(m_eventTopic);
    m_taskService = std::make_unique<core::task::TaskService>(
        *m_runtimeRepository, *m_deviceService, *m_archiveRepository, m_stableIds,
        m_taskState, m_eventPublisher.get());

    if (!m_deviceService->loadFromDatabase() || !m_taskService->loadFromDatabase()) {
        m_lastError = !m_deviceService->lastError().isEmpty()
            ? m_deviceService->lastError() : m_taskService->lastError();
        m_taskService.reset();
        m_eventPublisher.reset();
        m_deviceService.reset();
        m_runtimeRepository.reset();
        m_archiveRepository->closeCleanly(QDateTime::currentMSecsSinceEpoch());
        m_archiveRepository.reset();
        return false;
    }
    m_archiveLane = std::make_unique<core::execution::ExecutionLane>();
    m_archiveReader = std::make_unique<core::archive::ArchiveReader>(
        m_trackTopic, m_eventTopic, *m_archiveLane, *m_archiveRepository, 256, 128);
    m_archiveReader->refresh();
    m_lastError.clear();
    return true;
}

/**
 * @brief 先停止归档 Consumer 与 Lane，再销毁借用连接的服务并正常闭合 application run。
 */
bool StorageRuntime::shutdown()
{
    if (QThread::currentThread() != thread()) {
        m_lastError = QStringLiteral("StorageRuntime 必须在所属存储线程关闭");
        return false;
    }
    m_archiveReader.reset();
    m_archiveLane.reset();
    m_taskService.reset();
    m_eventPublisher.reset();
    m_deviceService.reset();
    m_runtimeRepository.reset();
    if (!m_archiveRepository) {
        m_lastError.clear();
        return true;
    }
    const bool closed = m_archiveRepository->closeCleanly(QDateTime::currentMSecsSinceEpoch());
    if (!closed) m_lastError = m_archiveRepository->lastError();
    m_archiveRepository.reset();
    return closed;
}

/**
 * @brief 返回同步生命周期操作结束后稳定的错误文本值副本。
 */
QString StorageRuntime::lastError() const
{
    return m_lastError;
}

/**
 * @brief 从设备Owner读取当前配置并在存储线程完成JSON序列化。
 */
QByteArray StorageRuntime::listDevices()
{
    if (!m_deviceService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("设备服务尚未初始化"));
    QVector<core::config::DeviceConfiguration> configurations;
    if (!m_deviceService->listCurrent(configurations)) {
        return ownerErrorJson(m_deviceService->lastError());
    }
    QJsonArray devices;
    for (const auto &configuration : configurations) devices.append(deviceJson(configuration));
    return successJson(QJsonObject {{QStringLiteral("devices"), devices}});
}

/**
 * @brief 在设备Owner的当前快照中按稳定UUID查找唯一配置，不绕过Owner直接查询SQLite。
 */
QByteArray StorageRuntime::getDevice(const QString &deviceUuid)
{
    if (!m_deviceService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("设备服务尚未初始化"));
    const QUuid uuid(deviceUuid);
    if (uuid.isNull()) return errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("设备UUID无效"));
    QVector<core::config::DeviceConfiguration> configurations;
    if (!m_deviceService->listCurrent(configurations)) {
        return ownerErrorJson(m_deviceService->lastError());
    }
    for (const auto &configuration : configurations) {
        if (configuration.deviceUuid == uuid) {
            return successJson(QJsonObject {{QStringLiteral("device"), deviceJson(configuration)}});
        }
    }
    return errorJson(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("设备不存在或已删除"));
}

/**
 * @brief 只接受revision=0的新设备，请求成功后返回数据库生成的稳定UUID和revision。
 */
QByteArray StorageRuntime::createDevice(const QByteArray &requestBody)
{
    if (!m_deviceService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("设备服务尚未初始化"));
    core::config::DeviceConfiguration configuration;
    QString error;
    if (!parseDevice(requestBody, configuration, error) || configuration.revision != 0) {
        return errorJson(QStringLiteral("INVALID_REQUEST"),
                         error.isEmpty() ? QStringLiteral("新设备revision必须为0") : error);
    }
    core::model::DeviceId deviceId;
    core::model::EndpointId endpointId;
    if (!m_deviceService->saveAndActivate(configuration, deviceId, endpointId)) {
        return ownerErrorJson(m_deviceService->lastError());
    }
    return successJson(QJsonObject {{QStringLiteral("device"), deviceJson(configuration)}});
}

/**
 * @brief 强制使用路径UUID作为更新目标，并把expectedRevision作为唯一乐观并发门禁。
 */
QByteArray StorageRuntime::updateDevice(const QString &deviceUuid, const QByteArray &requestBody)
{
    if (!m_deviceService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("设备服务尚未初始化"));
    core::config::DeviceConfiguration configuration;
    QString error;
    const QUuid uuid(deviceUuid);
    if (uuid.isNull() || !parseDevice(requestBody, configuration, error) || configuration.revision == 0) {
        return errorJson(QStringLiteral("INVALID_REQUEST"),
                         error.isEmpty() ? QStringLiteral("设备UUID或revision无效") : error);
    }
    configuration.deviceUuid = uuid;
    core::model::DeviceId deviceId;
    core::model::EndpointId endpointId;
    if (!m_deviceService->saveAndActivate(configuration, deviceId, endpointId)) {
        return ownerErrorJson(m_deviceService->lastError());
    }
    return successJson(QJsonObject {{QStringLiteral("device"), deviceJson(configuration)}});
}

/**
 * @brief 解析删除请求中的revision审计字段，并把确定结果直接返回原HTTP请求者。
 */
QByteArray StorageRuntime::deleteDevice(const QString &deviceUuid, const QByteArray &requestBody)
{
    if (!m_deviceService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("设备服务尚未初始化"));
    QJsonParseError parseError;
    const QJsonObject object = QJsonDocument::fromJson(requestBody, &parseError).object();
    const QUuid uuid(deviceUuid);
    bool revisionOk = false;
    const qulonglong rawRevision = object.value(QStringLiteral("expectedRevision"))
        .toString().toULongLong(&revisionOk);
    const quint32 revision = revisionOk && rawRevision <= std::numeric_limits<quint32>::max()
        ? static_cast<quint32>(rawRevision) : 0U;
    if (parseError.error != QJsonParseError::NoError || uuid.isNull() || revision == 0) {
        return errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("删除请求UUID或expectedRevision无效"));
    }
    if (!m_deviceService->softDelete(
            uuid, revision, object.value(QStringLiteral("changedBy")).toString(QStringLiteral("UI")),
            object.value(QStringLiteral("changeReason")).toString())) {
        return ownerErrorJson(m_deviceService->lastError());
    }
    return successJson();
}

/**
 * @brief 从TaskService读取当前任务快照并在SQLite线程完成JSON序列化。
 */
QByteArray StorageRuntime::listTasks()
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    QVector<core::task::TaskDefinition> tasks;
    if (!m_taskService->listTasks(tasks)) return ownerErrorJson(m_taskService->lastError());
    QJsonArray array;
    for (const auto &task : tasks) {
        core::model::TaskId taskId;
        m_taskService->resolveTask(task.taskUuid, taskId);
        array.append(taskJson(task, activePhaseOf(taskId)));
    }
    return successJson(QJsonObject {{QStringLiteral("tasks"), array}});
}

/**
 * @brief 从TaskService读取模板快照，并把同一RadarControlSettings字段集合返回前端。
 */
QByteArray StorageRuntime::listTaskTemplates()
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    QVector<QUuid> uuids;
    QVector<QString> names;
    QVector<core::task::RadarControlSettings> settings;
    if (!m_taskService->listControlTemplates(uuids, names, settings)) {
        return ownerErrorJson(m_taskService->lastError());
    }
    QJsonArray templates;
    for (qsizetype index = 0; index < uuids.size(); ++index) {
        templates.append(QJsonObject {
            {QStringLiteral("templateId"), uuids[index].toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"), names[index]},
            {QStringLiteral("controlSettings"), core::task::radarControlSettingsToJson(settings[index])}
        });
    }
    return successJson(QJsonObject {{QStringLiteral("templates"), templates}});
}

/**
 * @brief 解析HTTP意图后只调用TaskService创建任务，不启动采集或发送设备命令。
 */
QByteArray StorageRuntime::createTask(const QByteArray &requestBody)
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    core::task::TaskDefinition task;
    QString error;
    if (!parseTask(requestBody, task, error)) return errorJson(QStringLiteral("INVALID_REQUEST"), error);
    core::model::TaskId taskId;
    if (!m_taskService->createTask(task, taskId)) {
        return ownerErrorJson(m_taskService->lastError());
    }
    return successJson(QJsonObject {{QStringLiteral("task"), taskJson(task)}});
}

/**
 * @brief 把HTTP选择意图交给TaskService，使Ready成为TaskState中的唯一活动事实。
 */
QByteArray StorageRuntime::activateTask(const QString &taskUuid)
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    const QUuid uuid(taskUuid);
    core::task::TaskDefinition task;
    if (uuid.isNull() || !m_taskService->activateTask(uuid, task)) {
        return uuid.isNull()
            ? errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("任务UUID无效"))
            : ownerErrorJson(m_taskService->lastError());
    }
    return successJson(QJsonObject {{QStringLiteral("task"),
                                     taskJson(task, core::model::TaskPhase::Ready)}});
}

/**
 * @brief 仅撤销因 DPH 快照准备失败而无法使用的当前 Ready 激活，保留数据库中的原草稿。
 */
bool StorageRuntime::cancelTaskActivation(const QUuid &taskUuid)
{
    if (!m_taskService || !m_taskService->cancelActivation(taskUuid)) {
        m_lastError = m_taskService ? m_taskService->lastError()
                                    : QStringLiteral("任务服务尚未初始化");
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 把完成意图交给TaskService收口SQLite任务并清除唯一活动TaskState。
 */
QByteArray StorageRuntime::completeTask(const QString &taskUuid)
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    const QUuid uuid(taskUuid);
    core::task::TaskDefinition task;
    if (uuid.isNull() || !m_taskService->completeTask(uuid, task)) {
        return uuid.isNull()
            ? errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("任务UUID无效"))
            : ownerErrorJson(m_taskService->lastError());
    }
    return successJson(QJsonObject {{QStringLiteral("task"), taskJson(task)}});
}

/**
 * @brief 只把已完成任务写成归档状态；原有航迹、事件和会话始终留在 SQLite 真源。
 */
QByteArray StorageRuntime::archiveTask(const QString &taskUuid)
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    const QUuid uuid(taskUuid);
    core::task::TaskDefinition task;
    if (uuid.isNull() || !m_taskService->archiveTask(uuid, task)) {
        return uuid.isNull()
            ? errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("任务UUID无效"))
            : ownerErrorJson(m_taskService->lastError());
    }
    return successJson(QJsonObject {{QStringLiteral("task"), taskJson(task)}});
}

/**
 * @brief 从TaskService读取Ready任务；未选择任务时明确返回null。
 */
QByteArray StorageRuntime::activeTask()
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    core::task::TaskDefinition task;
    if (!m_taskService->activeTask(task)) {
        return successJson(QJsonObject {{QStringLiteral("activeTaskId"), QJsonValue::Null}});
    }
    const auto current = m_taskState.latest();
    return successJson(QJsonObject {
        {QStringLiteral("activeTaskId"), task.taskUuid.toString(QUuid::WithoutBraces)},
        {QStringLiteral("task"), taskJson(task, current ? current->sample->value.phase
                                                       : core::model::TaskPhase::None)}});
}

/**
 * @brief 将路径中的稳定UUID交给TaskService软删除，失败原因直接返回原HTTP请求者。
 */
QByteArray StorageRuntime::deleteTask(const QString &taskUuid)
{
    if (!m_taskService) return errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
    const QUuid uuid(taskUuid);
    if (uuid.isNull() || !m_taskService->softDeleteTask(uuid, QStringLiteral("UI"))) {
        return uuid.isNull()
            ? errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("任务UUID无效"))
            : ownerErrorJson(m_taskService->lastError());
    }
    return successJson();
}

/**
 * @brief 按“结构 → 类型 → 值域 → 任务存在性 → Ready/Running → 持久化”的固定顺序分类失败。
 * @details 每类失败直接写入稳定 error.code，调用方不得解析中文文本；Running 阶段额外要求当前
 * DeviceController 准备句柄与目标任务一致，避免落库后没有可同步的运行副本。
 */
bool StorageRuntime::updateTaskControlSettings(
    const QString &taskUuid,
    const QByteArray &requestBody,
    core::model::TaskId preparedTaskId,
    core::model::TaskId &appliedTaskId,
    core::task::TaskDefinition &appliedTask,
    QByteArray &response)
{
    appliedTaskId = core::model::TaskId();
    appliedTask = {};
    if (!m_taskService) {
        response = errorJson(QStringLiteral("SERVICE_UNAVAILABLE"), QStringLiteral("任务服务尚未初始化"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(requestBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        response = errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("请求体必须是JSON object"));
        return false;
    }
    const QJsonValue controlSettingsValue =
        document.object().value(QStringLiteral("controlSettings"));
    if (!controlSettingsValue.isObject()) {
        response = errorJson(QStringLiteral("INVALID_REQUEST"),
                             QStringLiteral("controlSettings必须是JSON object"));
        return false;
    }
    core::task::RadarControlSettings settings;
    QString error;
    if (!core::task::radarControlSettingsStructureFromJson(
            controlSettingsValue.toObject(), settings, error)) {
        response = errorJson(QStringLiteral("INVALID_REQUEST"), error);
        return false;
    }
    if (!core::task::validateRadarControlSettings(settings, error)) {
        response = errorJson(QStringLiteral("BUSINESS_REJECTED"), error);
        return false;
    }
    const QUuid uuid(taskUuid);
    if (uuid.isNull()) {
        response = errorJson(QStringLiteral("INVALID_REQUEST"), QStringLiteral("任务UUID无效"));
        return false;
    }
    core::model::TaskId taskId;
    if (!m_taskService->resolveTask(uuid, taskId)) {
        response = errorJson(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("任务不存在或已删除"));
        return false;
    }
    const auto current = m_taskState.latest();
    if (!current || current->sample->value.taskId != taskId
        || (current->sample->value.phase != core::model::TaskPhase::Ready
            && current->sample->value.phase != core::model::TaskPhase::Running)) {
        response = errorJson(QStringLiteral("TASK_STATE_CONFLICT"),
                             QStringLiteral("任务当前状态不允许修改控制参数"));
        return false;
    }
    const core::model::TaskPhase phase = current->sample->value.phase;
    if (phase == core::model::TaskPhase::Running && preparedTaskId != taskId) {
        response = errorJson(QStringLiteral("TASK_STATE_CONFLICT"),
                             QStringLiteral("运行中任务的设备控制副本尚未就绪"));
        return false;
    }
    core::task::TaskDefinition task;
    if (!m_taskService->updateControlSettings(uuid, settings, appliedTaskId, task)) {
        publishTaskSettingsFault(
            core::model::EventCode::ControlTaskControlSettingsPersistFailed, taskId);
        response = errorJson(QStringLiteral("PERSISTENCE_FAILED"),
                             m_taskService->lastError().isEmpty()
                                 ? QStringLiteral("任务控制参数持久化失败")
                                 : m_taskService->lastError());
        return false;
    }
    appliedTask = task;
    response = successJson(QJsonObject {{QStringLiteral("task"), taskJson(appliedTask, phase)}});
    return true;
}

/**
 * @brief 发布“快照已更新但设备运行副本替换失败”的 Control 域 Fault 事实，供主线程编排者跨线程调用。
 */
bool StorageRuntime::publishTaskSettingsInvariantFault(core::model::TaskId taskId)
{
    return publishTaskSettingsFault(
        core::model::EventCode::ControlTaskControlSettingsInvariantFailed, taskId);
}

/**
 * @brief 在同一条既定路径上补齐 SampleMeta 与稳定事件码，再交给唯一事件门禁。
 */
bool StorageRuntime::publishTaskSettingsFault(
    core::model::EventCode code, core::model::TaskId taskId)
{
    if (!m_eventPublisher || !taskId.isSet()) return false;
    core::model::Sample<core::model::SystemEvent> sample;
    sample.meta.receiveTimeUtcMs = QDateTime::currentMSecsSinceEpoch();
    sample.meta.receiveTimeSteadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    sample.meta.traceId = m_nextFaultTraceId++;
    if (m_nextFaultTraceId == 0) m_nextFaultTraceId = 1;
    sample.value.domain = core::model::EventDomain::Control;
    sample.value.severity = code == core::model::EventCode::ControlTaskControlSettingsInvariantFailed
        ? core::model::EventSeverity::Fault : core::model::EventSeverity::Error;
    sample.value.eventCode = code;
    sample.value.taskId = taskId;
    sample.value.flags = core::model::eventSubjectBit(core::model::EventSubjectFlag::TaskIdValid);
    try {
        return m_eventPublisher->publish(std::move(sample)).accepted();
    } catch (const std::overflow_error &) {
        return false;
    }
}

/**
 * @brief 只读取唯一 TaskState 中属于该任务的阶段，不查询数据库或推断设备状态。
 */
core::model::TaskPhase StorageRuntime::activePhaseOf(core::model::TaskId taskId) const
{
    if (!taskId.isSet()) return core::model::TaskPhase::None;
    const auto current = m_taskState.latest();
    if (!current || current->sample->value.taskId != taskId) {
        return core::model::TaskPhase::None;
    }
    return current->sample->value.phase;
}

/**
 * @brief 在SQLite Owner线程把当前活动任务的稳定身份解析为一次采集所需的紧凑运行句柄。
 * @details V1主链只接受一台完整雷达；多雷达异构组合仍属于插件边界，不在此处猜测分配策略。
 */
bool StorageRuntime::resolveActiveTaskContext(ActiveTaskRuntimeContext &context)
{
    context = {};
    if (!m_taskService || !m_deviceService) {
        m_lastError = QStringLiteral("任务或设备服务尚未初始化");
        return false;
    }
    if (!m_taskService->activeTask(context.task)
        || context.task.deviceBindings.size() != 1) {
        m_lastError = QStringLiteral("当前必须有且仅有一台完整雷达的活动任务");
        return false;
    }
    const auto &binding = context.task.deviceBindings.front();
    if (binding.faceMask == 0U || (binding.faceMask & 0xF0U) != 0U
        || !m_taskService->resolveTask(context.task.taskUuid, context.taskId)
        || !m_deviceService->resolveLatestDevice(
            binding.deviceUuid, context.deviceId, context.endpointId)
        || !m_deviceService->resolveConfigurationRevision(
            context.deviceId, context.configurationRevision)
        || !m_deviceService->resolveConfiguration(
            context.deviceId, context.configuration)) {
        m_lastError = QStringLiteral("活动任务的设备、端点或配置revision无法解析");
        return false;
    }
    context.faceMask = binding.faceMask;
    m_lastError.clear();
    return true;
}

/**
 * @brief 把已解析身份交给TaskService一次创建全部阵面会话；失败时由TaskService完成内部回滚。
 */
bool StorageRuntime::startCollections(ActiveTaskRuntimeContext &context)
{
    if (!m_taskService || !m_taskService->startCollections(
            context.taskId, context.deviceId, context.endpointId,
            context.faceMask, context.faceAcquisitionIds)) {
        m_lastError = m_taskService ? m_taskService->lastError()
                                    : QStringLiteral("任务服务尚未初始化");
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 使用同一停止时刻依次收口全部非零阵面会话，并保留首个失败原因供原HTTP请求者诊断。
 */
bool StorageRuntime::stopCollections(
    const std::array<core::model::AcquisitionId, 4> &acquisitionIds)
{
    if (!m_taskService) {
        m_lastError = QStringLiteral("任务服务尚未初始化");
        return false;
    }
    bool found = false;
    bool success = true;
    QString firstError;
    const qint64 stoppedUtcMs = QDateTime::currentMSecsSinceEpoch();
    for (const auto acquisitionId : acquisitionIds) {
        if (!acquisitionId.isSet()) continue;
        found = true;
        if (!m_taskService->stopCollection(
                acquisitionId, core::task::CollectionStopReason::UserStop, stoppedUtcMs)) {
            if (firstError.isEmpty()) firstError = m_taskService->lastError();
            success = false;
        }
    }
    if (!found) firstError = QStringLiteral("当前没有可停止的采集会话");
    if (!found || !success) {
        m_lastError = firstError;
        return false;
    }
    m_lastError.clear();
    return true;
}

} // namespace core::app
