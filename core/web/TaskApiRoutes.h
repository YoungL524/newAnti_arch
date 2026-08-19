/**
 * @file TaskApiRoutes.h
 * @brief 注册任务创建、修改、启动、完成、归档、重跑和删除的 REST 路由。
 * @details 路由层只解析 HTTP/JSON 并映射状态码，生命周期规则和事务由 TaskService 执行。
 */

#pragma once

class TaskService;

namespace web { class WebService; }

namespace TaskApiRoutes {

void registerRoutes(web::WebService* ws, TaskService* taskService);

} // namespace TaskApiRoutes
