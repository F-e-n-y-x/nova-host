// Nova browser client: the stream page's interface, on top of moonlight-web-stream's stream.js.
//
// Upstream builds the stream (video, audio, input) and exposes its ViewerApp as window.app. This
// module adds Nova's loading and error screen, an auto-hiding top bar, a side menu (fullscreen,
// keyboard, mouse mode, stats, quality, disconnect or quit) and shortcuts. It only calls
// ViewerApp's public methods and hides upstream's own sidebar and connection dialog with CSS, so
// upstream's TypeScript stays unmodified apart from the small patches in ../patches.
import {
  DISPLAY_LABEL, aboutDialog, creditLine, el, formatMbps, getJson, icon, logo, overlay, postJson, qualityFields,
  qualitySummary, readSettings, segmented, toast,
} from './common.js'

const query = new URLSearchParams(location.search)
const hostId = Number(query.get('hostId'))
const appId = Number(query.get('appId'))
const display = query.get('novaDisplay')
const isTouch = matchMedia('(pointer: coarse)').matches || navigator.maxTouchPoints > 0
const mac = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent)
const CHORD = mac ? ['Ctrl', '⌥', '⇧'] : ['Ctrl', 'Alt', 'Shift']
document.documentElement.classList.toggle('is-touch', isTouch)

const S = {
  app: null, title: '', phase: 'connecting', error: null, log: [], capabilities: null, status: 'Ok',
  hud: false, menuOpen: false, videoAt: 0, started: performance.now(), lastLoss: null, session: null,
}

// ------------------------------------------------------------------------------ root
// Everything Nova draws lives in one root that swallows input events, so clicks and keys in the
// menu never reach the stream (upstream listens on document).
const root = el('div', { class: 'nv-stream-ui', id: 'nv-stream-ui' })
for (const type of ['keydown', 'keyup', 'paste', 'mousedown', 'mouseup', 'mousemove', 'wheel', 'contextmenu', 'touchstart', 'touchend', 'touchcancel', 'touchmove']) {
  root.addEventListener(type, (e) => e.stopPropagation())
}

const streamInput = () => S.app?.getStream?.()?.getInput?.()
const refocusStream = () => { document.getElementById('input')?.focus({ preventScroll: true }) }

// ------------------------------------------------------------------------------ loading / error veil

