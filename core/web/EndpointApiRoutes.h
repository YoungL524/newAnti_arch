/**
 * @file EndpointApiRoutes.h
 * @brief 注册通信端点配置读取、单项更新和完整热重载 REST 路由。
 * @details 实际校验、设备绑定保护、运行时回滚和文件持久化由 AppController 统一完成。
 */

#pragma once

class AppController;

namespace web { class WebService; }

namespace EndpointApiRoutes {

void registerRoutes(web::WebService* ws, AppController* controller);

} // namespace EndpointApiRoutes
