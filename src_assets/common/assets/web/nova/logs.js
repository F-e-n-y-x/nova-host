/**
 * @file Parse the host log (/api/logs) into entries and derive what the dashboard shows:
 * detected encoders and health checks. Only facts present in the log are reported.
 */

const LINE_START = /\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})]:\s/g

/**
 * Split the raw log into entries.
 *
 * @param {string} text Raw log text.
 * @returns {{timestamp: string, level: string, message: string}[]} Entries in order.
 */
export function parseLogs(text) {
  if (!text) return []
  const parts = text.split(LINE_START)
  const entries = []
  for (let i = 1; i + 1 < parts.length; i += 2) {
    const body = parts[i + 1].replace(/\s+$/, '')
    const colon = body.indexOf(':')
    const level = colon > 0 ? body.slice(0, colon) : ''
    const message = colon > 0 ? body.slice(colon + 1).trim() : body
    entries.push({ timestamp: parts[i], level, message })
  }
  return entries
}

const ENCODER_LABELS = {
  nvenc: 'NVENC',
  vaapi: 'VA-API',
  vulkan: 'Vulkan',
  amdvce: 'AMF',
  quicksync: 'Quick Sync',
  videotoolbox: 'VideoToolbox',
  software: 'Software',
}

/**
 * Encoders the host settled on at startup ("Found H.264 encoder: h264_nvenc [nvenc]").
 *
 * @param {ReturnType<typeof parseLogs>} entries Log entries.
 * @returns {{codec: string, name: string, type: string, label: string, hardware: boolean}[]} Encoders.
 */
export function detectEncoders(entries) {
  const found = new Map()
  for (const { message } of entries) {
    const match = /^Found (\S+) encoder: (\S+) \[([^\]]+)]/.exec(message)
    if (match) {
      const [, codec, name, type] = match
      found.set(codec, { codec, name, type, label: ENCODER_LABELS[type] || type, hardware: type !== 'software' })
    }
  }
  return [...found.values()]
}

/**
 * Health checks for the dashboard. Each check names i18n keys (under nova.health) and
 * a status. A check only appears when the log (or config) shows the condition.
 *
 * @param {ReturnType<typeof parseLogs>} entries Log entries.
 * @param {object} [config] Host config from /api/config.
 * @returns {{id: string, status: 'success'|'warning'|'danger', title: string, desc: string, params?: object, to?: string}[]} Checks.
 */
export function healthChecks(entries, config = {}) {
  const checks = []

  const fatal = entries.filter((e) => e.level === 'Fatal')
  if (fatal.length > 0) {
    checks.push({ id: 'fatal', status: 'danger', title: 'nova.health.fatal_title', desc: 'nova.health.fatal_desc',
      params: { count: fatal.length }, to: '/logs' })
  }

  const encoders = detectEncoders(entries)
  const hardware = encoders.filter((e) => e.hardware)
  if (hardware.length > 0) {
    checks.push({ id: 'encoder', status: 'success', title: 'nova.health.gpu_encoding_title', desc: 'nova.health.gpu_encoding_desc',
      params: { encoder: [...new Set(hardware.map((e) => e.label))].join(', '), codecs: hardware.map((e) => e.codec).join(', ') } })
  } else if (encoders.length > 0) {
    checks.push({ id: 'encoder', status: 'warning', title: 'nova.health.cpu_encoding_title', desc: 'nova.health.cpu_encoding_desc',
      to: '/settings#encoder' })
  } else if (entries.length > 0) {
    checks.push({ id: 'encoder', status: 'danger', title: 'nova.health.no_encoder_title', desc: 'nova.health.no_encoder_desc', to: '/logs' })
  }

  // "Couldn't find an active default sink" is not a failure: Nova streams from its own virtual sinks.
  if (entries.some((e) => /^Unable to initialize audio capture/.test(e.message))) {
    checks.push({ id: 'audio', status: 'warning', title: 'nova.health.audio_title', desc: 'nova.health.audio_desc', to: '/settings#audio' })
  }

  const clipboardOn = !['disabled', 'false', false].includes(config.clipboard_sync)
  if (clipboardOn && entries.some((e) => /^clipboard: (could not watch the local clipboard|exec failed|wl-paste --watch failed)/.test(e.message))) {
    checks.push({ id: 'clipboard', status: 'warning', title: 'nova.health.clipboard_title', desc: 'nova.health.clipboard_desc', to: '/settings#input' })
  }

  return checks
}
