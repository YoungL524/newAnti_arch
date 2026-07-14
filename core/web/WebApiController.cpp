#include "WebApiController.h"
#include "WebService.h"
#include "bus/BusEnvelope.h"
#include "bus/SystemBus.h"
#include "bus/GenericPackets.h"
#include "RadarWorkModeController.h"
#include "business/DeviceRegistry.h"
#include "business/RuntimeSettingsService.h"
#include "business/TaskService.h"
#include "business/TrackTypes.h"
#include "log/Logger.h"
#include <cmath>
#include <limits>
#include <optional>
#include <memory>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <QTimer>

namespace {

bool wmcAcquisitionActive(RadarWorkModeController::State st)
{
    return st == RadarWorkModeController::State::Starting
        || st == RadarWorkModeController::State::Working;
}

QJsonObject composeRadarDeviceStatus(RadarWorkModeController* wmc, bool endpointOpen)
{
    QJsonObject dev;
    if (!wmc || !endpointOpen) {
        dev[QStringLiteral("state")] = RadarWorkModeController::stateName(
            RadarWorkModeController::State::Standby);
        dev[QStringLiteral("acquisition")] = false;
        return dev;
    }
    const auto st = wmc->state();
    dev[QStringLiteral("state")] = RadarWorkModeController::stateName(st);
    dev[QStringLiteral("acquisition")] = wmcAcquisitionActive(st);
    return dev;
}

QString resolveDeviceEndpointId(const QString& deviceId, const QString& endpointId)
{
    if (!endpointId.isEmpty())
        return endpointId;
    // Legacy fallback: 历史库可能存在 id=radar_cpu 但未写 endpoint_id 的设备
    if (deviceId == QStringLiteral("radar_cpu"))
        return QStringLiteral("radar_cpu");
    return {};
}

} // namespace

WebApiController::WebApiController(web::WebService* ws,
                                   RadarWorkModeController* wmc,
                                   DeviceRegistry* deviceRegistry,
                                   RuntimeSettingsService* runtimeSettings,
                                   RuntimeStatusProvider runtimeStatusProvider,
                                   HealthProvider healthProvider,
                                   TaskService* taskService,
                                   QObject* parent)
    : QObject(parent)
    , m_ws(ws)
    , m_wmc(wmc)
    , m_deviceReg(deviceRegistry)
    , m_runtimeSettings(runtimeSettings)
    , m_runtimeStatusProvider(std::move(runtimeStatusProvider))
    , m_health(std::move(healthProvider))
    , m_taskService(taskService)
{
}

WebApiController::~WebApiController() { shutdown(); }

