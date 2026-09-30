// Nova browser client: helpers shared by the game picker and the stream page.
// Plain ES module, no build step. Everything here talks only to the gateway's own origin.

const SVG_NS = 'http://www.w3.org/2000/svg'

// 24px outline icons in one stroke weight (drawn for Nova; shapes follow the lucide set Nova's web UI uses).
const ICONS = {
  star: '<path d="M12 2.2c.77 4.8 5.06 9.09 9.86 9.86-4.8.77-9.09 5.06-9.86 9.86-.77-4.8-5.06-9.09-9.86-9.86C6.94 11.29 11.23 7 12 2.2z"/>',
  play: '<path d="M7 4.5v15l12.5-7.5z"/>',
  monitor: '<rect x="2.5" y="3.5" width="19" height="13" rx="2"/><path d="M8 20.5h8M12 16.5v4"/>',
  virtual: '<rect x="2.5" y="3.5" width="19" height="13" rx="2"/><path d="M8 20.5h8M12 16.5v4"/><path d="M9 10l2 2 4-4"/>',
  phone: '<rect x="6.5" y="2.5" width="11" height="19" rx="2.5"/><path d="M11 18.5h2"/>',
  settings: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
  close: '<path d="M6 6l12 12M18 6 6 18"/>',
  back: '<path d="M15 5l-7 7 7 7"/>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  power: '<path d="M12 3v8"/><path d="M6.4 6.6a8 8 0 1 0 11.2 0"/>',
  fullscreen: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
  exitFullscreen: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
  keyboard: '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M6 14h.01M18 14h.01M9 14h6"/>',
  mouse: '<rect x="6" y="3" width="12" height="18" rx="6"/><path d="M12 7v4"/>',
  stats: '<path d="M4 20V10M10 20V4M16 20v-7M22 20H2"/>',
  menu: '<path d="M4 7h16M4 12h16M4 17h16"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/>',
  alert: '<circle cx="12" cy="12" r="9"/><path d="M12 7.5v5.5M12 16.5h.01"/>',
  logout: '<path d="M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3M10 16l-4-4 4-4M6 12h10"/>',
  external: '<path d="M14 4h6v6M20 4l-9 9M18 14v4a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4"/>',
  wake: '<path d="M12 3v3M12 18v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M3 12h3M18 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/><circle cx="12" cy="12" r="3.5"/>',
  refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/>',
  command: '<path d="M9 6a3 3 0 1 0-3 3h12a3 3 0 1 0-3-3v12a3 3 0 1 0 3-3H6a3 3 0 1 0 3 3z"/>',
  touch: '<path d="M9 11V5.5a1.5 1.5 0 0 1 3 0V10m0-1.5a1.5 1.5 0 0 1 3 0V11m0-1a1.5 1.5 0 0 1 3 0v4.5a6.5 6.5 0 0 1-6.5 6.5h-.8a6 6 0 0 1-4.6-2.2L4.5 15a1.6 1.6 0 0 1 2.4-2.1L9 15"/>',
  gamepad: '<path d="M6 11h4M8 9v4M15 12h.01M18 10h.01"/><path d="M17.3 5H6.7a4 4 0 0 0-4 3.6l-.7 6.2A3 3 0 0 0 5 18c1 0 1.9-.5 2.4-1.3L9 14h6l1.6 2.7A2.8 2.8 0 0 0 19 18a3 3 0 0 0 3-3.2l-.7-6.2A4 4 0 0 0 17.3 5z"/>',
  wifi: '<path d="M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M2 9a15 15 0 0 1 20 0M12 19.5h.01"/>',
}

export function icon(name, cls = '') {
  const svg = document.createElementNS(SVG_NS, 'svg')
  svg.setAttribute('viewBox', '0 0 24 24')
  svg.setAttribute('aria-hidden', 'true')
  svg.setAttribute('focusable', 'false')
  svg.setAttribute('class', `nv-icon ${name === 'play' || name === 'star' ? 'nv-icon--fill ' : ''}${cls}`.trim())
  svg.innerHTML = ICONS[name] || ''
  return svg
}

