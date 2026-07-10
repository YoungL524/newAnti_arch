#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>
#include <optional>

class DataStore;
class DeviceRegistry;

namespace task {

inline constexpr char kStatusDraft[]     = "Draft";
inline constexpr char kStatusActive[]    = "Active";
inline constexpr char kStatusCompleted[] = "Completed";
inline constexpr char kStatusArchived[]  = "Archived";

struct TaskInfo {
    QString id;
    QString name;
    QString description;
    QString operatorName;
    QString status = QString::fromUtf8(kStatusDraft);
    QStringList deviceIds;
    qint64 createdAt = 0;
    qint64 activatedAt = 0;
    qint64 completedAt = 0;
    qint64 archivedAt = 0;

    QJsonObject toJson() const;
    static TaskInfo fromJson(const QJsonObject& body);
};

/// 当前 active task 的运行时快照（来自内存缓存，热路径只读）
struct ActiveTaskContext {
    QString id;
    QStringList deviceIds;

    bool isEmpty() const { return id.isEmpty(); }
};

} // namespace task

class TaskService {
public:
    explicit TaskService(DataStore* store, DeviceRegistry* deviceRegistry = nullptr);

    bool ensureSchema();
    static QString generateId();
    QString lastError() const { return m_lastError; }

    QVector<task::TaskInfo> list() const;
    std::optional<task::TaskInfo> get(const QString& id) const;
    /// 完整 active task（查库，含 name/status 等元数据）
    std::optional<task::TaskInfo> activeTask() const;
    /// 当前 active task 快照（内存缓存，热路径）
    std::optional<task::ActiveTaskContext> getActiveTask() const;
    bool isDeviceInActiveTask(const QString& deviceId) const;
    /// deviceId 属于当前 active task 时返回 taskId，否则空
    QString resolveTaskIdForDevice(const QString& deviceId) const;
    /// @deprecated 请用 resolveTaskIdForDevice
    QString resolveActiveTaskIdForDevice(const QString& deviceId) const;
    void refreshActiveTaskCache();
    std::optional<task::TaskInfo> create(task::TaskInfo task);
    bool update(const task::TaskInfo& task);
    bool updateDevices(const QString& id, const QStringList& deviceIds);
    /// 将设备并入当前 active task（去重）；无 active task 时 no-op
    bool mergeActiveTaskDevices(const QStringList& deviceIds);
    /// Draft -> Active only. Tasks are one-shot: Completed/Archived cannot re-activate;
    /// use create + activate or a future clone/reopen for re-execution.
    bool activate(const QString& id);
    bool complete(const QString& id);
    bool archive(const QString& id);
    bool remove(const QString& id);

private:
    task::TaskInfo rowToTask(const QVariantMap& row) const;
    bool saveDeviceIds(const QString& taskId, const QStringList& deviceIds);
    bool validateDeviceIds(const QStringList& deviceIds);
    bool isTerminalStatus(const QString& status) const;
    void clearError();
    void setError(const QString& msg);

    struct ActiveTaskCache {
        QString taskId;
        QSet<QString> deviceIds;
    };

    DataStore* m_store = nullptr;
    DeviceRegistry* m_deviceReg = nullptr;
    QString m_lastError;
    ActiveTaskCache m_activeTaskCache;
};