const veil = (() => {
  const bg = el('div', { class: 'nv-veil__bg', 'aria-hidden': 'true' })
  const artBox = el('div', { class: 'nv-veil__art' }, el('span', { class: 'nv-veil__ring', 'aria-hidden': 'true' }), el('span', { class: 'nv-art' }))
  const badge = el('div', { class: 'nv-veil__badge', 'aria-hidden': 'true' }, icon('alert', 'nv-icon--lg'))
  const title = el('h1', { class: 'nv-veil__title' }, 'Starting your game')
  const status = el('p', { class: 'nv-veil__status', role: 'status', 'aria-live': 'polite' }, 'Connecting to your PC…')
  const steps = el('ol', { class: 'nv-veil__steps', 'aria-hidden': 'true' }, el('li'), el('li'), el('li'))
  const slow = el('p', { class: 'nv-veil__slow', hidden: true })
  const log = el('pre', { class: 'nv-veil__log', 'aria-label': 'Connection log' })
  const logBtn = el('button', { type: 'button', class: 'nv-btn nv-btn--ghost', 'aria-expanded': 'false', onclick: () => toggleLog() }, 'Show details')
  const actions = el('div', { class: 'nv-veil__actions' })
  const node = el('div', { class: 'nv-veil', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'nv-veil-title' },
    bg, el('div', { class: 'nv-veil__card' }, artBox, badge, title, status, steps, slow, actions, log),
    el('div', { class: 'nv-veil__credit' }, creditLine(() => about?.open())))
  title.id = 'nv-veil-title'

  function toggleLog(force) {
    const shown = force ?? !log.classList.contains('is-shown')
    log.classList.toggle('is-shown', shown)
    logBtn.textContent = shown ? 'Hide details' : 'Show details'
    logBtn.setAttribute('aria-expanded', String(shown))
    if (shown) { log.textContent = S.log.join('\n'); log.scrollTop = log.scrollHeight }
  }
  function setStep(n) {
    [...steps.children].forEach((li, i) => { li.className = i < n ? 'is-done' : i === n ? 'is-now' : '' })
  }
  function loadingActions() {
    actions.replaceChildren(el('a', { class: 'nv-btn nv-btn--secondary', href: '/' }, icon('back', 'nv-icon--sm'), 'Back to games'), logBtn)
  }
  loadingActions()
  setStep(0)
  return {
    node,
    setArt(src, name) {
      const box = artBox.querySelector('.nv-art')
      box.replaceChildren()
      if (src) {
        const img = el('img', { src, alt: '' })
        img.addEventListener('error', () => box.replaceChildren(el('span', { class: 'nv-art__placeholder' }, el('span', {}, name || ''))), { once: true })
        box.append(img)
        bg.style.backgroundImage = `url("${src}")`
      } else {
        box.append(el('span', { class: 'nv-art__placeholder' }, el('span', {}, name || '')))
      }
    },
    setTitle(t) { if (!S.error) title.textContent = t },
    status(text, step) { if (S.error) return; status.textContent = text; if (step != null) setStep(step) },
    slow(text) { slow.hidden = !text; slow.textContent = text || '' },
    log() { if (log.classList.contains('is-shown')) { log.textContent = S.log.join('\n'); log.scrollTop = log.scrollHeight } },
    hide() { node.classList.add('is-gone'); node.setAttribute('aria-hidden', 'true'); node.setAttribute('inert', '') },
    error(heading, text) {
      node.classList.remove('is-gone'); node.classList.add('is-error'); node.removeAttribute('aria-hidden'); node.removeAttribute('inert')
      title.textContent = heading
      status.textContent = text
      slow.hidden = true
      actions.replaceChildren(
        el('button', { type: 'button', class: 'nv-btn nv-btn--primary', 'data-autofocus': true, onclick: () => location.reload() }, icon('refresh', 'nv-icon--sm'), 'Try again'),
        el('a', { class: 'nv-btn nv-btn--secondary', href: '/' }, icon('back', 'nv-icon--sm'), 'Back to games'),
        logBtn)
      actions.firstChild.focus()
    },
  }
})()
root.append(veil.node)

// ------------------------------------------------------------------------------ top bar

const hint = el('div', { class: 'nv-hint', role: 'status', 'aria-live': 'polite' })
let hintTimer = 0
function showHint(content, ms = 3500) {
  hint.replaceChildren(...(Array.isArray(content) ? content : [content]))
  hint.classList.add('is-shown')
  clearTimeout(hintTimer)
  hintTimer = setTimeout(() => hint.classList.remove('is-shown'), ms)
}
const chord = (key) => [...CHORD, key].map((k) => el('kbd', { class: 'nv-kbd' }, k))

