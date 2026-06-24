#include "TaskApiRoutes.h"
#include "WebService.h"
#include "business/TaskService.h"

#include <QJsonArray>

namespace {

QJsonObject taskToJson(const task::TaskInfo& t)
{
    return t.toJson();
}

QJsonObject fail(const QString& msg)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}};
}

QJsonObject okTask(const task::TaskInfo& t)
{
    QJsonObject r;
    r[QStringLiteral("ok")] = true;
    r[QStringLiteral("task")] = taskToJson(t);
    return r;
}

task::TaskInfo taskFromBody(const QJsonObject& body)
{
    return task::TaskInfo::fromJson(body);
}

QString validateCreate(const task::TaskInfo& t)
{
    if (t.name.isEmpty())
        return QStringLiteral("name 不能为空");
    return {};
}

QString taskError(TaskService* svc, const QString& fallback)
{
    const QString err = svc->lastError();
    return err.isEmpty() ? fallback : err;
}

} // namespace

void TaskApiRoutes::registerRoutes(web::WebService* ws, TaskService* taskService)
{
    if (!ws || !taskService) return;

    ws->handleGet(QStringLiteral("/api/tasks"), [taskService](const QJsonObject&, const QString&) {
        QJsonArray arr;
        for (const auto& t : taskService->list())
            arr.append(taskToJson(t));
        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        r[QStringLiteral("tasks")] = arr;
        return r;
    });

    ws->handleGet(QStringLiteral("/api/tasks/active"), [taskService](const QJsonObject&, const QString&) {
        auto active = taskService->activeTask();
        QJsonObject r;
        r[QStringLiteral("ok")] = true;
        if (active.has_value()) {
            r[QStringLiteral("activeTaskId")] = active->id;
            r[QStringLiteral("task")] = taskToJson(active.value());
        } else {
            r[QStringLiteral("activeTaskId")] = QJsonValue::Null;
        }
        return r;
    });

    ws->handlePost(QStringLiteral("/api/tasks"), [taskService](const QJsonObject& body) {
        auto t = taskFromBody(body);
        QString err = validateCreate(t);
        if (!err.isEmpty()) return fail(err);
        auto created = taskService->create(t);
        if (!created.has_value())
            return fail(taskError(taskService, QStringLiteral("创建任务失败")));
        return okTask(created.value());
    });

    ws->handleGetPrefix(QStringLiteral("/api/tasks/"), [taskService](const QString& suffix, const QJsonObject&) {
        if (suffix.isEmpty() || suffix.contains(QChar('/')))
            return fail(QStringLiteral("无效路径"));
        auto t = taskService->get(suffix);
        if (!t.has_value())
            return fail(QStringLiteral("任务不存在"));
        return okTask(t.value());
    });

    ws->handlePutPrefix(QStringLiteral("/api/tasks/"), [taskService](const QString& suffix, const QJsonObject& body) {
        if (suffix.isEmpty() || suffix.contains(QChar('/')))
            return fail(QStringLiteral("无效路径"));

        auto existing = taskService->get(suffix);
        if (!existing.has_value())
            return fail(QStringLiteral("任务不存在"));
        if (existing->status == QString::fromUtf8(task::kStatusArchived))
            return fail(QStringLiteral("invalid task status transition"));

        task::TaskInfo t = existing.value();
        t.id = suffix;
        if (body.contains(QStringLiteral("name")))
            t.name = body[QStringLiteral("name")].toString();
        if (body.contains(QStringLiteral("description")))
            t.description = body[QStringLiteral("description")].toString();
        if (body.contains(QStringLiteral("operatorName")))
            t.operatorName = body[QStringLiteral("operatorName")].toString();
        if (body.contains(QStringLiteral("deviceIds")))
            t.deviceIds = taskFromBody(body).deviceIds;

        if (!taskService->update(t))
            return fail(taskError(taskService, QStringLiteral("更新任务失败")));
        auto updated = taskService->get(suffix);
        if (!updated.has_value())
            return fail(QStringLiteral("更新后读取失败"));
        return okTask(updated.value());
    });

    ws->handlePostPrefix(QStringLiteral("/api/tasks/"), [taskService](const QString& suffix, const QJsonObject&) {
        const int slash = suffix.indexOf(QChar('/'));
        if (slash <= 0)
            return fail(QStringLiteral("无效路径"));
        const QString id = suffix.left(slash);
        const QString action = suffix.mid(slash + 1);

        if (!taskService->get(id).has_value())
            return fail(QStringLiteral("任务不存在"));

        bool ok = false;
        if (action == QStringLiteral("activate"))
            ok = taskService->activate(id);
        else if (action == QStringLiteral("complete"))
            ok = taskService->complete(id);
        else if (action == QStringLiteral("archive"))
            ok = taskService->archive(id);
        else
            return fail(QStringLiteral("未知操作"));

        if (!ok)
            return fail(taskError(taskService, QStringLiteral("操作失败")));
        auto t = taskService->get(id);
        if (!t.has_value())
            return fail(QStringLiteral("操作后读取失败"));
        return okTask(t.value());
    });

    ws->handleDeletePrefix(QStringLiteral("/api/tasks/"), [taskService](const QString& suffix, const QJsonObject&) {
        if (suffix.isEmpty() || suffix.contains(QChar('/')))
            return fail(QStringLiteral("无效路径"));
        if (!taskService->remove(suffix))
            return fail(taskError(taskService, QStringLiteral("删除任务失败")));
        return QJsonObject{{QStringLiteral("ok"), true}};
    });
}