export function logo(wordmark = 'Nova', size = 26) {
  const wrap = el('span', { class: 'nv-logo' })
  const svg = document.createElementNS(SVG_NS, 'svg')
  svg.setAttribute('width', size); svg.setAttribute('height', size); svg.setAttribute('viewBox', '0 0 28 28'); svg.setAttribute('aria-hidden', 'true')
  svg.innerHTML = '<path d="M14 2.5c.9 5.6 5.9 10.6 11.5 11.5-5.6.9-10.6 5.9-11.5 11.5-.9-5.6-5.9-10.6-11.5-11.5C8.1 13.1 13.1 8.1 14 2.5z"/>'
  wrap.append(svg)
  if (wordmark) wrap.append(el('span', {}, wordmark))
  return wrap
}

/** Tiny element builder: el('button', { class: 'x', onclick }, 'text', child). Text is never parsed as HTML. */
export function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag)
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue
    if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v)
    else if (k === 'class') node.className = v
    else if (k === 'style' && typeof v === 'object') Object.assign(node.style, v)
    else if (k === 'dataset') Object.assign(node.dataset, v)
    else node.setAttribute(k, v === true ? '' : String(v))
  }
  for (const c of children.flat()) {
    if (c == null || c === false) continue
    node.append(c instanceof Node ? c : document.createTextNode(String(c)))
  }
  return node
}

// ------------------------------------------------------------------ gateway / upstream API

export class HttpError extends Error {
  constructor(status, message) { super(message); this.status = status }
}

export async function getJson(path, { timeout = 12000, lines = false } = {}) {
  const ctrl = new AbortController()
  const timer = setTimeout(() => ctrl.abort(), timeout)
  try {
    const res = await fetch(path, { signal: ctrl.signal, credentials: 'same-origin', headers: { Accept: 'application/json' } })
    if (!res.ok) throw new HttpError(res.status, `${path}: ${res.status}`)
    const text = await res.text()
    if (!lines) return JSON.parse(text)
    // moonlight-web-stream streams some answers as JSON lines: the first line is the state, each
    // later line an update of one item (hosts, by host_id) as it comes in.
    const [first, ...rest] = text.split('\n').filter((l) => l.trim())
    const state = JSON.parse(first)
    for (const line of rest) {
      const item = JSON.parse(line)
      for (const list of Object.values(state)) {
        if (!Array.isArray(list)) continue
        const i = list.findIndex((x) => x && item && x.host_id != null && x.host_id === item.host_id)
        if (i >= 0) list[i] = { ...list[i], ...item }
      }
    }
    return state
  } finally {
    clearTimeout(timer)
  }
}

export async function postJson(path, body) {
  const res = await fetch(path, {
    method: 'POST', credentials: 'same-origin',
    headers: { 'Content-Type': 'application/json', Accept: 'application/json' }, body: JSON.stringify(body),
  })
  if (!res.ok) throw new HttpError(res.status, `${path}: ${res.status}`)
  const text = await res.text()
  return text ? JSON.parse(text) : null
}

/** Signed out (the gateway answers 401): reload, and the gateway shows "Sign in with Nova". */
export function handleAuth(err) {
  if (err instanceof HttpError && err.status === 401) {
    location.reload()
    return true
  }
  return false
}

// ------------------------------------------------------------------ stream settings
// moonlight-web-stream keeps its settings in localStorage "mlSettings" and merges them over its
// defaults, so a partial object is enough. The stream page reads them when it starts.

const SETTINGS_KEY = 'mlSettings'
export const DEFAULTS = {
  bitrate: 10000, fps: 60, videoSize: 'custom', videoSizeCustom: { width: 1920, height: 1080 }, videoCodec: 'h264',
  mouseMode: 'follow', touchMode: 'mouseRelative', enterFullscreenOnStreamStart: false, playAudioLocal: false,
}

export function readSettings() {
  try {
    const raw = JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}')
    return { ...DEFAULTS, ...(raw && typeof raw === 'object' ? raw : {}) }
  } catch {
    return { ...DEFAULTS }
  }
}

