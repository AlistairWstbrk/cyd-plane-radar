#pragma once

class WiFiManager;

namespace web {

/** <style>/<script> injected into every WiFiManager page (restyled portal). */
const char* portalHeadHtml();
/** Extra buttons on the portal home page. */
const char* portalMenuHtml();
/** Register /settings, /airports, /live and the JSON API on WiFiManager's server. */
void registerRoutes(WiFiManager& wm);

}  // namespace web
