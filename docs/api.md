# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, authentication is required for all API calls. You can authenticate using
basic authentication with the admin username and password.

## CSRF Protection

State-changing API endpoints (POST, DELETE) are protected against Cross-Site Request Forgery (CSRF) attacks.

**For Web Browsers:**
- Requests from same-origin (configured via `csrf_allowed_origins`) are automatically allowed
- Cross-origin requests require a CSRF token

**For Non-Browser Applications:**
- Non-browser clients (e.g. `curl`, scripts, custom apps) are **exempt** from CSRF protection
- CSRF attacks require a browser to silently attach credentials to a cross-origin request — this threat
  does not apply to non-browser clients that explicitly provide credentials with every request
- Requests with no `Origin` or `Referer` header (as is typical for non-browser clients) are automatically
  allowed without a CSRF token

**Example (browser-equivalent cross-origin request):**
```bash
# Get CSRF token
curl -u user:pass https://localhost:47990/api/csrf-token

# Use token in request
curl -u user:pass -H "X-CSRF-Token: your_token_here" \
  -X POST https://localhost:47990/api/restart
```

@htmlonly
<script src="api.js"></script>
@endhtmlonly

## GET /api/csrf-token
@copydoc confighttp::getCSRFToken()

## GET /api/apps
@copydoc confighttp::getApps()

## POST /api/apps
@copydoc confighttp::saveApp()

## POST /api/apps/close
@copydoc confighttp::closeApp()

## DELETE /api/apps/{index}
@copydoc confighttp::deleteApp()

## GET /api/browse
@copydoc confighttp::browseDirectory()

## GET /api/clients/list
@copydoc confighttp::getClients()

## POST /api/clients/unpair
@copydoc confighttp::unpair()

## POST /api/clients/unpair-all
@copydoc confighttp::unpairAll()

## POST /api/clients/update
@copydoc confighttp::updateClient()

## GET /api/config
@copydoc confighttp::getConfig()

## GET /api/configLocale
@copydoc confighttp::getLocale()

## POST /api/config
@copydoc confighttp::saveConfig()

## GET /api/covers/{index}
@copydoc confighttp::getCover()

## POST /api/covers/upload
@copydoc confighttp::uploadCover()

## GET /api/logs
@copydoc confighttp::getLogs()

## POST /api/password
@copydoc confighttp::savePassword()

## POST /api/pin
@copydoc confighttp::savePin()

## POST /api/reset-display-device-persistence
@copydoc confighttp::resetDisplayDevicePersistence()

## POST /api/reset-portal-token
@copydoc confighttp::resetPortalToken()

## POST /api/restart
@copydoc confighttp::restart()

## GET /api/sessions
@copydoc confighttp::getSessions()

## GET /api/sessions/history
@copydoc confighttp::getSessionHistory()

## GET /api/virtual-input/status
@copydoc confighttp::getVirtualInputStatus()

## Nova client API

Paired devices (Nebula, or any client that sends its paired client certificate) call these on the
GameStream HTTPS port (47984 by default). No web UI login is involved; each device only sees what its
permissions allow, and devices without "launch apps" only see the running app.

| Request | Reply |
|---|---|
| `GET /nova/v1/capabilities` | `{"nova": true, "version": "<version>", "features": ["apps", "art", "details", "display_mode", "bitrate", "sessions"]}` |
| `GET /nova/v1/apps` | `[{"id", "index", "appid", "name", "running", "source", "has": {"poster", "hero", "logo", "icon"}, "last_played", "playtime_s", "mode_default"}]` — `id` is stable across reorders and renames of imported games; `appid` is the GameStream id to pass to `/launch`; `mode_default` is `"virtual"`, `"mirror"` or `null` |
| `GET /nova/v1/apps/<id>/art/<poster\|hero\|logo\|icon>` | The image, with `ETag` and `Cache-Control: private, max-age=86400` (`304` on `If-None-Match`) |
| `GET /nova/v1/apps/<id>/details` | `{"id", "description", "genres", "developer", "publisher", "release_date", "screenshots", "metacritic", "steam_appid", "last_played", "playtime_s", "last_session": {"device", "resolution", "fps", "codec"}}` — store fields come from Steam's `appdetails` API (fetched once, cached 30 days, HTML stripped); `screenshots` are host-proxied paths |
| `GET /nova/v1/apps/<id>/screenshot/<n>` | A store screenshot, downloaded once and re-encoded to JPEG on the host |
| `GET /bitrate?bitrate=<kbps>` | Changes the caller's running stream bitrate (Sunshine-Foundation compatible). XML `<root status_code="200"><bitrate>1</bitrate><applied_kbps>…</applied_kbps></root>`; `0` when the device isn't streaming. Clamped to 500 kbps – `max_bitrate` (or 800 Mbps) |

`/launch` and `/resume` also accept `nova_display=virtual|mirror`. Nova exports it to prep commands as
`NOVA_DISPLAY_MODE` (when neither the client nor the app's `nova-display-mode` sets one, the variable is unset).
Playtime and last played come from `app_stats.json` next to the state file.
