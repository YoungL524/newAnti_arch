/**
 * @file SystemEventApiRoutes.h
 * @brief 注册 system_events 的只读分页查询路由。
 * @details 路由使用绑定参数过滤任务、设备、类别和时间范围，不允许客户端提供 SQL 标识符。
 */

#pragma once

namespace web { class WebService; }
class DataStore;

namespace SystemEventApiRoutes {

void registerRoutes(web::WebService* ws, DataStore* store);

} // namespace SystemEventApiRoutes
