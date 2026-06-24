#pragma once

namespace web { class WebService; }
class DataStore;

namespace SystemEventApiRoutes {

void registerRoutes(web::WebService* ws, DataStore* store);

} // namespace SystemEventApiRoutes
