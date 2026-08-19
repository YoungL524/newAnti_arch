#include "TaskApiRoutes.h"
#include "WebService.h"
#include "business/TaskService.h"

#include <QJsonArray>

namespace {

/**
 * @brief 复用 TaskInfo::toJson 形成单一任务 DTO 序列化入口。
 */
QJsonObject taskToJson(const task::TaskInfo& t)
{
    return t.toJson();
}

/**
 * @brief 生成包含 error 字段的统一 JSON 失败响应和调用方指定状态码。
 */
QJsonObject fail(const QString& msg)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}};
}

/**
 * @brief 生成包含 task 对象的成功响应，供创建和重跑接口复用。
 */
QJsonObject okTask(const task::TaskInfo& t)
{
    QJsonObject r;
    r[QStringLiteral("ok")] = true;
    r[QStringLiteral("task")] = taskToJson(t);
    return r;
}

/**
 * @brief 解析 JSON 正文并转换为 TaskInfo；语法错误保持 ok=false。
 */
task::TaskInfo taskFromBody(const QJsonObject& body)
{
    return task::TaskInfo::fromJson(body);
}

/**
 * @brief 在进入 TaskService 前检查创建请求必填名称和设备数组类型。
 */
QString validateCreate(const task::TaskInfo& t)
{
    if (t.name.isEmpty())
        return QStringLiteral("name 不能为空");
    return {};
}

/**
 * @brief 把 TaskService::lastError 转为失败响应，缺少诊断时使用稳定兜底文本。
 */
QString taskError(TaskService* svc, const QString& fallback)
{
    const QString err = svc->lastError();
    return err.isEmpty() ? fallback : err;
}

} // namespace

/**
 * @brief 注册任务 CRUD 和生命周期命令路由，统一完成路径 ID、正文和业务结果映射。
 * @details HTTP 层负责校验任务 ID 和请求字段，TaskService 负责状态机与持久化，避免 Web 路由复制任务生命周期规则。
 */
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
        if (existing->status == QString::fromUtf8(task::kStatusCompleted)
            || existing->status == QString::fromUtf8(task::kStatusArchived))
            return fail(QStringLiteral("已完成或已归档任务的执行配置不可修改"));

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
        else if (action == QStringLiteral("rerun")) {
            const auto rerunTask = taskService->rerun(id);
            if (!rerunTask.has_value())
                return fail(taskError(taskService, QStringLiteral("再次执行任务失败")));
            return okTask(rerunTask.value());
        }
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
