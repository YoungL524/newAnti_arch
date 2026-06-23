#pragma once

class AppController;

namespace web { class WebService; }

namespace EndpointApiRoutes {

void registerRoutes(web::WebService* ws, AppController* controller);

} // namespace EndpointApiRoutes
