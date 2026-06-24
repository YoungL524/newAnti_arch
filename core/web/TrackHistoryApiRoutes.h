#pragma once

namespace web { class WebService; }
class DataStore;

namespace TrackHistoryApiRoutes {

void registerRoutes(web::WebService* ws, DataStore* store);

} // namespace TrackHistoryApiRoutes