export function writeSettings(patch) {
  try {
    let raw = {}
    try { raw = JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}') || {} } catch { raw = {} }
    localStorage.setItem(SETTINGS_KEY, JSON.stringify({ ...raw, ...patch }))
  } catch { /* private mode: settings last for this page only */ }
}

export const RESOLUTIONS = [
  { value: '720p', label: '720p', size: [1280, 720] },
  { value: '1080p', label: '1080p', size: [1920, 1080] },
  { value: '1440p', label: '1440p', size: [2560, 1440] },
  { value: '4k', label: '4K', size: [3840, 2160] },
  { value: 'native', label: 'Window' },
]
export const FPS = [30, 60, 90, 120]
export const CODECS = [
  { value: 'auto', label: 'Auto' }, { value: 'h264', label: 'H.264' }, { value: 'h265', label: 'HEVC' }, { value: 'av1', label: 'AV1' },
]

/** The resolution option a settings object stands for ('custom' 1920x1080 is upstream's default = 1080p). */
export function resolutionOf(s) {
  if (s.videoSize !== 'custom') return s.videoSize
  const c = s.videoSizeCustom || {}
  const hit = RESOLUTIONS.find((r) => r.size && r.size[0] === c.width && r.size[1] === c.height)
  return hit ? hit.value : 'custom'
}

export function setResolution(value) {
  const r = RESOLUTIONS.find((x) => x.value === value)
  if (!r) return
  // Presets are written as custom sizes, which upstream always honours exactly.
  writeSettings(r.size ? { videoSize: 'custom', videoSizeCustom: { width: r.size[0], height: r.size[1] } } : { videoSize: 'native' })
}

export function windowSize() {
  const dpr = Math.min(window.devicePixelRatio || 1, 2)
  const w = Math.round(Math.max(document.documentElement.clientWidth, window.innerWidth) * dpr)
  const h = Math.round(Math.max(document.documentElement.clientHeight, window.innerHeight) * dpr)
  return [w, h]
}

export function qualitySummary(s = readSettings()) {
  const res = resolutionOf(s)
  const r = RESOLUTIONS.find((x) => x.value === res)
  const size = res === 'custom' ? `${s.videoSizeCustom?.width}×${s.videoSizeCustom?.height}` : r ? r.label : res
  const codec = CODECS.find((c) => c.value === s.videoCodec)?.label || s.videoCodec
  return `${size} · ${s.fps} fps · ${formatMbps(s.bitrate)} · ${codec}`
}

export function formatMbps(kbps) {
  const mbps = kbps / 1000
  return `${mbps >= 10 ? Math.round(mbps) : mbps.toFixed(1).replace(/\.0$/, '')} Mbps`
}

// Bitrate slider: 1 to 150 Mbps on a gentle curve so the common 5-40 range gets most of the track.
export const BITRATE_MIN = 1000
export const BITRATE_MAX = 150000
export const sliderToKbps = (t) => Math.round((BITRATE_MIN * Math.pow(BITRATE_MAX / BITRATE_MIN, t / 1000)) / 500) * 500 || BITRATE_MIN
export const kbpsToSlider = (k) => Math.round((1000 * Math.log(Math.max(k, BITRATE_MIN) / BITRATE_MIN)) / Math.log(BITRATE_MAX / BITRATE_MIN))

// ------------------------------------------------------------------ UI pieces

/** A segmented control. options: [{ value, label, disabled }] */
export function segmented(label, options, value, onChange, { block = false } = {}) {
  const group = el('div', { class: `nv-seg${block ? ' nv-seg--block' : ''}`, role: 'group', 'aria-label': label })
  const buttons = options.map((o) => {
    const b = el('button', { type: 'button', 'aria-pressed': String(o.value === value), disabled: o.disabled || false, dataset: { value: String(o.value) } }, o.label)
    b.addEventListener('click', () => {
      for (const x of buttons) x.setAttribute('aria-pressed', String(x === b))
      onChange(o.value)
    })
    return b
  })
  group.append(...buttons)
  group.setValue = (v) => { for (const x of buttons) x.setAttribute('aria-pressed', String(x.dataset.value === String(v))) }
  return group
}