void WebApiController::registerRestRoutes()
{
    // GET /api/status
    m_ws->handleGet(QStringLiteral("/api/status"), [this](const QJsonObject&, const QString&) {
        QJsonObject s;
        auto health = m_health();
        s[QStringLiteral("ok")] = true;
        s[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        s[QStringLiteral("uptime")] = QDateTime::currentSecsSinceEpoch();
        s[QStringLiteral("endpoints")] = health.comm.openEndpoints;
        s[QStringLiteral("dbConnected")] = health.database.connected;
        s[QStringLiteral("devices")] = buildDeviceStatusArray(health);
        const bool commActive = health.comm.openEndpoints > 0;
        auto st2 = m_wmc ? m_wmc->state() : RadarWorkModeController::State::Standby;
        s[QStringLiteral("acquisition")] = commActive && wmcAcquisitionActive(st2);
        s[QStringLiteral("state")] = commActive
            ? (m_wmc ? RadarWorkModeController::stateName(st2) : QStringLiteral("Unknown"))
            : RadarWorkModeController::stateName(RadarWorkModeController::State::Standby);
        return s;
    });

    // GET /api/logs
    m_ws->handleGet(QStringLiteral("/api/logs"), [](const QJsonObject&, const QString&) {
        QJsonObject r; r[QStringLiteral("ok")] = true;
        QJsonArray arr;
        for (const auto& rec : Logger::instance().recentLogRecords(100))
            arr.append(rec.toJson());
        r[QStringLiteral("logs")] = arr;
        return r;
    });

    if (m_wmc) {
        auto handleCommand = [this](const QJsonObject& body, bool work) -> QJsonObject {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            qint64& lastMs = work ? m_lastStartMs : m_lastStopMs;
            if (now - lastMs < 500)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("throttled"), true}};

            const QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();

            if (work) {
                if (!m_taskService || !m_taskService->getActiveTask().has_value()) {
                    return QJsonObject{
                        {QStringLiteral("ok"), false},
                        {QStringLiteral("error"), QStringLiteral("请先创建并激活任务")}};
                }
                for (const auto& v : ids) {
                    const QString id = v.toString();
                    if (id.isEmpty())
                        continue;
                    if (!m_taskService->isDeviceInActiveTask(id)) {
                        return QJsonObject{
                            {QStringLiteral("ok"), false},
                            {QStringLiteral("error"),
                             QStringLiteral("所选设备不属于当前激活任务: %1").arg(id)}};
                    }
                }
            }

            const auto resolution = resolveSingleControllerDeviceIds(ids);
            if (!ids.isEmpty() && !resolution.hasExecutableTarget)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("不支持的设备 id")}};

            if (work && m_taskService && resolution.hasExecutableTarget
                && !m_taskService->isDeviceInActiveTask(resolution.executedId)) {
                return QJsonObject{
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QStringLiteral("所选设备不属于当前激活任务: %1").arg(resolution.executedId)}};
            }

            const bool cmdAccepted = work ? m_wmc->requestWork() : m_wmc->requestStandby();
            if (!cmdAccepted)
                return QJsonObject{{QStringLiteral("ok"), false},
                                   {QStringLiteral("error"), QStringLiteral("命令被拒绝：状态转换中")}};

            lastMs = now;
            if (m_taskService) {
                m_taskService->appendActiveTaskEvent(
                    QStringLiteral("operation"),
                    work ? QStringLiteral("开始采集") : QStringLiteral("停止采集"),
                    resolution.executedId);
            }
            return buildSingleControllerCommandResponse(
                resolution.executedId, resolution.rejectedDeviceIds, true);
        };
        m_ws->handlePost(QStringLiteral("/api/start"), [this, handleCommand](const QJsonObject& body) { return handleCommand(body, true); });
        m_ws->handlePost(QStringLiteral("/api/stop"),  [this, handleCommand](const QJsonObject& body) { return handleCommand(body, false); });

        m_ws->handlePost(QStringLiteral("/api/reset"), [this](const QJsonObject& body) -> QJsonObject {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastResetMs < 500)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("throttled"), true}};

            const QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            const auto resolution = resolveSingleControllerDeviceIds(ids);
            if (!ids.isEmpty() && !resolution.hasExecutableTarget)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("不支持的设备 id")}};

            const bool cmdAccepted = m_wmc && m_wmc->reset();
            if (!cmdAccepted)
                return QJsonObject{{QStringLiteral("ok"), false},
                                   {QStringLiteral("error"), QStringLiteral("复位被拒绝：当前非 Fault 状态")}};

            m_lastResetMs = now;
            QJsonObject r = buildSingleControllerCommandResponse(
                resolution.executedId, resolution.rejectedDeviceIds, true);
            if (m_wmc)
                r[QStringLiteral("state")] = RadarWorkModeController::stateName(m_wmc->state());
            return r;
        });
    }

    // 航迹交互命令：校验 REST 请求后发布 TrackInteractionCommand 到总线。
    // 成功响应 ok/accepted/mode=queued 仅表示命令已入队，不代表 Manager 已应用；
    // 最终 interaction 状态以 WebSocket tracks snapshot 为准（见 05-核心流程 §2）。
    auto handleTrackInteraction = [this](const QString& suffix, const QJsonObject& body) -> std::optional<QJsonObject> {
        const auto parts = suffix.split(QChar('/'), Qt::SkipEmptyParts);
        if (parts.size() != 4
         || parts[1] != QStringLiteral("tracks")
         || parts[3] != QStringLiteral("interaction"))
            return std::nullopt;

        const QString deviceId = parts[0];
        bool ok = false;
        quint64 trackId64 = parts[2].toULongLong(&ok);
        if (deviceId.isEmpty() || !ok || trackId64 > std::numeric_limits<uint32_t>::max())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("deviceId 或 trackId 无效")}};

        if (!body.contains(QStringLiteral("selected")) || !body.value(QStringLiteral("selected")).isBool())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("selected 必须是 bool")}};
        if (!body.contains(QStringLiteral("trackingEnabled")) || !body.value(QStringLiteral("trackingEnabled")).isBool())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("trackingEnabled 必须是 bool")}};

        const bool selected = body.value(QStringLiteral("selected")).toBool();
        const bool trackingEnabled = body.value(QStringLiteral("trackingEnabled")).toBool();
        const uint32_t trackId = static_cast<uint32_t>(trackId64);

        if (!m_taskService)
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("任务服务未初始化")}};
        const auto activeTask = m_taskService->getActiveTask();
        if (!activeTask.has_value())
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("当前无激活任务")}};
        if (!m_taskService->isDeviceInActiveTask(deviceId))
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("设备不属于当前激活任务")}};

        auto cmd = std::make_shared<TrackInteractionCommand>();
        cmd->taskId = activeTask->id;
        cmd->deviceId = deviceId;
        cmd->trackId = trackId;
        cmd->selected = selected;
        cmd->trackingEnabled = trackingEnabled;
        cmd->timestamp = QDateTime::currentMSecsSinceEpoch();
        if (!SystemBus::getInstance().publish(
                BusEnvelope::makeTrackInteractionCommand(std::move(cmd)))) {
            return QJsonObject{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"), QStringLiteral("航迹交互命令发布失败")}};
        }

        QJsonObject interaction;
        interaction[QStringLiteral("taskId")] = activeTask->id;
        interaction[QStringLiteral("deviceId")] = deviceId;
        interaction[QStringLiteral("trackId")] = static_cast<qint64>(trackId);
        interaction[QStringLiteral("selected")] = selected;
        interaction[QStringLiteral("trackingEnabled")] = trackingEnabled;
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("accepted"), true},
            {QStringLiteral("mode"), QStringLiteral("queued")},
            {QStringLiteral("interaction"), interaction}};
    };

    // ── Device Registry API ──
    if (m_deviceReg) {
        auto deviceInfoToJson = [](const DeviceInfo& dev) -> QJsonObject {
            QJsonObject j;
            j[QStringLiteral("id")] = dev.id;
            j[QStringLiteral("name")] = dev.name;
            j[QStringLiteral("brand")] = dev.brand;
            j[QStringLiteral("kind")] = static_cast<int>(dev.kind);
            j[QStringLiteral("lng")] = dev.lng;
            j[QStringLiteral("lat")] = dev.lat;
            j[QStringLiteral("altitude")] = dev.altitude;
            j[QStringLiteral("maxRange")] = dev.maxRange;
            j[QStringLiteral("facesJson")] = dev.facesJson;
            j[QStringLiteral("endpointId")] = dev.endpointId;
            j[QStringLiteral("linkedRadarId")] = dev.linkedRadarId;
            j[QStringLiteral("mountOffsetAzimuth")] = dev.mountOffsetAzimuth;
            j[QStringLiteral("coordinateSystem")] = dev.coordinateSystem;
            return j;
        };

        m_ws->handleGet(QStringLiteral("/api/devices"), [this, deviceInfoToJson](const QJsonObject&, const QString&) {
            QJsonArray arr;
            for (const auto& d : m_deviceReg->list()) {
                auto item = deviceInfoToJson(d);
                item[QStringLiteral("status")] = static_cast<int>(d.status);
                arr.append(item);
            }
            QJsonObject r; r[QStringLiteral("ok")] = true; r[QStringLiteral("devices")] = arr;
            return r;
        });

        auto deviceFromBody = [](const QJsonObject& body) -> DeviceInfo {
            DeviceInfo d;
            d.id           = body[QStringLiteral("id")].toString();
            d.name         = body[QStringLiteral("name")].toString();
            d.brand        = body[QStringLiteral("brand")].toString();
            d.kind         = static_cast<DeviceKind>(body[QStringLiteral("kind")].toInt());
            d.lng          = body[QStringLiteral("lng")].toDouble();
            d.lat          = body[QStringLiteral("lat")].toDouble();
            d.altitude     = body[QStringLiteral("altitude")].toDouble();
            d.maxRange     = body[QStringLiteral("maxRange")].toDouble();
            d.facesJson    = body[QStringLiteral("facesJson")].toString(QStringLiteral("[]"));
            d.endpointId   = body[QStringLiteral("endpointId")].toString();
            d.linkedRadarId = body[QStringLiteral("linkedRadarId")].toString();
            d.mountOffsetAzimuth = body[QStringLiteral("mountOffsetAzimuth")].toDouble();
            d.coordinateSystem = body[QStringLiteral("coordinateSystem")].toString(QStringLiteral("WGS-84"));
            return d;
        };

        auto validate = [](const DeviceInfo& d) -> QString {
            if (d.name.isEmpty())
                return QStringLiteral("name 不能为空");
            if (d.kind < DeviceKind::Radar || d.kind > DeviceKind::Gps)
                return QStringLiteral("kind 无效 (0=Radar,1=Opto,2=Level,3=Gps)");
            if (d.lng < -180 || d.lng > 180)
                return QStringLiteral("longitude 必须在 [-180, 180] 范围内");
            if (d.lat < -90 || d.lat > 90)
                return QStringLiteral("latitude 必须在 [-90, 90] 范围内");
            if (d.kind == DeviceKind::Radar && !d.facesJson.isEmpty()) {
                QJsonParseError err;
                QJsonDocument::fromJson(d.facesJson.toUtf8(), &err);
                if (err.error != QJsonParseError::NoError)
                    return QStringLiteral("facesJson JSON 格式错误");
            }
            if (d.kind != DeviceKind::Radar && !d.endpointId.isEmpty())
                return QStringLiteral("只有雷达设备允许绑定通信端点");
            return {};
        };

        m_ws->handlePost(QStringLiteral("/api/devices"), [this, validate, deviceFromBody, deviceInfoToJson](const QJsonObject& body) {
            auto d = deviceFromBody(body);
            QString err = validate(d);
            if (!err.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            auto created = m_deviceReg->createAssigningId(d);
            if (!created.has_value())
                return QJsonObject{{QStringLiteral("ok"), false},
                                   {QStringLiteral("error"), m_deviceReg->lastError().isEmpty()
                                        ? QStringLiteral("创建设备失败") : m_deviceReg->lastError()}};
            QJsonObject r;
            r[QStringLiteral("ok")] = true;
            r[QStringLiteral("device")] = deviceInfoToJson(created.value());
            const QStringList migrated = m_deviceReg->takeLastMigratedDeviceIds();
            if (!migrated.isEmpty()) {
                QJsonArray arr;
                for (const auto& id : migrated) arr.append(id);
                r[QStringLiteral("migratedDeviceIds")] = arr;
            }
            return r;
        });

        m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), [this, validate, deviceFromBody, deviceInfoToJson, handleTrackInteraction](const QString& suffix, const QJsonObject& body) {
            if (auto r = handleTrackInteraction(suffix, body))
                return *r;

            if (suffix.contains(QChar('/')))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("无效路径")}};

            auto d = deviceFromBody(body);
            if (!d.id.isEmpty() && d.id != suffix)
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("路径 id 与 body id 不匹配")}};
            if (d.id.isEmpty()) d.id = suffix;
            if (!m_deviceReg->get(d.id).has_value())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("设备不存在")}};
            QString err = validate(d);
            if (!err.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            bool ok = m_deviceReg->update(d);
            QJsonObject r{{QStringLiteral("ok"), ok}};
            if (ok) {
                if (auto updated = m_deviceReg->get(d.id); updated.has_value())
                    r[QStringLiteral("device")] = deviceInfoToJson(updated.value());
                const QStringList migrated = m_deviceReg->takeLastMigratedDeviceIds();
                if (!migrated.isEmpty()) {
                    QJsonArray arr;
                    for (const auto& id : migrated) arr.append(id);
                    r[QStringLiteral("migratedDeviceIds")] = arr;
                }
            } else
                r[QStringLiteral("error")] = m_deviceReg->lastError().isEmpty()
                    ? QStringLiteral("更新设备失败") : m_deviceReg->lastError();
            return r;
        });

        m_ws->handleDeletePrefix(QStringLiteral("/api/devices/"), [this](const QString& suffix, const QJsonObject&) {
            if (suffix.isEmpty())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("id 不能为空")}};
            if (!m_deviceReg->get(suffix).has_value())
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("设备不存在")}};
            bool ok = m_deviceReg->remove(suffix);
            QJsonObject result{{QStringLiteral("ok"), ok}};
            if (!ok)
                result[QStringLiteral("error")] = m_deviceReg->lastError().isEmpty()
                    ? QStringLiteral("删除设备失败") : m_deviceReg->lastError();
            return result;
        });
    }

    // ── Track Interaction API（无 DeviceRegistry 时单独注册）──
    if (!m_deviceReg) {
        m_ws->handlePutPrefix(QStringLiteral("/api/devices/"), [this, handleTrackInteraction](const QString& suffix, const QJsonObject& body) {
            if (auto r = handleTrackInteraction(suffix, body))
                return *r;
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("无效路径")}};
        });
    }

    // ── Runtime Settings API ──
    auto buildStatusJson = [this]() -> QJsonObject {
        QJsonObject st;
        if (m_runtimeStatusProvider) {
            auto ds = m_runtimeStatusProvider();
            st[QStringLiteral("currentSceneNumber")] = static_cast<int>(ds.currentSceneNumber);
            st[QStringLiteral("sendCount")] = static_cast<qint64>(ds.sendCount);
        }
        return st;
    };

    m_ws->handleGet(QStringLiteral("/api/runtime-settings"), [this, buildStatusJson](const QJsonObject&, const QString&) {
        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        QJsonObject settingsJson;
        if (m_runtimeSettings) {
            auto s = m_runtimeSettings->get();
            settingsJson[QStringLiteral("frequencyControl")] = static_cast<int>(s.frequencyControl);
            settingsJson[QStringLiteral("waveControl")] = static_cast<int>(s.waveControl);
        }
        r[QStringLiteral("settings")] = settingsJson;
        r[QStringLiteral("status")] = buildStatusJson();
        return r;
    });

    if (m_runtimeSettings) {
        m_ws->handlePut(QStringLiteral("/api/runtime-settings"), [this, buildStatusJson](const QJsonObject& body) {
            auto parseField = [&](const QString& key, uint32_t* out, QString* err) -> bool {
                if (!body.contains(key)) { *err = key + QStringLiteral(" 不能为空"); return false; }
                const auto v = body.value(key);
                if (!v.isDouble()) { *err = key + QStringLiteral(" 必须是数字"); return false; }
                const double d = v.toDouble();
                if (!std::isfinite(d) || std::floor(d) != d) { *err = key + QStringLiteral(" 必须是整数"); return false; }
                if (d < 0) { *err = key + QStringLiteral(" 必须是非负整数"); return false; }
                if (d > static_cast<double>(std::numeric_limits<uint32_t>::max())) { *err = key + QStringLiteral(" 超出 uint32 范围"); return false; }
                *out = static_cast<uint32_t>(d);
                return true;
            };
            RadarRuntimeSettings s;
            QString err;
            if (!parseField(QStringLiteral("frequencyControl"), &s.frequencyControl, &err)
             || !parseField(QStringLiteral("waveControl"), &s.waveControl, &err))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            // service validate (含 0..255 等业务规则)
            if (!m_runtimeSettings->validate(s, &err))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            // Publish first, then commit
            DataPacket packet;
            packet.sourceType = SOURCE_RUNTIME_SETTINGS;
            packet.dispatchTarget = 0;
            static uint64_t seq = 0;
            packet.packetSequence = ++seq;
            packet.timeStamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            packDataToPayload(packet, s);
            if (!SystemBus::getInstance().publish(std::move(packet)))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("运行时参数事件发布失败")}};
            if (!m_runtimeSettings->set(s, &err))
                return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}};
            QJsonObject settingsJson;
            settingsJson[QStringLiteral("frequencyControl")] = static_cast<int>(s.frequencyControl);
            settingsJson[QStringLiteral("waveControl")] = static_cast<int>(s.waveControl);
            QJsonObject r;
            r[QStringLiteral("ok")] = true;
            r[QStringLiteral("settings")] = settingsJson;
            r[QStringLiteral("status")] = buildStatusJson();
            return r;
        });
    }

}