const bar = (() => {
  const name = el('span', { class: 'nv-bar__name' }, 'Nova')
  const live = el('span', { class: 'nv-chip nv-chip--success' }, el('span', { class: 'nv-dot' }), 'Live')
  const mode = display ? el('span', { class: 'nv-chip nv-chip--accent' }, DISPLAY_LABEL[display] || display) : null
  const fmt = el('span', { class: 'nv-chip nv-num' })
  const btn = (label, iconName, onclick, attrs = {}) => el('button', { type: 'button', class: 'nv-icon-btn', 'aria-label': label, title: label, onclick, ...attrs }, icon(iconName))
  const statsBtn = btn('Stats overlay', 'stats', () => toggleHud(), { 'aria-pressed': 'false', 'data-nv-optional': true })
  const kbdBtn = btn('Keyboard', 'keyboard', () => showKeyboard(), { hidden: !isTouch })
  const fsBtn = btn('Full screen', 'fullscreen', () => toggleFullscreen())
  const menuBtn = el('button', { type: 'button', class: 'nv-bar__menu', 'data-nv-menu-button': true, 'aria-haspopup': 'dialog', onclick: () => menu.open() },
    icon('menu'), el('span', {}, 'Menu'), !isTouch && el('kbd', {}, `${CHORD.join('+')}+M`))
  const node = el('header', { class: 'nv-bar', 'aria-label': 'Stream controls' },
    el('a', { class: 'nv-icon-btn nv-bar__back', href: '/', 'aria-label': 'Back to games (the game keeps running)', title: 'Back to games', onclick: (e) => { e.preventDefault(); disconnect() } }, icon('back')),
    el('div', { class: 'nv-bar__title' }, logo(''), name),
    el('div', { class: 'nv-bar__chips' }, live, mode, fmt),
    el('span', { class: 'nv-bar__spacer' }),
    el('div', { class: 'nv-bar__actions' }, statsBtn, kbdBtn, fsBtn, menuBtn))
  const handle = el('button', { type: 'button', class: 'nv-handle', 'aria-label': 'Show stream controls', onclick: () => reveal(4000) })

  let hideTimer = 0
  let shown = false
  let hovering = false
  node.addEventListener('mouseenter', () => { hovering = true; clearTimeout(hideTimer) })
  node.addEventListener('mouseleave', () => { hovering = false; schedule(1200) })
  node.addEventListener('focusin', () => { clearTimeout(hideTimer); show() })
  function show() {
    if (S.phase !== 'playing' || document.pointerLockElement) return
    shown = true
    node.classList.add('is-shown')
    handle.classList.remove('is-dim')
  }
  function hide() {
    if (hovering || S.menuOpen || node.contains(document.activeElement)) return
    shown = false
    node.classList.remove('is-shown')
    setTimeout(() => handle.classList.add('is-dim'), 2500)
  }
  function schedule(ms) { clearTimeout(hideTimer); hideTimer = setTimeout(hide, ms) }
  function reveal(ms = 2500) { show(); schedule(ms) }
  return {
    node, handle, reveal, show, hide: () => { hovering = false; hide() }, isShown: () => shown,
    setTitle(t) { name.textContent = t },
    setFormat(t) { fmt.textContent = t; fmt.hidden = !t },
    setStatus(ok) {
      live.className = `nv-chip ${ok ? 'nv-chip--success' : 'nv-chip--warning'}`
      live.lastChild.textContent = ok ? 'Live' : 'Poor connection'
    },
    sync() {
      const fs = !!document.fullscreenElement
      fsBtn.replaceChildren(icon(fs ? 'exitFullscreen' : 'fullscreen'))
      fsBtn.setAttribute('aria-label', fs ? 'Exit full screen' : 'Full screen')
      fsBtn.title = fsBtn.getAttribute('aria-label')
      statsBtn.setAttribute('aria-pressed', String(S.hud))
    },
  }
})()
root.append(bar.node, bar.handle, hint)
fmtUpdate()

// Reveal the bar when the pointer comes near the top edge, then let it hide again.
addEventListener('mousemove', (e) => {
  if (document.pointerLockElement || isTouchEvent(e)) return
  if (e.clientY < 72) bar.reveal(2200)
}, { capture: true, passive: true })
function isTouchEvent(e) { return e.sourceCapabilities?.firesTouchEvents }

// ------------------------------------------------------------------------------ stats HUD

const hud = (() => {
  const item = (label) => { const v = el('span', { class: 'nv-hud__value' }, '–'); return [el('span', { class: 'nv-hud__item' }, v, el('span', { class: 'nv-hud__label' }, label)), v] }
  const [fpsI, fps] = item('FPS')
  const [rttI, rtt] = item('Network ms')
  const [hostI, host] = item('Host ms')
  const [lossI, loss] = item('Loss')
  const [resI, res] = item('Video')
  const node = el('div', { class: 'nv-hud', 'aria-hidden': 'true' }, fpsI, rttI, hostI, lossI, resI)
  return { node, fps, rtt, host, loss, lossI, res }
})()
root.append(hud.node)

function readStats() {
  const stats = S.app?.getStream?.()?.getStats?.()
  if (!stats) return null
  const d = stats.getCurrentStats?.()
  if (!d) return null
  const t = d.transport || {}
  const received = Number(t.webrtcPacketsReceived) || 0
  const lost = Number(t.webrtcPacketsLost) || 0
  let lossPct = null
  if (S.lastLoss && received > S.lastLoss.received) {
    lossPct = (100 * Math.max(0, lost - S.lastLoss.lost)) / Math.max(1, received - S.lastLoss.received + Math.max(0, lost - S.lastLoss.lost))
  }
  S.lastLoss = { received, lost }
  const fps = d.videoFps ?? t.webrtcFps
  const w = d.videoWidth ?? t.videoWidth
  const h = d.videoHeight ?? t.videoHeight
  return {
    fps: fps != null ? Math.round(fps) : null,
    rtt: d.streamerRttMs ?? d.browserRtt,
    host: d.avgHostProcessingLatencyMs,
    loss: lossPct,
    size: w && h ? `${w}×${h}` : null,
    codec: d.videoCodec,
    decoder: t.decoderImplementation || d.videoPipeline,
    dropped: t.webrtcFramesDropped,
  }
}
const n1 = (v) => (v == null || Number.isNaN(v) ? '–' : v < 10 ? v.toFixed(1) : String(Math.round(v)))