export function switchRow(title, sub, checked, onChange) {
  const sw = el('button', { type: 'button', class: 'nv-switch', role: 'switch', 'aria-checked': String(!!checked), 'aria-label': title })
  const row = el('label', { class: 'nv-switch-row' },
    el('span', { class: 'nv-switch-row__text' }, el('span', { class: 'nv-switch-row__title' }, title), sub && el('span', { class: 'nv-switch-row__sub' }, sub)),
    sw)
  sw.addEventListener('click', (e) => {
    e.preventDefault()
    const next = sw.getAttribute('aria-checked') !== 'true'
    sw.setAttribute('aria-checked', String(next))
    onChange(next)
  })
  row.addEventListener('click', (e) => { if (e.target !== sw) { e.preventDefault(); sw.click() } })
  return row
}

export function bitrateField(value, onChange) {
  const out = el('span', { class: 'nv-field__value' }, formatMbps(value))
  const range = el('input', { type: 'range', class: 'nv-range', min: 0, max: 1000, step: 1, value: kbpsToSlider(value), 'aria-label': 'Bitrate' })
  const paint = () => { range.style.setProperty('--fill', `${range.value / 10}%`) }
  paint()
  range.addEventListener('input', () => { const k = sliderToKbps(Number(range.value)); out.textContent = formatMbps(k); paint(); onChange(k) })
  return el('div', { class: 'nv-field' },
    el('div', { class: 'nv-field__row' }, el('span', { class: 'nv-field__label' }, 'Bitrate'), out),
    range,
    el('span', { class: 'nv-field__hint' }, 'Higher looks sharper and needs a faster network. 10 to 20 Mbps suits 1080p on Wi-Fi.'))
}

/** Stream quality fields (resolution, frame rate, bitrate, codec) bound to mlSettings. */
export function qualityFields(onChange = () => {}, { maxKbps = null } = {}) {
  const s = readSettings()
  const [ww, wh] = windowSize()
  const wrap = el('div')
  const field = (label, control, hint) => el('div', { class: 'nv-field' },
    el('div', { class: 'nv-field__row' }, el('span', { class: 'nv-field__label' }, label)), control, hint && el('span', { class: 'nv-field__hint' }, hint))
  wrap.append(
    field('Resolution', segmented('Resolution', RESOLUTIONS.map((r) => ({ value: r.value, label: r.label })), resolutionOf(s),
      (v) => { setResolution(v); onChange() }, { block: true }), `Window streams at this browser window's size (${ww}×${wh} now). With a virtual display, Nova sizes the display to match.`),
    field('Frame rate', segmented('Frame rate', FPS.map((f) => ({ value: f, label: `${f}` })), s.fps,
      (v) => { writeSettings({ fps: v }); onChange() }, { block: true })),
    bitrateField(Math.min(s.bitrate, maxKbps || Infinity), (k) => { writeSettings({ bitrate: k }); onChange() }),
    field('Video codec', segmented('Video codec', CODECS, s.videoCodec, (v) => { writeSettings({ videoCodec: v }); onChange() }, { block: true }),
      'H.264 plays in every browser. HEVC and AV1 look better at the same bitrate where the browser can decode them.'),
  )
  return wrap
}

// ------------------------------------------------------------------ overlays

let toastHost = null
export function toast(message, { tone = '', timeout = 3200 } = {}) {
  if (!toastHost) {
    toastHost = el('div', { class: 'nv-toasts', role: 'status', 'aria-live': 'polite' })
    document.body.append(toastHost)
  }
  const t = el('div', { class: `nv-toast${tone ? ` nv-toast--${tone}` : ''}` }, message)
  toastHost.append(t)
  setTimeout(() => { t.classList.add('is-leaving'); setTimeout(() => t.remove(), 220) }, timeout)
  return t
}

/**
 * Sheet or dialog with a scrim, Escape to close and focus kept inside while open.
 * Returns { root, open(), close(), isOpen() }.
 */