void WebApiController::connectWebPush()
{
    auto* ws = m_ws;
    auto* wmc = m_wmc;

    // HTTP 日志转发
    QObject::connect(ws, &web::WebService::serverLog,
                     [](const QString& level, const QString& msg) {
        if (level == QStringLiteral("error"))
            qWarning().noquote() << QStringLiteral("[HTTP]") << msg;
        else qDebug().noquote() << QStringLiteral("[HTTP]") << msg;
    });

    // Logger → WS 日志推送
    auto* taskService = m_taskService;
    Logger::instance().setExternalCallback([ws, taskService](const LogRecord& rec) {
        if (rec.level == QStringLiteral("DEBUG")) return;
        QJsonObject m = rec.toJson();
        m[QStringLiteral("type")] = QStringLiteral("log");
        QMetaObject::invokeMethod(ws, [ws, taskService, rec, m]() {
            if (taskService && !rec.taskId.isEmpty()) {
                taskService->appendTaskLog(rec.taskId, rec.deviceId, rec.level,
                                           rec.message, rec.timestampMs);
            }
            ws->broadcast(m);
        }, Qt::QueuedConnection);
    });

    // WS 定时状态推送
    auto* timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, [this, wmc, ws]() {
        auto health = m_health();
        const bool commActive = health.comm.openEndpoints > 0;
        QJsonObject msg;
        msg[QStringLiteral("type")] = QStringLiteral("status");
        msg[QStringLiteral("connected")] = commActive;
        msg[QStringLiteral("upTime")] = health.timestampMs;
        msg[QStringLiteral("devices")] = buildDeviceStatusArray(health);
        auto st3 = wmc ? wmc->state() : RadarWorkModeController::State::Standby;
        msg[QStringLiteral("acquisition")] = commActive && wmcAcquisitionActive(st3);
        msg[QStringLiteral("state")] = commActive
            ? RadarWorkModeController::stateName(st3)
            : RadarWorkModeController::stateName(RadarWorkModeController::State::Standby);
        ws->broadcast(msg);
    });
    timer->start(1000);
}

