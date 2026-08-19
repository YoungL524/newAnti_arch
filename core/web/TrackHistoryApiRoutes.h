/**
 * @file TrackHistoryApiRoutes.h
 * @brief 注册按任务、设备和时间窗口查询已归档权威航迹的 REST 路由。
 * @details 历史查询直接读取 tracks 表分页结果，与实时 WebSocket 显示投影相互独立。
 */

#pragma once

namespace web { class WebService; }
class DataStore;

namespace TrackHistoryApiRoutes {

void registerRoutes(web::WebService* ws, DataStore* store);

} // namespace TrackHistoryApiRoutes