function setStatsEnabled(on) {
  const stats = S.app?.getStream?.()?.getStats?.()
  if (stats && stats.isEnabled() !== on) stats.setEnabled(on)
}
function toggleHud(force) {
  S.hud = force ?? !S.hud
  hud.node.classList.toggle('is-shown', S.hud)
  bar.sync()
  menu.sync()
  try { localStorage.setItem('nvStatsHud', S.hud ? '1' : '0') } catch { /* ignore */ }
}

setInterval(() => {
  // Stats are collected while the HUD or the menu shows them.
  const want = S.hud || S.menuOpen
  setStatsEnabled(want)
  if (!want) return
  const s = readStats()
  if (!s) return
  hud.fps.textContent = s.fps ?? '–'
  hud.rtt.textContent = n1(s.rtt)
  hud.host.textContent = n1(s.host)
  hud.loss.textContent = s.loss == null ? '–' : `${s.loss.toFixed(1)}%`
  hud.lossI.classList.toggle('is-warn', (s.loss || 0) >= 2)
  hud.res.textContent = s.size || '–'
  menu.stats(s)
  if (s.size || s.codec) fmtUpdate(s)
}, 500)

function fmtUpdate(s) {
  const codec = s?.codec ? String(s.codec).replace(/^H265.*/, 'HEVC').replace(/^H264.*/, 'H.264').replace(/^AV1.*/, 'AV1') : null
  bar.setFormat([s?.size, codec].filter(Boolean).join(' · '))
}

// ------------------------------------------------------------------------------ actions

async function toggleFullscreen() {
  if (!S.app) return
  if (S.app.isFullscreen()) {
    S.app.markManualFullscreenExitRequested?.()
    await S.app.exitFullscreen()
  } else {
    await S.app.requestFullscreen()
  }
  bar.sync(); menu.sync()
}

function showKeyboard() {
  menu.close()
  const kb = S.app?.sidebar?.getScreenKeyboard?.()
  if (kb) kb.show()
  else toast('The keyboard is not available yet')
}

async function captureMouse() {
  menu.close()
  try {
    await S.app?.requestPointerLock(true)
  } catch {
    toast("This browser didn't allow capturing the mouse", { tone: 'warning' })
  }
}

function setMouseMode(mode) {
  if (!S.app) return
  if (mode === 'relative') { captureMouse(); return }
  if (document.pointerLockElement) document.exitPointerLock()
  const cfg = S.app.getInputConfig()
  cfg.mouseMode = mode
  S.app.setInputConfig(cfg)
  menu.sync()
}

function setTouchMode(mode) {
  if (!S.app) return
  const cfg = S.app.getInputConfig()
  cfg.touchMode = mode
  S.app.setInputConfig(cfg)
}

function sendCombo(keys) {
  const input = streamInput()
  if (!input) return
  // Modifier flags follow the keys held so far, as a real keyboard would send them.
  const MOD = { 16: 1, 17: 2, 18: 4, 91: 8 }
  let mods = 0
  for (const k of keys) { mods |= MOD[k] || 0; input.sendKey(true, k, mods) }
  for (const k of [...keys].reverse()) { mods &= ~(MOD[k] || 0); input.sendKey(false, k, mods) }
  toast('Sent to the PC')
}

async function stopStream() {
  S.leaving = true
  try { await S.app?.getStream?.()?.stop() } catch { /* the page is leaving anyway */ }
}

async function disconnect() {
  await stopStream()
  location.assign('/')
}

async function quitGame() {
  await stopStream()
  try {
    await postJson('/api/host/cancel', { host_id: hostId })
  } catch {
    toast("Couldn't quit the game on the PC", { tone: 'danger' })
    await new Promise((r) => setTimeout(r, 1200))
  }
  location.assign('/')
}

