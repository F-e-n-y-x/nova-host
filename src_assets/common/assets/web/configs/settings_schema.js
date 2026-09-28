/**
 * @file Declarative description of every host setting shown on the Settings page.
 *
 * Defaults live in config_tabs.json (checked against config.cpp and the docs by
 * ConfigConsistencyTest); this file adds how each option is presented: control type,
 * choices, units, limits, which platforms show it and when it applies. Labels and
 * descriptions come from en.json `config.<key>` / `config.<key>_desc`.
 *
 * Option fields:
 * - type: 'bool' | 'choice' | 'number' | 'text' | 'path' | 'secret' | custom component name
 * - picker: 'displays' | 'sinks' — offer the host's list (GET /api/displays, /api/audio/sinks)
 *   as choices; falls back to the text field when the host doesn't provide it
 * - secret: the host returns "********" for a stored value and keeps it when saved back
 * - choices: [{ value, label }] — label is an i18n key, or `{ text }` for a literal
 * - platforms: only on these platforms; hideOn: never on these platforms
 * - when(config, platform): only while this returns true (dependent options)
 * - unit, min, max, step, placeholder, mono, integer
 * - descKeys: description keys instead of `config.<key>_desc`; platformDesc: pick
 *   `config.<key>_desc_<platform>` (unix fallback) like platform-i18n.js
 * - help: id of an extra help block shown under "More" (see SettingHelp.vue)
 * - warn(config, platform): i18n key of a warning to show, or null
 */

/** @typedef {{value: string|number, label?: string, text?: string}} Choice */

const on = (value) => ['enabled', 'true', '1', 'yes', 'on', true, 1].includes(typeof value === 'string' ? value.toLowerCase() : value)

const LOCALES = [
  ['bg', 'Български (Bulgarian)'], ['cs', 'Čeština (Czech)'], ['de', 'Deutsch (German)'], ['en', 'English'],
  ['en_GB', 'English, United Kingdom'], ['en_US', 'English, United States'], ['es', 'Español (Spanish)'],
  ['fr', 'Français (French)'], ['hu', 'Magyar (Hungarian)'], ['it', 'Italiano (Italian)'], ['ja', '日本語 (Japanese)'],
  ['ko', '한국어 (Korean)'], ['pl', 'Polski (Polish)'], ['pt', 'Português (Portuguese)'],
  ['pt_BR', 'Português, Brasileiro (Portuguese, Brazilian)'], ['ru', 'Русский (Russian)'], ['sv', 'svenska (Swedish)'],
  ['tr', 'Türkçe (Turkish)'], ['uk', 'Українська (Ukranian)'], ['vi', 'Tiếng Việt (Vietnamese)'],
  ['zh', '简体中文 (Chinese Simplified)'], ['zh_TW', '繁體中文 (Chinese Traditional)'],
].map(([value, text]) => ({ value, text }))

const numbered = (key, count) => Array.from({ length: count }, (_, i) => ({ value: String(i), label: `config.${key}_${i}` }))
const named = (key, values) => values.map((value) => ({ value, label: `config.${key}_${value}` }))
const CODER = [
  { value: 'auto', label: 'config.ffmpeg_auto' },
  { value: 'cabac', label: 'config.coder_cabac' },
  { value: 'cavlc', label: 'config.coder_cavlc' },
]

const CAPTURE = {
  linux: [['nvfbc', 'NvFBC'], ['wlr', 'wlroots'], ['kms', 'KMS'], ['x11', 'X11'], ['kwin', 'KWin Screencast'], ['portal', 'XDG Portal']],
  freebsd: [['wlr', 'wlroots'], ['x11', 'X11'], ['portal', 'XDG Portal']],
  windows: [['ddx', 'Desktop Duplication API'], ['wgc', 'Windows.Graphics.Capture (beta)']],
  macos: [],
}

const ENCODERS = {
  windows: [['nvenc', 'NVIDIA NVENC'], ['quicksync', 'Intel QuickSync'], ['amdvce', 'AMD AMF/VCE']],
  freebsd: [['vulkan', 'Vulkan'], ['vaapi', 'VA-API']],
  linux: [['nvenc', 'NVIDIA NVENC'], ['vaapi', 'VA-API'], ['vulkan', 'Vulkan']],
  macos: [['videotoolbox', 'VideoToolbox']],
}

