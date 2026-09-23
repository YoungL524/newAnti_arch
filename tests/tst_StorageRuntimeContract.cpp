/**
 * @file tst_StorageRuntimeContract.cpp
 * @brief 验证存储组合根对外提供的设备与任务 V1 JSON 字段合同。
 * @details 测试使用临时 SQLite 和真实 Service Owner，不启动 Socket；用于防止旧的 id/name/lng 字段或
 * 数值 revision 再次进入 HTTP 边界，不验证实时 Topic、Command 控制平面或浏览器行为。
 */

#include "core/app/StorageRuntime.h"
#include "core/datadomain/CoreDataDomain.h"
#include "core/model/StableIdRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

/** @brief 汇总设备与任务低频网络 DTO 的稳定字段验收。 */
class StorageRuntimeContractTest final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 验收规范字段名、字符串 revision、单设备查询和 taskId 响应。 */
    void exposesFrozenV1JsonFields();
};

/**
 * @brief 使用数据库预置台账完成设备查询、revision 更新及任务创建，拒绝旧短字段重新出现。
 */
void StorageRuntimeContractTest::exposesFrozenV1JsonFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::model::StableIdRegistry stableIds;
    core::domain::CoreDataDomain domain(32, 32);
    core::app::StorageRuntime runtime(
        directory.filePath(QStringLiteral("contract.db")), QStringLiteral("test-1.0"),
        stableIds, domain.trackTopic(), domain.eventTopic(), domain.taskState());
    QVERIFY2(runtime.initialize(), qPrintable(runtime.lastError()));

    const QJsonObject list = QJsonDocument::fromJson(runtime.listDevices()).object();
    QVERIFY(list.value(QStringLiteral("ok")).toBool());
    const QJsonArray devices = list.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 2);
    const QJsonObject device = devices.first().toObject();
    QVERIFY(device.value(QStringLiteral("deviceId")).isString());
    QVERIFY(device.value(QStringLiteral("revision")).isString());
    bool revisionOk = false;
    const quint32 originalRevision = device.value(QStringLiteral("revision"))
                                         .toString().toUInt(&revisionOk);
    QVERIFY(revisionOk);
    QVERIFY(device.contains(QStringLiteral("displayName")));
    QVERIFY(device.contains(QStringLiteral("longitudeDeg")));
    QVERIFY(device.contains(QStringLiteral("latitudeDeg")));
    QVERIFY(device.value(QStringLiteral("faces")).isArray());
    const QJsonObject firstFace = device.value(QStringLiteral("faces")).toArray().first().toObject();
    QVERIFY(firstFace.contains(QStringLiteral("headingDeg")));
    QVERIFY(firstFace.contains(QStringLiteral("pitchDeg")));
    QVERIFY(firstFace.contains(QStringLiteral("rollDeg")));
    QCOMPARE(device.value(QStringLiteral("longitudeDeg")).toDouble(), 118.65303);
    QCOMPARE(device.value(QStringLiteral("latitudeDeg")).toDouble(), 31.93494);
    QCOMPARE(device.value(QStringLiteral("altitudeM")).toDouble(), 63.9288);
    QVERIFY(!device.contains(QStringLiteral("id")));
    QVERIFY(!device.contains(QStringLiteral("name")));
    QVERIFY(!device.contains(QStringLiteral("lng")));
    QVERIFY(!device.contains(QStringLiteral("lat")));
    QVERIFY(!device.contains(QStringLiteral("headingDeg")));
    QVERIFY(!device.contains(QStringLiteral("absoluteHeadingDeg")));
    QVERIFY(!device.contains(QStringLiteral("pitchDeg")));
    QVERIFY(!device.contains(QStringLiteral("rollDeg")));

    const QString deviceId = device.value(QStringLiteral("deviceId")).toString();
    const QJsonObject single = QJsonDocument::fromJson(runtime.getDevice(deviceId)).object();
    QCOMPARE(single.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("device")).toObject(), device);

    QJsonObject update = device;
    update.insert(QStringLiteral("expectedRevision"), device.value(QStringLiteral("revision")));
    update.insert(QStringLiteral("changedBy"), QStringLiteral("contract-test"));
    update.insert(QStringLiteral("changeReason"), QStringLiteral("验证字符串revision"));
    const QJsonObject updated = QJsonDocument::fromJson(
        runtime.updateDevice(deviceId, QJsonDocument(update).toJson(QJsonDocument::Compact))).object();
    QVERIFY(updated.value(QStringLiteral("ok")).toBool());
    QCOMPARE(updated.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("device")).toObject()
                 .value(QStringLiteral("revision")).toString(),
             QString::number(originalRevision + 1U));

    const QJsonObject templateResponse = QJsonDocument::fromJson(runtime.listTaskTemplates()).object();
    QVERIFY(templateResponse.value(QStringLiteral("ok")).toBool());
    const QJsonArray templates = templateResponse.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("templates")).toArray();
    QCOMPARE(templates.size(), 1);
    const QJsonObject controlTemplate = templates.at(0).toObject();
    QVERIFY(controlTemplate.value(QStringLiteral("controlSettings")).isObject());
    const QString templateId = controlTemplate.value(QStringLiteral("templateId")).toString();
    QVERIFY(!templateId.isEmpty());

    const QJsonObject taskRequest {
        {QStringLiteral("name"), QStringLiteral("contract-task")},
        {QStringLiteral("operatorName"), QStringLiteral("tester")},
        {QStringLiteral("description"), QStringLiteral("V1字段验收")},
        {QStringLiteral("sourceTemplateId"), templateId},
        {QStringLiteral("radarArrayType"), device.value(QStringLiteral("arrayType"))},
        {QStringLiteral("deviceIds"), QJsonArray {deviceId}},
        {QStringLiteral("deviceFaceMasks"), QJsonObject {{deviceId, 1}}}
    };
    const QJsonObject created = QJsonDocument::fromJson(runtime.createTask(
        QJsonDocument(taskRequest).toJson(QJsonDocument::Compact))).object();
    QVERIFY(created.value(QStringLiteral("ok")).toBool());
    const QJsonObject task = created.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("task")).toObject();
    QVERIFY(task.value(QStringLiteral("taskId")).isString());
    QVERIFY(!task.contains(QStringLiteral("id")));
    QCOMPARE(task.value(QStringLiteral("status")).toString(), QStringLiteral("Draft"));
    QCOMPARE(task.value(QStringLiteral("sourceTemplateId")).toString(), templateId);
    QCOMPARE(task.value(QStringLiteral("controlSettings")).toObject(),
             controlTemplate.value(QStringLiteral("controlSettings")).toObject());

    const QString taskId = task.value(QStringLiteral("taskId")).toString();
    const QJsonObject activated = QJsonDocument::fromJson(runtime.activateTask(taskId)).object();
    QVERIFY(activated.value(QStringLiteral("ok")).toBool());
    QCOMPARE(activated.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("task")).toObject()
                 .value(QStringLiteral("status")).toString(), QStringLiteral("Ready"));

    QVERIFY2(runtime.shutdown(), qPrintable(runtime.lastError()));
}

/**
 * @brief 使用非 GUI QtTest 入口运行临时 SQLite 合同验收。
 */
QTEST_GUILESS_MAIN(StorageRuntimeContractTest)

#include "tst_StorageRuntimeContract.moc"