async function reconnect() {
  await stopStream()
  location.reload()
}

// Quitting closes the game on the PC, so it takes a second press within a few seconds.
function quitButton() {
  const title = el('span', { class: 'nv-choice__title' }, 'Quit game')
  const sub = el('span', { class: 'nv-choice__sub' }, 'Closes it on the PC')
  let armed = 0
  const b = el('button', { type: 'button', class: 'nv-choice nv-choice--danger', 'data-nv-quit': true }, icon('power'), el('span', { class: 'nv-choice__text' }, title, sub))
  b.addEventListener('click', () => {
    if (armed) { clearTimeout(armed); quitGame(); return }
    title.textContent = 'Press again to quit'
    sub.textContent = 'Unsaved progress is lost'
    armed = setTimeout(() => { armed = 0; title.textContent = 'Quit game'; sub.textContent = 'Closes it on the PC' }, 4000)
  })
  return b
}

// ------------------------------------------------------------------------------ menu

const menu = (() => {
  const sheet = el('aside', { class: 'nv-sheet nv-smenu', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'nv-menu-title' })
  const o = overlay(sheet, {
    onClose: () => { S.menuOpen = false; refocusStream(); bar.reveal(1500) },
  })
  const title = el('h2', { class: 'nv-sheet__title', id: 'nv-menu-title' }, 'Nova')
  const chips = el('div', { class: 'nv-smenu__chips' })
  const band = el('div', { class: 'nv-statband' })
  const vals = {}
  for (const [k, label] of [['fps', 'FPS'], ['rtt', 'Network'], ['host', 'Host'], ['loss', 'Loss']]) {
    vals[k] = el('div', { class: 'nv-statband__value' }, '–')
    band.append(el('div', {}, vals[k], el('div', { class: 'nv-statband__label' }, label)))
  }
  const line = el('div', { class: 'nv-statband__line' }, 'Collecting…')
  band.append(line)

  const tile = (iconName, label, onclick) => {
    const state = el('span', { class: 'nv-qt__state' })
    const b = el('button', { type: 'button', class: 'nv-qt', onclick }, el('span', { class: 'nv-qt__icon' }, icon(iconName)), el('span', { class: 'nv-qt__title' }, label), state)
    b.stateEl = state
    return b
  }
  const tFull = tile('fullscreen', 'Full screen', () => toggleFullscreen())
  const tKbd = tile('keyboard', 'Keyboard', () => showKeyboard())
  const tMouse = tile('mouse', 'Capture mouse', () => (document.pointerLockElement ? document.exitPointerLock() : captureMouse()))
  const tStats = tile('stats', 'Stats overlay', () => toggleHud())
  const tKeys = tile('command', 'Special keys', () => { keysRow.hidden = !keysRow.hidden; tKeys.setAttribute('aria-expanded', String(!keysRow.hidden)) })
  const tHome = tile('back', 'Games', () => disconnect())
  tKeys.setAttribute('aria-expanded', 'false')
  tHome.stateEl.textContent = 'Keeps running'
  tKbd.stateEl.textContent = isTouch ? 'On screen' : 'Type on the PC'
  const K = { ctrl: 17, alt: 18, shift: 16, win: 91, del: 46, tab: 9, esc: 27, f4: 115, prt: 44 }
  const keysRow = el('div', { class: 'nv-keys', hidden: true },
    ...[['Ctrl+Alt+Del', [K.ctrl, K.alt, K.del]], ['Alt+Tab', [K.alt, K.tab]], ['Win', [K.win]], ['Esc', [K.esc]], ['Alt+F4', [K.alt, K.f4]], ['Print Screen', [K.prt]]]
      .map(([label, keys]) => el('button', { type: 'button', class: 'nv-btn nv-btn--secondary', onclick: () => sendCombo(keys) }, label)))

  const mouseSeg = segmented('Mouse', [
    { value: 'follow', label: 'Pointer' }, { value: 'relative', label: 'Captured' }, { value: 'localCursor', label: 'Local cursor' }, { value: 'pointAndDrag', label: 'Drag' },
  ], 'follow', setMouseMode, { block: true })
  const touchSeg = segmented('Touch', [
    { value: 'mouseRelative', label: 'Trackpad' }, { value: 'pointAndDrag', label: 'Direct' }, { value: 'touch', label: 'Touch' }, { value: 'localCursor', label: 'Cursor' },
  ], 'mouseRelative', setTouchMode, { block: true })

  const apply = el('div', { class: 'nv-apply' },
    el('button', { type: 'button', class: 'nv-btn nv-btn--primary nv-btn--block', onclick: () => reconnect() }, icon('refresh', 'nv-icon--sm'), 'Reconnect to apply'))
  const quality = el('div')
  const summary = el('span', { class: 'nv-field__hint' })

  const section = (label, ...children) => el('section', { class: 'nv-section' }, el('h3', { class: 'nv-section__title' }, label), ...children)
  const body = el('div', { class: 'nv-sheet__body' },
    section('Connection', band),
    section('Controls', el('div', { class: 'nv-tiles' }, tFull, tKbd, tMouse, tStats, tKeys, tHome), keysRow),
    section('Mouse', mouseSeg, el('p', { class: 'nv-field__hint', style: { marginTop: '8px' } }, 'Captured sends raw movement for 3D games; press Esc to get the pointer back.')),
    isTouch ? section('Touch', touchSeg) : null,
    section('Quality', summary, quality, apply),
    !isTouch ? section('Shortcuts', el('dl', { class: 'nv-shortcuts' },
      el('dt', {}, chord('M')), el('dd', {}, 'Open or close this menu'),
      el('dt', {}, chord('F')), el('dd', {}, 'Full screen'),
      el('dt', {}, chord('Z')), el('dd', {}, 'Capture or release the mouse'),
      el('dt', {}, chord('S')), el('dd', {}, 'Stats overlay'),
      el('dt', {}, chord('Q')), el('dd', {}, 'Disconnect, the game keeps running'))) : section('Gestures', el('dl', { class: 'nv-shortcuts' },
      el('dt', {}, 'Handle at the top'), el('dd', {}, 'Controls and this menu'),
      el('dt', {}, 'Three-finger tap'), el('dd', {}, 'Keyboard'),
      el('dt', {}, 'Two-finger drag'), el('dd', {}, 'Scroll'))),
  )
  const foot = el('div', { class: 'nv-sheet__foot' },
    el('div', { class: 'nv-exit' },
      el('button', { type: 'button', class: 'nv-choice', 'data-nv-disconnect': true, onclick: () => disconnect() }, icon('close'),
        el('span', { class: 'nv-choice__text' }, el('span', { class: 'nv-choice__title' }, 'Disconnect'), el('span', { class: 'nv-choice__sub' }, 'Game keeps running'))),
      quitButton()),
    creditLine(() => about?.open()))
  sheet.append(
    el('div', { class: 'nv-sheet__head' }, title, el('button', { type: 'button', class: 'nv-icon-btn', 'aria-label': 'Close menu', 'data-autofocus': true, onclick: () => o.close() }, icon('close'))),
    chips, body, foot)
  o.mount(root)

  function renderQuality() {
    const start = JSON.stringify(readSettings())
    summary.textContent = `Now: ${qualitySummary()}. Changes apply when you reconnect; the game keeps running.`
    quality.replaceChildren(qualityFields(() => { apply.classList.toggle('is-shown', JSON.stringify(readSettings()) !== start) }))
    apply.classList.remove('is-shown')
  }

  return {
    open() {
      if (S.phase !== 'playing') return
      hint.classList.remove('is-shown')
      if (document.pointerLockElement) document.exitPointerLock()
      S.menuOpen = true
      renderQuality()
      this.sync()
      o.open()
    },
    close: () => o.close(),
    toggle() { if (o.isOpen()) o.close(); else this.open() },
    isOpen: () => o.isOpen(),
    setTitle(t) { title.textContent = t },
    sync() {
      const fs = !!document.fullscreenElement
      tFull.setAttribute('aria-pressed', String(fs)); tFull.stateEl.textContent = fs ? 'On' : 'Off'
      const locked = !!document.pointerLockElement
      tMouse.setAttribute('aria-pressed', String(locked)); tMouse.stateEl.textContent = locked ? 'On' : 'Off'
      tMouse.disabled = isTouch && !('requestPointerLock' in document.body)
      tStats.setAttribute('aria-pressed', String(S.hud)); tStats.stateEl.textContent = S.hud ? 'On' : 'Off'
      const cfg = S.app?.getInputConfig?.()
      if (cfg) { mouseSeg.setValue(cfg.mouseMode); touchSeg.setValue(cfg.touchMode) }
      const touchBtn = touchSeg.querySelector('[data-value="touch"]')
      if (touchBtn && S.capabilities) touchBtn.disabled = !S.capabilities.touch
      chips.replaceChildren(
        el('span', { class: `nv-chip ${S.status === 'Ok' ? 'nv-chip--success' : 'nv-chip--warning'}` }, el('span', { class: 'nv-dot' }), S.status === 'Ok' ? 'Live' : 'Poor connection'),
        display ? el('span', { class: 'nv-chip nv-chip--accent' }, DISPLAY_LABEL[display] || display) : null,
        el('span', { class: 'nv-chip nv-num' }, formatMbps(readSettings().bitrate)))
    },
    stats(s) {
      if (!o.isOpen()) return
      vals.fps.textContent = s.fps ?? '–'
      vals.rtt.textContent = s.rtt == null ? '–' : `${n1(s.rtt)} ms`
      vals.host.textContent = s.host == null ? '–' : `${n1(s.host)} ms`
      vals.loss.textContent = s.loss == null ? '–' : `${s.loss.toFixed(1)}%`
      line.textContent = [s.size, s.codec, s.decoder && `decoder ${s.decoder}`, s.dropped != null && `${s.dropped} frames dropped`].filter(Boolean).join(' · ') || 'Collecting…'
    },
  }
})()

