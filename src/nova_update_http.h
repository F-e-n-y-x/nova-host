/**
 * @file src/nova_update_http.h
 * @brief Web UI routes for the Nova update check and installer.
 */
#pragma once

// local includes
#include "confighttp.h"

namespace nova_update {
  /**
   * @brief Register GET /api/update/status and POST /api/update/install on the admin server.
   *
   * @param server The web UI HTTPS server.
   */
  void register_routes(confighttp::https_server_t &server);
}  // namespace nova_update