export function overlay(root, { onClose, scrimClass = '' } = {}) {
  const scrim = el('div', { class: `nv-scrim ${scrimClass}`.trim() })
  let lastFocus = null
  let open = false
  const api = {
    root,
    isOpen: () => open,
    open() {
      if (open) return
      open = true
      lastFocus = document.activeElement
      root.classList.add('is-open'); scrim.classList.add('is-open')
      root.removeAttribute('inert')
      root.setAttribute('aria-hidden', 'false')
      requestAnimationFrame(() => (root.querySelector('[data-autofocus]') || root.querySelector('button, [href], input, [tabindex]'))?.focus({ preventScroll: true }))
    },
    close() {
      if (!open) return
      open = false
      root.classList.remove('is-open'); scrim.classList.remove('is-open')
      root.setAttribute('inert', '')
      root.setAttribute('aria-hidden', 'true')
      if (lastFocus && document.contains(lastFocus)) lastFocus.focus({ preventScroll: true })
      onClose?.()
    },
  }
  root.setAttribute('inert', '')
  root.setAttribute('aria-hidden', 'true')
  scrim.addEventListener('click', () => api.close())
  root.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') { e.preventDefault(); api.close() }
    if (e.key === 'Tab') {
      const f = [...root.querySelectorAll('button:not([disabled]), [href], input:not([disabled]), [tabindex]:not([tabindex="-1"])')].filter((x) => x.offsetParent !== null)
      if (!f.length) return
      if (e.shiftKey && document.activeElement === f[0]) { e.preventDefault(); f[f.length - 1].focus() }
      else if (!e.shiftKey && document.activeElement === f[f.length - 1]) { e.preventDefault(); f[0].focus() }
    }
  })
  api.mount = (parent = document.body) => { parent.append(scrim, root); return api }
  return api
}

/** About: the credit to moonlight-web-stream, its licence and where its source is. */
export function aboutDialog(info) {
  const up = info?.upstream || {}
  const dialog = el('div', { class: 'nv-dialog', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'nv-about-title' })
  const o = overlay(dialog)
  dialog.append(
    el('h2', { id: 'nv-about-title' }, 'About the browser client'),
    el('p', {}, 'Nova streams to this browser with moonlight-web-stream by MrCreativ3001 and contributors, which turns the Moonlight protocol into WebRTC. Nova runs it as a separate program and adds this interface on top.'),
    el('dl', { class: 'nv-kv' },
      el('dt', {}, 'Streaming'), el('dd', {}, up.version || 'moonlight-web-stream'),
      el('dt', {}, 'Licence'), el('dd', {}, up.licence || 'GPL-3.0-or-later'),
      el('dt', {}, 'Source'), el('dd', {}, el('a', { href: up.source || 'https://github.com/MrCreativ3001/moonlight-web-stream', target: '_blank', rel: 'noopener' }, 'github.com/MrCreativ3001/moonlight-web-stream')),
      el('dt', {}, 'Device'), el('dd', {}, `Shows in Nova's Devices as "${info?.device_name || 'Browser client'}"`),
    ),
    el('div', { class: 'nv-dialog__actions' }, el('button', { type: 'button', class: 'nv-btn nv-btn--primary', 'data-autofocus': true, onclick: () => o.close() }, 'Done')),
  )
  return o.mount()
}

export function creditLine(onAbout) {
  return el('p', { class: 'nv-credit' },
    'Streaming by ', el('a', { href: 'https://github.com/MrCreativ3001/moonlight-web-stream', target: '_blank', rel: 'noopener' }, 'moonlight-web-stream'),
    ' (GPL-3.0) · ', el('button', { type: 'button', onclick: onAbout }, 'About'))
}

/** Title-derived hue for artwork placeholders, the same formula as Nova's web UI (NvArt). */
export function hueOf(title) {
  let h = 0
  for (const ch of title) h = (h * 31 + ch.codePointAt(0)) % 360
  return h
}

export const DISPLAY_LABEL = { virtual: 'Virtual display', mirror: 'Mirror desktop' }