let about = null

// ------------------------------------------------------------------------------ shortcuts

// Ctrl+Alt+Shift+<key>, the chord Moonlight clients use. Caught before upstream sends keys to the
// PC; the modifiers that already went out are released so nothing stays held on the PC.
addEventListener('keydown', (e) => {
  if (!(e.ctrlKey && e.altKey && e.shiftKey)) return
  const action = { KeyM: () => menu.toggle(), KeyQ: () => disconnect(), KeyS: () => toggleHud(), KeyF: () => toggleFullscreen(), KeyX: () => toggleFullscreen(),
    KeyZ: () => (document.pointerLockElement ? document.exitPointerLock() : captureMouse()) }[e.code]
  if (!action) return
  e.preventDefault()
  e.stopImmediatePropagation()
  streamInput()?.raiseAllKeys?.()
  action()
}, { capture: true })

document.addEventListener('fullscreenchange', () => { bar.sync(); menu.sync() })
document.addEventListener('pointerlockchange', () => {
  menu.sync()
  if (document.pointerLockElement) {
    bar.hide()
    showHint(['Mouse captured · press ', el('kbd', { class: 'nv-kbd' }, 'Esc'), ' to release'], 3000)
  } else if (S.phase === 'playing') {
    bar.reveal(2500)
  }
})