const GAMEPADS = ['generic', 'x360', 'xone', 'xseries', 'ds4', 'ds5', 'switch']
const VIGEMBUS_GAMEPADS = new Set(['auto', 'x360', 'ds4'])

const literal = (pairs) => pairs.map(([value, text]) => ({ value, text }))
const controllerOn = (c) => on(c.controller)
const keyboardOn = (c) => on(c.keyboard)
const mouseOn = (c) => on(c.mouse)
const ddOn = (c) => c.dd_configuration_option !== 'disabled'

/** Artwork sources in `art_source_priority` (the host's known_art_sources). */
export const ART_SOURCES = ['steam', 'steamgriddb', 'lutris', 'igdb']
/**
 * Sources listed in `art_source_priority`, in order (unknown names and repeats dropped).
 *
 * @param {string} value Comma-separated list.
 * @returns {string[]} Known sources, best first.
 */
export function parseArtSources(value) {
  const out = []
  for (const name of String(value ?? '').split(',')) {
    const source = name.trim().toLowerCase()
    if (ART_SOURCES.includes(source) && !out.includes(source)) out.push(source)
  }
  return out
}
const artSourceOn = (c, source) => parseArtSources(c.art_source_priority).includes(source)
const sgdbOn = (c) => artSourceOn(c, 'steamgriddb')
const steamDetailsOn = (c) => on(c.metadata_steam)
const SGDB_POSTER_STYLES = ['alternate', 'blurred', 'white_logo', 'material', 'no_logo']
const SGDB_HERO_STYLES = ['alternate', 'blurred', 'material']
/** Steam store languages (`l=` values) with their English names. */
const STEAM_LANGUAGES = [
  ['english', 'English'], ['arabic', 'Arabic'], ['bulgarian', 'Bulgarian'], ['schinese', 'Chinese (Simplified)'],
  ['tchinese', 'Chinese (Traditional)'], ['czech', 'Czech'], ['danish', 'Danish'], ['dutch', 'Dutch'],
  ['finnish', 'Finnish'], ['french', 'French'], ['german', 'German'], ['greek', 'Greek'], ['hungarian', 'Hungarian'],
  ['indonesian', 'Indonesian'], ['italian', 'Italian'], ['japanese', 'Japanese'], ['koreana', 'Korean'],
  ['norwegian', 'Norwegian'], ['polish', 'Polish'], ['portuguese', 'Portuguese'], ['brazilian', 'Portuguese (Brazil)'],
  ['romanian', 'Romanian'], ['russian', 'Russian'], ['spanish', 'Spanish (Spain)'], ['latam', 'Spanish (Latin America)'],
  ['swedish', 'Swedish'], ['thai', 'Thai'], ['turkish', 'Turkish'], ['ukrainian', 'Ukrainian'], ['vietnamese', 'Vietnamese'],
]

