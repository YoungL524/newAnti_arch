#include <QtTest>

#include "DataStore.h"
#include "DeviceRegistry.h"
#include "TaskService.h"

class TestTaskService : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_store = new DataStore;
        QVERIFY(m_store->connectToSqlite(QStringLiteral(":memory:")));

        m_devices = new DeviceRegistry(m_store);
        QVERIFY(m_devices->ensureSchema());

        m_tasks = new TaskService(m_store, m_devices);
        QVERIFY(m_tasks->ensureSchema());
        m_tasks->refreshActiveTaskCache();
    }

    void cleanup()
    {
        delete m_tasks;
        delete m_devices;
        delete m_store;
        m_tasks = nullptr;
        m_devices = nullptr;
        m_store = nullptr;
    }

    void completedTaskConfigurationIsImmutable()
    {
        createRadar(QStringLiteral("radar_1"));
        const auto created = createTask(QStringLiteral("task_1"), {QStringLiteral("radar_1")});
        QVERIFY(created.has_value());
        QVERIFY(m_tasks->activate(created->id));
        QVERIFY(m_tasks->complete(created->id));

        auto changed = m_tasks->get(created->id).value();
        changed.name = QStringLiteral("changed");
        QVERIFY(!m_tasks->update(changed));
        QCOMPARE(m_tasks->get(created->id)->name, QStringLiteral("task_1"));
    }

    void deviceReferencedByCompletedTaskCannotBeDeleted()
    {
        createRadar(QStringLiteral("radar_1"));
        const auto created = createTask(QStringLiteral("task_1"), {QStringLiteral("radar_1")});
        QVERIFY(created.has_value());
        QVERIFY(m_tasks->activate(created->id));
        QVERIFY(m_tasks->complete(created->id));

        QVERIFY(!m_devices->remove(QStringLiteral("radar_1")));
        QVERIFY(m_devices->lastError().contains(created->id));
        QVERIFY(m_devices->get(QStringLiteral("radar_1")).has_value());
    }

    void activatingSecondTaskDoesNotCompleteCurrentTask()
    {
        createRadar(QStringLiteral("radar_1"));
        const auto first = createTask(QStringLiteral("task_1"), {QStringLiteral("radar_1")});
        const auto second = createTask(QStringLiteral("task_2"), {QStringLiteral("radar_1")});
        QVERIFY(first.has_value());
        QVERIFY(second.has_value());
        QVERIFY(m_tasks->activate(first->id));

        QVERIFY(!m_tasks->activate(second->id));
        QCOMPARE(m_tasks->get(first->id)->status, QString::fromUtf8(task::kStatusActive));
        QCOMPARE(m_tasks->get(second->id)->status, QString::fromUtf8(task::kStatusDraft));
        QCOMPARE(m_tasks->getActiveTask()->id, first->id);
    }

    void activeTaskEndpointCannotBeUnboundOrStolen()
    {
        createRadar(QStringLiteral("radar_1"));
        const auto task = createTask(QStringLiteral("task_1"), {QStringLiteral("radar_1")});
        QVERIFY(task.has_value());
        QVERIFY(m_tasks->activate(task->id));

        auto activeRadar = m_devices->get(QStringLiteral("radar_1")).value();
        activeRadar.endpointId.clear();
        QVERIFY(!m_devices->update(activeRadar));
        QVERIFY(!m_tasks->updateDevices(task->id, QStringList{}));

        DeviceInfo other;
        other.id = QStringLiteral("radar_2");
        other.name = QStringLiteral("radar_2");
        other.kind = DeviceKind::Radar;
        other.endpointId = QStringLiteral("radar_cpu");
        QVERIFY(!m_devices->create(other));
        QCOMPARE(m_devices->get(QStringLiteral("radar_1"))->endpointId,
                 QStringLiteral("radar_cpu"));
    }

    void radarWithLinkedSubdeviceCannotBeDeleted()
    {
        createRadar(QStringLiteral("radar_1"));

        DeviceInfo child;
        child.id = QStringLiteral("opto_1");
        child.name = QStringLiteral("opto_1");
        child.kind = DeviceKind::Opto;
        child.linkedRadarId = QStringLiteral("radar_1");
        QVERIFY(m_devices->create(child));

        QVERIFY(!m_devices->remove(QStringLiteral("radar_1")));
        QVERIFY(m_devices->lastError().contains(QStringLiteral("opto_1")));
    }

    void deviceUpdatePreservesBackendManagedFields()
    {
        createRadar(QStringLiteral("radar_1"));
        const auto before = m_devices->get(QStringLiteral("radar_1")).value();

        auto edited = before;
        edited.name = QStringLiteral("renamed");
        edited.createdAt = 0;
        edited.status = DeviceStatus::Fault;
        QVERIFY(m_devices->update(edited));

        const auto after = m_devices->get(QStringLiteral("radar_1")).value();
        QCOMPARE(after.name, QStringLiteral("renamed"));
        QCOMPARE(after.createdAt, before.createdAt);
        QCOMPARE(static_cast<int>(after.status), static_cast<int>(before.status));
    }

    void deletingDraftOnlyDeviceRemovesDraftBinding()
    {
        createRadar(QStringLiteral("radar_1"));
        const auto created = createTask(QStringLiteral("task_1"), {QStringLiteral("radar_1")});
        QVERIFY(created.has_value());

        QVERIFY(m_devices->remove(QStringLiteral("radar_1")));
        QVERIFY(!m_devices->get(QStringLiteral("radar_1")).has_value());
        QVERIFY(m_tasks->get(created->id)->deviceIds.isEmpty());
    }

private:
    void createRadar(const QString& id)
    {
        DeviceInfo radar;
        radar.id = id;
        radar.name = id;
        radar.kind = DeviceKind::Radar;
        radar.endpointId = QStringLiteral("radar_cpu");
        QVERIFY(m_devices->create(radar));
    }

    std::optional<task::TaskInfo> createTask(const QString& name,
                                              const QStringList& deviceIds)
    {
        task::TaskInfo task;
        task.name = name;
        task.deviceIds = deviceIds;
        return m_tasks->create(task);
    }

    DataStore* m_store = nullptr;
    DeviceRegistry* m_devices = nullptr;
    TaskService* m_tasks = nullptr;
};

QTEST_MAIN(TestTaskService)
#include "tst_TaskService.moc"