// ------------------------------------------------------------------------------ stream events

const LINE_HINTS = [
  [/Completed Stage: Launch Streamer/, 'Starting the game on your PC…', 1],
  [/WebRTC negotiation success|Trying Web Socket transport/, 'Setting up video…', 1],
  [/Using video pipeline/, 'Waiting for the first picture…', 2],
]

function onInfo(event) {
  const d = event.detail || {}
  if (d.type === 'app' && d.app?.title) {
    setTitle(d.app.title)
  } else if (d.type === 'connectionComplete') {
    S.capabilities = d.capabilities
    veil.status('Waiting for the first picture…', 2)
    menu.sync()
  } else if (d.type === 'videoReady') {
    ready()
  } else if (d.type === 'connectionStatus') {
    const ok = d.status !== 'Poor'
    if ((S.status === 'Ok') !== ok) {
      S.status = d.status
      bar.setStatus(ok)
      if (!ok) { toast('The connection is poor. A lower bitrate may help.', { tone: 'warning' }); bar.reveal(3000) }
      menu.sync()
    }
  } else if (d.type === 'serverMessage') {
    S.log.push(`Server: ${d.message}`)
    veil.status(String(d.message))
  } else if (d.type === 'addDebugLine') {
    const line = String(d.line || '').trim()
    if (!line) return
    S.log.push(line)
    if (S.log.length > 400) S.log.splice(0, S.log.length - 400)
    veil.log()
    const kind = d.additional?.type
    if (kind === 'fatal' || kind === 'fatalDescription') fail(line)
    else if (kind === 'informError') toast(line, { tone: 'warning' })
    else for (const [re, text, step] of LINE_HINTS) if (re.test(line)) veil.status(text, step)
  }
}