void WebApiController::shutdown()
{
    Logger::instance().setExternalCallback(nullptr);
}

QJsonArray WebApiController::buildDeviceStatusArray(const AppHealth& health) const
{
    QJsonArray arr;
    bool wmcMerged = false;

    auto endpointOpenFor = [&health](const QString& deviceId, const QString& endpointId) -> bool {
        const QString ep = resolveDeviceEndpointId(deviceId, endpointId);
        if (ep.isEmpty())
            return false;
        return health.comm.endpointOpen.value(ep, false);
    };

    if (m_deviceReg) {
        for (const auto& d : m_deviceReg->list()) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = d.id;
            dev[QStringLiteral("name")] = d.name;
            dev[QStringLiteral("kind")] = static_cast<int>(d.kind);

            if (m_wmc && mapsToRadarCpu(d.id, d.endpointId)) {
                const QJsonObject runtime = composeRadarDeviceStatus(
                    m_wmc, endpointOpenFor(d.id, d.endpointId));
                dev[QStringLiteral("state")] = runtime[QStringLiteral("state")];
                dev[QStringLiteral("acquisition")] = runtime[QStringLiteral("acquisition")];
                wmcMerged = true;
            } else {
                dev[QStringLiteral("state")] = QStringLiteral("Offline");
                dev[QStringLiteral("acquisition")] = false;
            }
            arr.append(dev);
        }
    }

    if (m_wmc && !wmcMerged) {
        QJsonObject dev;
        dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
        const QJsonObject runtime = composeRadarDeviceStatus(
            m_wmc, endpointOpenFor(QStringLiteral("radar_cpu"), QStringLiteral("radar_cpu")));
        dev[QStringLiteral("state")] = runtime[QStringLiteral("state")];
        dev[QStringLiteral("acquisition")] = runtime[QStringLiteral("acquisition")];
        arr.append(dev);
    }

    return arr;
}