/** Options, keyed by config name. */
export const OPTIONS = {
  // General
  locale: { type: 'choice', choices: LOCALES },
  sunshine_name: { type: 'text', placeholder: 'Nova' },
  min_log_level: { type: 'choice', choices: numbered('min_log_level', 7) },
  notify_pre_releases: { type: 'bool' },
  system_tray: { type: 'bool' },
  global_prep_cmd: { type: 'PrepCommandsEditor' },

  // Host control (Nova)
  pcsleep_enabled: { type: 'bool', platforms: ['linux'] },
  host_commands: { type: 'HostCommandsEditor', platforms: ['linux'] },

  // Windows games (Linux)
  windows_launcher: { type: 'choice', hideOn: ['windows', 'macos'], choices: named('windows_launcher', ['proton', 'wine', 'custom']) },
  proton_auto_update: { type: 'bool', hideOn: ['windows', 'macos'], when: (c) => (c.windows_launcher || 'proton') === 'proton' },
  windows_exe_launcher: {
    type: 'text', mono: true, hideOn: ['windows', 'macos'], placeholder: 'umu-run {exe}', when: (c) => c.windows_launcher === 'custom',
  },

  // Library & artwork
  metadata_steam: { type: 'bool', group: 'metadata_details' },
  metadata_language: { type: 'choice', group: 'metadata_details', when: steamDetailsOn, choices: literal(STEAM_LANGUAGES) },
  metadata_auto_fetch: { type: 'bool', group: 'metadata_details' },
  metadata_ttl_days: { type: 'number', group: 'metadata_details', unit: 'days', min: 1, max: 365, integer: true },
  art_source_priority: { type: 'ArtSourcesEditor', group: 'metadata_artwork' },
  art_prefer_official: { type: 'bool', group: 'metadata_artwork', when: (c) => artSourceOn(c, 'steam') },
  art_steamgriddb_poster_style: {
    type: 'choice', group: 'metadata_artwork', when: sgdbOn,
    choices: [{ value: '', label: '_common.auto' }, ...named('art_steamgriddb_style', SGDB_POSTER_STYLES)],
  },
  art_steamgriddb_hero_style: {
    type: 'choice', group: 'metadata_artwork', when: sgdbOn,
    choices: [{ value: '', label: '_common.auto' }, ...named('art_steamgriddb_style', SGDB_HERO_STYLES)],
  },
  art_allow_animated: { type: 'bool', group: 'metadata_artwork', when: sgdbOn },
  art_nsfw: { type: 'bool', group: 'metadata_artwork', when: sgdbOn },
  art_humor: { type: 'bool', group: 'metadata_artwork', when: sgdbOn },
  steamgriddb_api_key: { type: 'secret', mono: true, group: 'metadata_keys', placeholder: 'Paste your API key', help: 'steamgriddb', helpOpen: true },
  igdb_client_id: { type: 'text', mono: true, group: 'metadata_keys', placeholder: 'Twitch client ID', help: 'igdb', helpOpen: true },
  igdb_client_secret: { type: 'secret', mono: true, group: 'metadata_keys', placeholder: 'Twitch client secret' },
  steam_web_api_key: { type: 'secret', mono: true, group: 'metadata_keys', placeholder: 'Paste your API key', help: 'steam_web', helpOpen: true },
  rawg_api_key: { type: 'secret', mono: true, group: 'metadata_keys', placeholder: 'Paste your API key', help: 'rawg', helpOpen: true },

  // Display & capture
  capture: {
    type: 'choice',
    hideOn: ['macos'],
    choices: (platform) => [{ value: '', label: '_common.autodetect' }, ...literal(CAPTURE[platform] || [])],
  },
  capture_pacing: {
    type: 'choice', platforms: ['linux'],
    choices: ['auto', 'vblank', 'timer'].map((value) => ({ value, label: `nova.settings.choices.capture_pacing_${value}` })),
  },
  display_follow: {
    type: 'choice', platforms: ['linux', 'freebsd'],
    choices: ['virtual', 'off'].map((value) => ({ value, label: `nova.settings.choices.display_follow_${value}` })),
  },
  display_follow_cmd: {
    type: 'text', mono: true, platforms: ['linux', 'freebsd'], placeholder: { default: '/usr/local/bin/sunshine-resolution.sh' },
    when: (c) => c.display_follow !== 'off',
  },
  virtual_display: {
    type: 'choice', platforms: ['linux'],
    choices: ['headless_x', 'off'].map((value) => ({ value, label: `nova.settings.choices.virtual_display_${value}` })),
    when: (c) => c.display_follow !== 'off',
  },
  virtual_display_wm: {
    type: 'text', mono: true, platforms: ['linux'], placeholder: { default: 'openbox' },
    when: (c) => c.display_follow !== 'off' && c.virtual_display !== 'off',
  },
  virtual_display_session: {
    type: 'choice', platforms: ['linux'],
    choices: ['desktop', 'bare'].map((value) => ({ value, label: `nova.settings.choices.virtual_display_session_${value}` })),
    when: (c) => c.display_follow !== 'off' && c.virtual_display !== 'off',
  },
  virtual_display_linger: {
    type: 'number', unit: 's', min: 0, max: 3600, integer: true, platforms: ['linux'],
    when: (c) => c.display_follow !== 'off',
  },
  virtual_display_fps_cap: {
    type: 'number', unit: 'fps', min: -1, max: 1000, integer: true, platforms: ['linux'],
    when: (c) => c.display_follow !== 'off' && c.virtual_display !== 'off',
  },
  adapter_name: {
    type: 'text', mono: true, hideOn: ['macos'],
    placeholder: { windows: 'Radeon RX 580 Series', default: '/dev/dri/renderD128' },
    descKeys: { windows: ['config.adapter_name_desc_windows'], default: ['config.adapter_name_desc_linux_1'] },
    help: 'adapter_name',
  },
  output_name: {
    type: 'text', mono: true, picker: 'displays',
    placeholder: { windows: '{de9bb7e2-186e-505b-9e93-f48793333810}', linux: 'DP-0', freebsd: 'DP-0', default: '0' },
    platformDesc: true,
    help: 'output_name',
  },
  max_bitrate: { type: 'number', unit: 'Kbps', min: 0, integer: true },
  minimum_fps_target: { type: 'number', unit: 'fps', min: 0, max: 1000, integer: true },
  max_fps_target: { type: 'number', unit: 'fps', min: 0, max: 1000, integer: true },
  dd_configuration_option: {
    type: 'choice', platforms: ['windows'], group: 'display_device', descKeys: [],
    choices: [
      { value: 'disabled', label: '_common.disabled_def' },
      { value: 'verify_only', label: 'config.dd_config_verify_only' },
      { value: 'ensure_active', label: 'config.dd_config_ensure_active' },
      { value: 'ensure_primary', label: 'config.dd_config_ensure_primary' },
      { value: 'ensure_only_display', label: 'config.dd_config_ensure_only_display' },
    ],
  },
  dd_resolution_option: {
    type: 'choice', platforms: ['windows'], group: 'display_device', when: ddOn,
    descKeys: ['config.dd_resolution_option_ogs_desc'],
    choices: named('dd_resolution_option', ['disabled', 'auto', 'manual']),
  },
  dd_manual_resolution: {
    type: 'text', mono: true, placeholder: '2560x1440', platforms: ['windows'], group: 'display_device', descKeys: [],
    when: (c) => ddOn(c) && c.dd_resolution_option === 'manual',
    validate: (v) => (v === '' || /^\s*\d+\s*x\s*\d+\s*$/i.test(String(v)) ? null : 'nova.settings.errors.resolution'),
  },
  dd_refresh_rate_option: {
    type: 'choice', platforms: ['windows'], group: 'display_device', when: ddOn, descKeys: [],
    choices: named('dd_refresh_rate_option', ['disabled', 'auto', 'manual']),
  },
  dd_manual_refresh_rate: {
    type: 'text', mono: true, placeholder: '59.9558', platforms: ['windows'], group: 'display_device', descKeys: [],
    when: (c) => ddOn(c) && c.dd_refresh_rate_option === 'manual',
    validate: (v) => (v === '' || /^\s*\d+(\.\d+)?\s*$/.test(String(v)) ? null : 'nova.settings.errors.refresh_rate'),
  },
  dd_hdr_option: {
    type: 'choice', platforms: ['windows'], group: 'display_device', when: ddOn, descKeys: [],
    choices: named('dd_hdr_option', ['disabled', 'auto']),
  },
  dd_wa_hdr_toggle_delay: {
    type: 'number', unit: 'ms', min: 0, max: 3000, integer: true, platforms: ['windows'], group: 'display_device', when: ddOn,
    descKeys: ['config.dd_wa_hdr_toggle_delay_desc_1', 'config.dd_wa_hdr_toggle_delay_desc_2', 'config.dd_wa_hdr_toggle_delay_desc_3'],
  },
  dd_config_revert_delay: { type: 'number', unit: 'ms', min: 0, integer: true, platforms: ['windows'], group: 'display_device', when: ddOn },
  dd_config_revert_on_disconnect: { type: 'bool', platforms: ['windows'], group: 'display_device', when: ddOn },
  dd_mode_remapping: {
    type: 'ModeRemappingEditor', platforms: ['windows'], group: 'display_device', descKeys: [],
    when: (c) => (c.dd_resolution_option === 'auto' || c.dd_refresh_rate_option === 'auto') && ddOn(c),
  },

  // Encoder
  encoder: {
    type: 'choice',
    choices: (platform) => [
      { value: '', label: '_common.autodetect' },
      ...literal(ENCODERS[platform] || []),
      { value: 'software', label: 'config.encoder_software' },
    ],
  },
  hevc_mode: { type: 'choice', choices: numbered('hevc_mode', 4) },
  av1_mode: { type: 'choice', choices: numbered('av1_mode', 4) },

  // Audio
  stream_audio: { type: 'bool' },
  audio_sink: {
    type: 'text', mono: true, platformDesc: true, help: 'audio_sink', picker: 'sinks',
    placeholder: { windows: 'Speakers (High Definition Audio Device)', macos: 'BlackHole 2ch', default: 'alsa_output.pci-0000_09_00.3.analog-stereo' },
  },
  virtual_sink: { type: 'text', platforms: ['windows'], placeholder: 'Steam Streaming Speakers', picker: 'sinks' },
  install_steam_audio_drivers: { type: 'bool', platforms: ['windows'] },
  mic_enabled: { type: 'bool', platforms: ['linux'] },

  // Input
  controller: { type: 'bool' },
  gamepad_driver: {
    type: 'choice', platforms: ['windows'],
    choices: [
      { value: '', label: 'config.gamepad_driver_select', disabled: true },
      ...named('gamepad_driver', ['all', 'virtualhid', 'vigembus']),
    ],
    validate: (v) => (v === '' ? 'nova.settings.errors.gamepad_driver' : null),
  },
  gamepad: {
    type: 'choice', hideOn: ['macos'], when: controllerOn,
    choices: (platform, c) => [
      { value: 'auto', label: '_common.auto' },
      ...GAMEPADS
        .filter((g) => platform !== 'windows' || c.gamepad_driver !== 'vigembus' || VIGEMBUS_GAMEPADS.has(g))
        .map((g) => ({ value: g, label: `config.gamepad_${g}` })),
    ],
  },
  gamepad_motion_profile: {
    type: 'choice', platforms: ['linux'], when: (c) => controllerOn(c) && c.gamepad === 'auto',
    choices: ['auto', 'ds5', 'ds4', 'off'].map((value) => ({ value, label: `nova.settings.choices.gamepad_motion_profile_${value}` })),
  },
  motion_as_ds4: { type: 'bool', platforms: ['windows', 'linux'], when: (c) => controllerOn(c) && c.gamepad === 'auto' },
  touchpad_as_ds4: { type: 'bool', platforms: ['windows', 'linux'], when: (c) => controllerOn(c) && c.gamepad === 'auto' },
  ds4_back_as_touchpad_click: {
    type: 'bool',
    when: (c, p) => controllerOn(c) && (c.gamepad === 'ds4' || c.gamepad === 'ds5' || (c.gamepad === 'auto' && p !== 'macos')),
  },
  virtualhid_randomize_mac: {
    type: 'bool',
    when: (c, p) => controllerOn(c) && c.gamepad_driver !== 'vigembus' &&
      (c.gamepad === 'ds4' || c.gamepad === 'ds5' || (c.gamepad === 'auto' && p !== 'macos')),
  },
  back_button_timeout: { type: 'number', unit: 'ms', min: -1, integer: true, when: controllerOn },
  keyboard: { type: 'bool' },
  key_repeat_delay: { type: 'number', unit: 'ms', min: 0, integer: true, platforms: ['windows'], when: keyboardOn },
  key_repeat_frequency: { type: 'number', unit: '/s', min: 0, step: 0.1, platforms: ['windows'], when: keyboardOn },
  always_send_scancodes: { type: 'bool', platforms: ['windows'], when: keyboardOn },
  key_rightalt_to_key_win: { type: 'bool', when: keyboardOn },
  keybindings: { type: 'KeybindingsEditor', when: keyboardOn },
  mouse: { type: 'bool' },
  high_resolution_scrolling: { type: 'bool', when: mouseOn },
  native_pen_touch: { type: 'bool', when: mouseOn },
  clipboard_sync: { type: 'bool' },

  // Network
  upnp: { type: 'bool' },
  address_family: { type: 'choice', choices: named('address_family', ['ipv4', 'both']) },
  bind_address: { type: 'text', mono: true },
  port: {
    type: 'number', min: 1029, max: 65514, integer: true, help: 'ports', helpOpen: true,
    validate: (v) => {
      const port = Number(v)
      if (v === null || v === '' || Number.isNaN(port)) return null
      if (port - 5 < 1024) return 'config.port_alert_1'
      if (port + 21 > 65535) return 'config.port_alert_2'
      return null
    },
  },
  origin_web_ui_allowed: {
    type: 'choice', choices: named('origin_web_ui_allowed', ['pc', 'lan', 'wan']),
    warn: (c) => (c.origin_web_ui_allowed === 'wan' ? 'config.port_warning' : null),
  },
  csrf_allowed_origins: { type: 'text', mono: true },
  external_ip: { type: 'text', mono: true, placeholder: '123.456.789.12' },
  lan_encryption_mode: {
    type: 'choice',
    choices: [{ value: '0', label: '_common.disabled_def' }, ...named('lan_encryption_mode', ['1', '2'])],
  },
  wan_encryption_mode: {
    type: 'choice',
    choices: [{ value: '0', label: '_common.disabled' }, ...named('wan_encryption_mode', ['1', '2'])],
  },
  ping_timeout: { type: 'number', unit: 'ms', min: 0, integer: true },
  packetsize: { type: 'number', unit: 'bytes', min: 0, max: 65535, integer: true },

  // Files
  file_apps: { type: 'path', placeholder: 'apps.json' },
  credentials_file: { type: 'path', placeholder: 'sunshine_state.json' },
  log_path: { type: 'path', placeholder: 'sunshine.log' },
  pkey: { type: 'path', placeholder: '/dir/pkey.pem' },
  cert: { type: 'path', placeholder: '/dir/cert.pem' },
  file_state: { type: 'path', placeholder: 'sunshine_state.json' },

  // Advanced
  fec_percentage: { type: 'number', unit: '%', min: 1, max: 255, integer: true },
  qp: { type: 'number', min: 0, max: 51, integer: true },
  min_threads: { type: 'number', min: 1, integer: true },

  // NVIDIA NVENC
  nvenc_preset: {
    type: 'choice',
    choices: [1, 2, 3, 4, 5, 6, 7].map((n) => ({
      value: String(n),
      text: `P${n}`,
      suffix: n === 1 ? 'config.nvenc_preset_1' : n === 7 ? 'config.nvenc_preset_7' : null,
    })),
  },
  nvenc_split_encode: {
    type: 'choice', platforms: ['windows'],
    choices: [
      { value: 'disabled', label: '_common.disabled' },
      { value: 'driver_decides', label: 'config.nvenc_split_encode_driver_decides_def' },
      { value: 'enabled', label: '_common.enabled' },
    ],
  },
  nvenc_twopass: { type: 'choice', choices: named('nvenc_twopass', ['disabled', 'quarter_res', 'full_res']) },
  nvenc_spatial_aq: { type: 'bool' },
  nvenc_vbv_increase: { type: 'number', unit: '%', min: 0, max: 400, integer: true },
  nvenc_realtime_hags: { type: 'bool', platforms: ['windows'] },
  nvenc_latency_over_power: { type: 'bool', platforms: ['windows'] },
  nvenc_opengl_vulkan_on_dxgi: { type: 'bool', platforms: ['windows'] },
  nvenc_h264_cavlc: { type: 'bool' },

  // Intel QuickSync
  qsv_preset: { type: 'choice', descKeys: [], choices: named('qsv_preset', ['veryfast', 'faster', 'fast', 'medium', 'slow', 'slower', 'slowest']) },
  qsv_coder: { type: 'choice', descKeys: [], choices: CODER },
  qsv_slow_hevc: { type: 'bool' },

  // AMD AMF
  amd_usage: { type: 'choice', choices: named('amd_usage', ['transcoding', 'webcam', 'lowlatency_high_quality', 'lowlatency', 'ultralowlatency']) },
  amd_rc: { type: 'choice', choices: named('amd_rc', ['cbr', 'cqp', 'vbr_latency', 'vbr_peak']) },
  amd_enforce_hrd: { type: 'bool' },
  amd_max_au_size: { type: 'number', unit: 'bits', min: -1, max: 2147483647, integer: true },
  amd_quality: { type: 'choice', choices: named('amd_quality', ['speed', 'balanced', 'quality']) },
  amd_preanalysis: { type: 'bool' },
  amd_vbaq: { type: 'bool' },
  amd_coder: { type: 'choice', choices: CODER },

  // VideoToolbox
  vt_coder: { type: 'choice', descKeys: [], choices: CODER },
  vt_software: {
    type: 'choice', descKeys: [],
    choices: [
      { value: 'auto', label: '_common.auto' },
      { value: 'disabled', label: '_common.disabled' },
      ...named('vt_software', ['allowed', 'forced']),
    ],
  },
  vt_realtime: { type: 'bool', descKeys: [] },

  // VA-API
  vaapi_rc: {
    type: 'choice',
    choices: [{ value: 'auto', label: '_common.auto' }, ...named('vaapi_rc', ['avbr', 'vbr', 'cbr', 'cqp', 'icq', 'qvbr'])],
  },
  vaapi_blbrc: { type: 'bool' },
  vaapi_strict_rc_buffer: { type: 'bool' },
  vaapi_quality: {
    type: 'choice',
    choices: [{ value: 'auto', label: '_common.auto' }, ...named('vaapi_quality', ['speed', 'balanced', 'quality'])],
  },

  // Vulkan
  vk_tune: {
    type: 'choice',
    choices: [{ value: '0', label: '_common.auto' }, { value: '1', label: 'config.vk_tune_hq' },
      { value: '2', label: 'config.vk_tune_ll' }, { value: '3', label: 'config.vk_tune_ull' }],
  },
  vk_rc_mode: {
    type: 'choice',
    choices: [{ value: '0', label: '_common.auto' }, { value: '1', label: 'config.vk_rc_cqp' },
      { value: '2', label: 'config.vk_rc_cbr' }, { value: '4', label: 'config.vk_rc_vbr' }],
  },

  // Software
  sw_preset: { type: 'choice', choices: named('sw_preset', ['ultrafast', 'superfast', 'veryfast', 'faster', 'fast', 'medium', 'slow', 'slower', 'veryslow']) },
  sw_tune: { type: 'choice', choices: named('sw_tune', ['film', 'animation', 'grain', 'stillimage', 'fastdecode', 'zerolatency']) },
}