function setTitle(t) {
  if (!t || S.title === t) return
  S.title = t
  document.title = `${t} · Nova`
  bar.setTitle(t)
  menu.setTitle(t)
  veil.setTitle(S.phase === 'playing' ? t : `Starting ${t}`)
}

function ready() {
  if (S.phase === 'playing') return
  S.phase = 'playing'
  S.videoAt = performance.now()
  veil.hide()
  refocusStream()
  bar.reveal(3500)
  if (!isTouch) setTimeout(() => showHint(['Menu: ', ...chord('M'), ' or move to the top edge'], 4000), 600)
  else setTimeout(() => showHint('Tap the handle at the top for the menu', 4000), 600)
}

function fail(line) {
  if (S.leaving) return
  const ended = S.phase === 'playing'
  if (!S.error) S.error = []
  S.error.push(line)
  if (menu.isOpen()) menu.close()
  bar.hide()
  if (document.pointerLockElement) document.exitPointerLock()
  S.phase = 'error'
  veil.error(ended ? 'The stream ended' : "Couldn't start the stream", friendly(S.error))
}

function friendly(lines) {
  const all = lines.join('\n')
  if (/host was not found/i.test(all)) return "This stream link points to a PC the browser client doesn't know. Go back to your games and start it from there."
  if (/codec|video pipeline|video format/i.test(all)) return 'This browser cannot decode the chosen video format. Pick H.264 under Quality in the stream settings, then try again.'
  if (/no connection was possible|WebRTC|transport/i.test(all)) return "The video connection couldn't be set up. Check that this device is on the same network as the PC and that the PC's firewall allows the browser client's ports."
  if (/ConnectionTerminated/i.test(all)) return 'The PC closed the stream. The game may have quit, or another device took over.'
  return lines[lines.length - 1]
}

// Slow start: say what is going on instead of spinning forever.
setTimeout(() => { if (S.phase === 'connecting') veil.slow('This is taking longer than usual. A game that is still starting on the PC can take a minute.') }, 20000)
setTimeout(() => { if (S.phase === 'connecting') veil.slow('Still waiting. If nothing happens, go back and try again, or open the details to see what the connection is doing.') }, 60000)

// ------------------------------------------------------------------------------ boot

document.body.append(root)

// Game title and artwork for the loading screen.
;(async () => {
  let title = ''
  try {
    const apps = (await getJson(`/api/apps?host_id=${hostId}`)).apps || []
    title = apps.find((a) => a.app_id === appId)?.title || ''
  } catch { /* the stream reports the title too */ }
  if (title) setTitle(title)
  let src = ''
  try {
    const lib = await getJson('/nova/library')
    const entry = (lib.apps || []).find((a) => a.name === (S.title || title))
    if (entry?.poster) src = `/nova/art?index=${entry.index}&kind=poster`
  } catch { /* no artwork: the placeholder shows */ }
  veil.setArt(src, S.title || title)
  try {
    S.session = await getJson('/nova/session')
    about = aboutDialog(S.session)
  } catch { about = aboutDialog(null) }
})()

try { if (localStorage.getItem('nvStatsHud') === '1') toggleHud(true) } catch { /* ignore */ }

// Stream events: early.js recorded them from the first one; replay those, then follow live.
const hub = window.__novaStreamInfo
if (hub) {
  for (const e of hub.events.splice(0)) onInfo(e)
  hub.listeners.push(onInfo)
}

// Upstream's ViewerApp (window.app) drives the controls.
const started = performance.now()
;(function waitForApp() {
  const app = window.app
  const stream = app?.getStream?.()
  if (stream) {
    S.app = app
    if (!hub) stream.addInfoListener(onInfo)
    menu.sync()
    return
  }
  if (performance.now() - started > 30000) {
    fail('The stream page did not start. Reload the page, or go back to your games.')
    return
  }
  setTimeout(waitForApp, 50)
})()

// Leaving the tab: tell the PC so the session ends cleanly (the game keeps running).
addEventListener('pagehide', () => { if (S.phase === 'playing') S.app?.getStream?.()?.stop?.() })