bool WebApiController::mapsToRadarCpu(const QString& deviceId, const QString& endpointId) const
{
    return deviceId == QStringLiteral("radar_cpu")
        || endpointId == QStringLiteral("radar_cpu");
}

QSet<QString> WebApiController::radarCpuDeviceIds() const
{
    QSet<QString> ids;
    ids.insert(QStringLiteral("radar_cpu"));
    if (m_deviceReg) {
        for (const auto& d : m_deviceReg->list()) {
            if (mapsToRadarCpu(d.id, d.endpointId))
                ids.insert(d.id);
        }
    }
    return ids;
}

WebApiController::SingleControllerDeviceResolution
WebApiController::resolveSingleControllerDeviceIds(const QJsonArray& deviceIds) const
{
    SingleControllerDeviceResolution resolution;
    const auto mappedIds = radarCpuDeviceIds();

    if (deviceIds.isEmpty()) {
        resolution.executedId = QStringLiteral("radar_cpu");
        resolution.hasExecutableTarget = true;
        return resolution;
    }

    for (const auto& v : deviceIds) {
        const QString id = v.toString();
        if (!mappedIds.contains(id)) {
            resolution.rejectedDeviceIds.append(id);
            continue;
        }
        if (resolution.executedId.isEmpty())
            resolution.executedId = id;
        else
            resolution.rejectedDeviceIds.append(id);
    }
    resolution.hasExecutableTarget = !resolution.executedId.isEmpty();
    return resolution;
}

QJsonObject WebApiController::buildSingleControllerCommandResponse(
    const QString& executedId,
    const QJsonArray& rejectedDeviceIds,
    bool cmdAccepted) const
{
    QJsonArray accepted;
    if (cmdAccepted && !executedId.isEmpty())
        accepted.append(executedId);

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("mode"), QStringLiteral("single-controller")},
        {QStringLiteral("controllerId"), QStringLiteral("radar_cpu")},
        {QStringLiteral("acceptedDeviceIds"), accepted},
        {QStringLiteral("rejectedDeviceIds"), rejectedDeviceIds},
    };
}