/**
 * Page sections in display order. `options` are config keys; `encoders` lists the
 * encoder groups shown inside the Encoder section.
 */
export const SECTIONS = [
  { id: 'general', options: ['sunshine_name', 'locale', 'min_log_level', 'notify_pre_releases', 'system_tray', 'global_prep_cmd'] },
  {
    id: 'library',
    options: ['metadata_steam', 'metadata_language', 'metadata_auto_fetch', 'metadata_ttl_days', 'art_source_priority',
      'art_prefer_official', 'art_steamgriddb_poster_style', 'art_steamgriddb_hero_style', 'art_allow_animated', 'art_nsfw',
      'art_humor', 'steamgriddb_api_key', 'igdb_client_id', 'igdb_client_secret', 'steam_web_api_key', 'rawg_api_key'],
    panel: 'LibraryMetadataPanel',
  },
  { id: 'compat', options: ['windows_launcher', 'proton_auto_update', 'windows_exe_launcher'] },
  {
    id: 'display',
    options: ['capture', 'capture_pacing', 'display_follow', 'display_follow_cmd', 'virtual_display', 'virtual_display_wm',
      'virtual_display_session', 'virtual_display_linger', 'virtual_display_fps_cap', 'adapter_name', 'output_name', 'max_bitrate', 'minimum_fps_target', 'max_fps_target',
      'dd_configuration_option', 'dd_resolution_option', 'dd_manual_resolution', 'dd_refresh_rate_option', 'dd_manual_refresh_rate',
      'dd_hdr_option', 'dd_wa_hdr_toggle_delay', 'dd_config_revert_delay', 'dd_config_revert_on_disconnect', 'dd_mode_remapping'],
  },
  { id: 'encoder', options: ['encoder', 'hevc_mode', 'av1_mode'], encoders: true },
  { id: 'audio', options: ['stream_audio', 'audio_sink', 'virtual_sink', 'install_steam_audio_drivers', 'mic_enabled'] },
  { id: 'host', options: ['pcsleep_enabled', 'host_commands'] },
  {
    id: 'input',
    options: ['controller', 'gamepad_driver', 'gamepad', 'gamepad_motion_profile', 'motion_as_ds4', 'touchpad_as_ds4', 'ds4_back_as_touchpad_click',
      'virtualhid_randomize_mac', 'back_button_timeout', 'keyboard', 'key_repeat_delay', 'key_repeat_frequency',
      'always_send_scancodes', 'key_rightalt_to_key_win', 'keybindings', 'mouse', 'high_resolution_scrolling',
      'native_pen_touch', 'clipboard_sync'],
  },
  {
    id: 'network',
    options: ['upnp', 'address_family', 'bind_address', 'port', 'origin_web_ui_allowed', 'csrf_allowed_origins', 'external_ip',
      'lan_encryption_mode', 'wan_encryption_mode', 'ping_timeout', 'packetsize'],
  },
  { id: 'files', options: ['file_apps', 'credentials_file', 'log_path', 'pkey', 'cert', 'file_state'] },
  { id: 'advanced', options: ['fec_percentage', 'qp', 'min_threads'] },
]

