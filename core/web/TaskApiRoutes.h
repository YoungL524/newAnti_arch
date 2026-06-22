#pragma once

class TaskService;

namespace web { class WebService; }

namespace TaskApiRoutes {

void registerRoutes(web::WebService* ws, TaskService* taskService);

} // namespace TaskApiRoutes