/**
 * Encoder groups, matching the encoder tabs of config_tabs.json. `encoders` are the
 * values the host logs ("Found H.264 encoder: … [nvenc]") and accepts for `encoder`.
 */
export const ENCODER_GROUPS = [
  { id: 'nv', titleKey: 'config.category_nvidia_nvenc_encoder', encoders: ['nvenc'], hideOn: ['macos'] },
  { id: 'qsv', titleKey: 'config.category_intel_quicksync_encoder', encoders: ['quicksync'], platforms: ['windows'] },
  { id: 'amd', titleKey: 'config.category_amd_amf_encoder', encoders: ['amdvce'], platforms: ['windows'] },
  { id: 'vt', titleKey: 'config.category_videotoolbox_encoder', encoders: ['videotoolbox'], platforms: ['macos'] },
  { id: 'vaapi', titleKey: 'config.category_vaapi_encoder', encoders: ['vaapi'], platforms: ['linux', 'freebsd'] },
  { id: 'vulkan', titleKey: 'config.category_vulkan_encoder', encoders: ['vulkan'], platforms: ['linux', 'freebsd'] },
  { id: 'sw', titleKey: 'config.category_software_encoder', encoders: ['software'] },
]

/** Sub-headings inside a section, keyed by an option's `group`. */
export const GROUPS = {
  display_device: 'config.dd_options_header',
  metadata_details: 'nova.settings.library_group_details',
  metadata_artwork: 'nova.settings.library_group_artwork',
  metadata_keys: 'nova.settings.library_group_keys',
}

/**
 * Keep dependent options consistent after a change (the old Input tab did this with a watcher):
 * ViGEmBus only emulates X360 and DS4 pads, so other gamepad types fall back to auto.
 *
 * @param {object} config Working configuration (mutated).
 * @param {string} platform Host platform.
 */
export function applyRules(config, platform) {
  if (platform === 'windows' && config.gamepad_driver === 'vigembus' && !VIGEMBUS_GAMEPADS.has(config.gamepad)) {
    config.gamepad = 'auto'
  }
}

export { on as isOn }
