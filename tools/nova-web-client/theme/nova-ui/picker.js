// Nova browser client: the game picker. Lists Nova's games as art tiles; a game opens a details
// view with the Virtual display / Mirror desktop choice, like Nebula. Playing goes through the
// gateway's /nova/play, which pairs if needed and opens moonlight-web-stream's stream page.
import {
  DISPLAY_LABEL, aboutDialog, creditLine, el, getJson, handleAuth, hueOf, icon, logo, overlay, postJson,
  qualityFields, qualitySummary, readSettings, segmented, switchRow, toast, writeSettings,
} from './common.js'

const app = document.getElementById('app')
const state = { session: null, host: null, detailed: null, games: [], libraryOk: false, error: null }

const store = {
  get(k, d = null) { try { return localStorage.getItem(k) ?? d } catch { return d } },
  set(k, v) { try { localStorage.setItem(k, v) } catch { /* private mode */ } },
}
const defaultMode = () => (store.get('nvDisplayDefault') === 'mirror' ? 'mirror' : 'virtual')
const rememberedMode = (g) => { const m = store.get(`nvDisplay:${g.title}`); return m === 'virtual' || m === 'mirror' ? m : null }
const modeFor = (g) => rememberedMode(g) || g.display || defaultMode()
const playUrl = (g, mode) => `/nova/play?${new URLSearchParams({ id: String(g.app_id), display: mode })}`

function play(g, mode) {
  store.set(`nvDisplay:${g.title}`, mode)
  location.assign(playUrl(g, mode))
}

// ------------------------------------------------------------------------------ artwork

function art(g, kind = 'poster', { eager = false } = {}) {
  const box = el('span', { class: 'nv-art', style: { '--h': String(hueOf(g.title)) } })
  const fallback = () => {
    box.replaceChildren(el('span', { class: 'nv-art__placeholder', role: 'img', 'aria-label': g.title }, el('span', { 'aria-hidden': 'true' }, g.title)))
  }
  const src = artSrc(g, kind)
  if (!src) { fallback(); return box }
  const img = el('img', { src, alt: g.title, loading: eager ? 'eager' : 'lazy', decoding: 'async' })
  img.addEventListener('error', fallback, { once: true })
  box.append(img)
  return box
}

function artSrc(g, kind) {
  if (g.index != null) {
    if (kind === 'poster' && g.poster) return `/nova/art?index=${g.index}&kind=poster`
    if (kind === 'hero' && g.hero) return `/nova/art?index=${g.index}&kind=hero`
    if (kind === 'logo' && g.logo) return `/nova/art?index=${g.index}&kind=logo`
    return ''
  }
  // Nova's library wasn't readable: fall back to the box art Nova sends every Moonlight client.
  if (kind === 'poster' && !state.libraryOk && state.host) {
    return `/api/app/image?${new URLSearchParams({ host_id: state.host.host_id, app_id: g.app_id, force_refresh: 'false' })}`
  }
  return ''
}

// ------------------------------------------------------------------------------ header

function header() {
  const top = el('header', { class: 'nv-top' })
  const brand = el('a', { href: '/', class: 'nv-top__brand', 'aria-label': 'Nova, play in browser' }, logo('Nova'),
    el('span', { class: 'nv-top__divider', 'aria-hidden': 'true' }), el('span', { class: 'nv-top__label' }, 'Play in browser'))
  brand.style.textDecoration = 'none'
  const settingsBtn = el('button', { type: 'button', class: 'nv-icon-btn', title: 'Stream settings', 'aria-label': 'Stream settings', onclick: () => settings.open() }, icon('settings'))
  top.append(brand, el('span', { class: 'nv-top__spacer' }), settingsBtn, userMenu())
  addEventListener('scroll', () => top.classList.toggle('is-scrolled', scrollY > 4), { passive: true })
  return top
}

function userMenu() {
  const s = state.session || {}
  const name = s.user || 'Signed in'
  const wrap = el('div', { class: 'nv-user' })
  const pop = el('div', { class: 'nv-menu-pop', role: 'menu', id: 'nv-user-menu' })
  const btn = el('button', { type: 'button', class: 'nv-user__btn', 'aria-haspopup': 'menu', 'aria-expanded': 'false', 'aria-controls': 'nv-user-menu' },
    el('span', { class: 'nv-avatar', 'aria-hidden': 'true' }, name.charAt(0)), el('span', { class: 'nv-user__name' }, name))
  const close = () => { pop.classList.remove('is-open'); btn.setAttribute('aria-expanded', 'false') }
  btn.addEventListener('click', (e) => {
    e.stopPropagation()
    const open = !pop.classList.contains('is-open')
    pop.classList.toggle('is-open', open)
    btn.setAttribute('aria-expanded', String(open))
    if (open) pop.querySelector('a, button')?.focus()
  })
  document.addEventListener('click', (e) => { if (!wrap.contains(e.target)) close() })
  pop.addEventListener('keydown', (e) => { if (e.key === 'Escape') { close(); btn.focus() } })
  pop.append(
    el('div', { class: 'nv-menu-pop__who' }, el('strong', {}, name), el('span', {}, 'Signed in with your Nova account')),
    s.nova_url && el('a', { role: 'menuitem', href: s.nova_url, target: '_blank', rel: 'noopener' }, icon('external'), 'Open Nova'),
    el('button', { type: 'button', role: 'menuitem', onclick: () => { close(); settings.open() } }, icon('settings'), 'Stream settings'),
    el('button', { type: 'button', role: 'menuitem', onclick: () => { close(); about.open() } }, icon('info'), 'About the browser client'),
    s.signout_url && el('a', { role: 'menuitem', href: s.signout_url }, icon('logout'), 'Sign out'),
  )
  wrap.append(btn, pop)
  return wrap
}

// ------------------------------------------------------------------------------ host + now playing

function hostCard() {
  const h = state.detailed || state.host
  const online = h && h.server_state != null
  const busy = h?.server_state === 'Busy'
  const cls = !online ? 'is-offline' : busy ? 'is-busy' : 'is-ready'
  const playing = busy ? state.games.find((g) => g.app_id === state.detailed?.current_game) : null
  const label = !online ? 'Offline' : busy ? (playing ? `Playing ${playing.title}` : 'Playing a game') : 'Ready'
  const card = el('section', { class: 'nv-host', 'aria-label': 'Host' },
    el('span', { class: 'nv-host__icon' }, icon('monitor', 'nv-icon--lg')),
    el('div', { style: { minWidth: '0' } },
      el('div', { class: 'nv-host__name' }, h?.name || 'Your PC'),
      el('div', { class: `nv-host__state ${cls}` }, el('span', { class: `nv-dot${online ? ' nv-dot--pulse' : ''}` }), label),
      online && el('div', { class: 'nv-host__meta nv-num' }, qualitySummary())))
  if (!online) {
    card.append(el('div', { class: 'nv-host__actions' },
      el('button', { type: 'button', class: 'nv-btn nv-btn--secondary', onclick: wake }, icon('wake'), 'Wake PC'),
      el('button', { type: 'button', class: 'nv-btn nv-btn--ghost', onclick: () => load() }, icon('refresh'), 'Check again')))
  } else {
    card.append(el('div', { class: 'nv-host__play' },
      el('span', { class: 'nv-host__play-label', id: 'nv-play-on' }, 'Games start on'),
      segmented('Games start on', [{ value: 'virtual', label: 'Virtual display' }, { value: 'mirror', label: 'Mirror desktop' }], defaultMode(),
        (v) => { store.set('nvDisplayDefault', v); renderGrid() })))
  }
  return card
}

async function wake() {
  try {
    await postJson('/api/host/wake', { host_id: state.host.host_id })
    toast('Wake-up sent. The PC can take a minute to come online.')
    setTimeout(() => load(), 8000)
  } catch (e) {
    if (!handleAuth(e)) toast("Couldn't send the wake-up", { tone: 'danger' })
  }
}

function nowPlaying() {
  const id = state.detailed?.current_game
  const g = id ? state.games.find((x) => x.app_id === id) : null
  if (!g) return null
  return el('section', { class: 'nv-now', 'aria-label': 'Now playing' },
    art(g),
    el('div', { class: 'nv-now__text' },
      el('div', { class: 'nv-now__label' }, el('span', { class: 'nv-dot nv-dot--pulse' }), 'Now playing'),
      el('div', { class: 'nv-now__title' }, g.title)),
    el('div', { class: 'nv-now__actions' },
      el('button', { type: 'button', class: 'nv-btn nv-btn--primary', onclick: () => play(g, modeFor(g)) }, icon('play', 'nv-icon--sm'), 'Resume'),
      el('button', { type: 'button', class: 'nv-btn nv-btn--secondary', onclick: () => confirmQuit(g) }, icon('power', 'nv-icon--sm'), 'Quit')))
}

function confirmQuit(g) {
  const dialog = el('div', { class: 'nv-dialog', role: 'alertdialog', 'aria-modal': 'true', 'aria-labelledby': 'nv-quit-title' })
  const o = overlay(dialog, { onClose: () => setTimeout(() => { o.root.remove() }, 300) })
  dialog.append(
    el('h2', { id: 'nv-quit-title' }, `Quit ${g.title}?`),
    el('p', {}, 'It closes on the PC. Anything unsaved in the game is lost.'),
    el('div', { class: 'nv-dialog__actions' },
      el('button', { type: 'button', class: 'nv-btn nv-btn--ghost', 'data-autofocus': true, onclick: () => o.close() }, 'Keep playing'),
      el('button', {
        type: 'button', class: 'nv-btn nv-btn--danger',
        onclick: async () => {
          try {
            await postJson('/api/host/cancel', { host_id: state.host.host_id })
            toast(`${g.title} closed`)
          } catch (e) {
            if (!handleAuth(e)) toast("Couldn't quit the game", { tone: 'danger' })
          }
          o.close()
          load()
        },
      }, 'Quit game')))
  o.mount().open()
}

// ------------------------------------------------------------------------------ library grid

const grid = el('div', { class: 'nv-grid', role: 'list' })
const count = el('span', { class: 'nv-lib__count' })

function renderGrid() {
  grid.replaceChildren(...state.games.map((g, i) => {
    const mode = modeFor(g)
    const running = state.detailed?.current_game === g.app_id
    const tile = el('button', {
      type: 'button', class: 'nv-tile', role: 'listitem', style: { '--i': String(Math.min(i, 24)) },
      'aria-label': `${g.title}${running ? ', playing now' : ''}`, dataset: { nvGame: g.title },
      onclick: () => detail.show(g),
    },
    el('span', { style: { position: 'relative', display: 'block' } }, art(g, 'poster', { eager: i < 12 }),
      running && el('span', { class: 'nv-tile__badge nv-chip nv-chip--success' }, el('span', { class: 'nv-dot' }), 'Playing'),
      el('span', { class: 'nv-tile__play', 'aria-hidden': 'true' }, icon('play'))),
    el('span', { class: 'nv-tile__name' }, g.title),
    el('span', { class: 'nv-tile__meta' }, DISPLAY_LABEL[mode]))
    return tile
  }))
  count.textContent = `${state.games.length} ${state.games.length === 1 ? 'game' : 'games'}`
}

function skeleton() {
  return el('div', { class: 'nv-grid', 'aria-hidden': 'true' }, Array.from({ length: 6 }, () =>
    el('div', { class: 'nv-tile' }, el('span', { class: 'nv-art nv-skeleton' }), el('span', { class: 'nv-skeleton', style: { height: '14px', width: '70%' } }))))
}

// ------------------------------------------------------------------------------ details

const detail = (() => {
  const root = el('div', { class: 'nv-detail', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'nv-detail-title' })
  const o = overlay(root, { onClose: () => history.state?.nvDetail && history.back() })
  o.mount()
  addEventListener('popstate', () => { if (o.isOpen()) o.close() })
  return {
    show(g) {
      const mode = modeFor(g)
      const heroSrc = artSrc(g, 'hero')
      const backdrop = el('div', { class: `nv-detail__backdrop${heroSrc ? '' : ' is-blur'}`, 'aria-hidden': 'true' })
      const bgSrc = heroSrc || artSrc(g, 'poster')
      if (bgSrc) backdrop.append(el('img', { src: bgSrc, alt: '' }))
      else backdrop.style.background = `linear-gradient(160deg, hsl(${hueOf(g.title)} 50% 24%), hsl(${hueOf(g.title) + 30} 60% 8%))`
      const logoSrc = artSrc(g, 'logo')
      const [vw, vh] = [Math.round(innerWidth * Math.min(devicePixelRatio || 1, 2)), Math.round(innerHeight * Math.min(devicePixelRatio || 1, 2))]
      const choice = (value, title, sub, iconName) => el('button', {
        type: 'button', class: `nv-choice${value === mode ? ' nv-choice--primary' : ''}`, 'data-autofocus': value === mode ? true : null,
        dataset: { nvPlay: value }, onclick: () => play(g, value),
      }, icon(iconName, 'nv-icon--lg'), el('span', { class: 'nv-choice__text' }, el('span', { class: 'nv-choice__title' }, title), el('span', { class: 'nv-choice__sub' }, sub)))
      const running = state.detailed?.current_game === g.app_id
      root.replaceChildren(
        backdrop,
        el('div', { class: 'nv-detail__bar' },
          el('button', { type: 'button', class: 'nv-icon-btn', 'aria-label': 'Back to games', title: 'Back (Esc)', onclick: () => o.close() }, icon('back'))),
        el('div', { class: 'nv-detail__body' },
          el('div', {}, art(g, 'poster', { eager: true })),
          el('div', {},
            logoSrc ? el('img', { class: 'nv-detail__logo', src: logoSrc, alt: g.title }) : null,
            el('h2', { class: logoSrc ? 'nv-visually-hidden' : 'nv-detail__title', id: 'nv-detail-title' }, g.title),
            el('p', { class: 'nv-detail__meta' }, running ? 'Running on your PC now. Pick a display to rejoin it.' : `On ${state.detailed?.name || state.host?.name || 'your PC'}`),
            el('p', { class: 'nv-detail__label' }, 'Play on'),
            el('div', { class: 'nv-detail__choices' },
              choice('virtual', 'Virtual display', `Sized for this window · ${vw}×${vh}`, 'virtual'),
              choice('mirror', 'Desktop (Mirror)', "The PC's own screen", 'monitor')),
            el('p', { class: 'nv-detail__note' }, icon('check'), rememberedMode(g) ? 'Your last choice for this game' : g.display ? "This game's default in Nova" : 'This browser remembers your choice for this game'),
            el('div', { class: 'nv-detail__quality' }, el('span', {}, qualitySummary()),
              el('button', { type: 'button', class: 'nv-btn nv-btn--ghost', onclick: () => settings.open() }, 'Change')))))
      root.querySelector('.nv-detail__body > div:first-child .nv-art').classList.add('nv-detail__poster')
      if (!o.isOpen()) history.pushState({ nvDetail: true }, '')
      o.open()
    },
    close: () => o.close(),
    isOpen: () => o.isOpen(),
  }
})()

// ------------------------------------------------------------------------------ settings + about

const settings = (() => {
  const sheet = el('aside', { class: 'nv-sheet', role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'nv-settings-title' })
  const o = overlay(sheet, { onClose: () => { if (state.host) render() } })
  const body = el('div', { class: 'nv-sheet__body' })
  sheet.append(
    el('div', { class: 'nv-sheet__head' }, el('h2', { class: 'nv-sheet__title', id: 'nv-settings-title' }, 'Stream settings'),
      el('button', { type: 'button', class: 'nv-icon-btn', 'aria-label': 'Close', onclick: () => o.close() }, icon('close'))),
    body,
    el('div', { class: 'nv-sheet__foot' }, el('p', { class: 'nv-credit' }, 'Saved in this browser. They apply the next time a game starts.')))
  o.mount()
  const s0 = () => readSettings()
  return {
    open() {
      const s = s0()
      body.replaceChildren(
        el('section', { class: 'nv-section' }, el('h3', { class: 'nv-section__title' }, 'Picture'), qualityFields()),
        el('section', { class: 'nv-section' }, el('h3', { class: 'nv-section__title' }, 'Mouse and touch'),
          el('div', { class: 'nv-field' }, el('span', { class: 'nv-field__label' }, 'Mouse'),
            segmented('Mouse', [{ value: 'follow', label: 'Desktop pointer' }, { value: 'relative', label: 'Captured (games)' }, { value: 'pointAndDrag', label: 'Point and drag' }],
              s.mouseMode === 'localCursor' ? 'follow' : s.mouseMode, (v) => writeSettings({ mouseMode: v }), { block: true }),
            el('span', { class: 'nv-field__hint' }, 'Captured hides the pointer and sends raw movement, which most 3D games need. You can switch while playing.')),
          el('div', { class: 'nv-field' }, el('span', { class: 'nv-field__label' }, 'Touch'),
            segmented('Touch', [{ value: 'mouseRelative', label: 'Trackpad' }, { value: 'pointAndDrag', label: 'Direct pointer' }, { value: 'touch', label: 'Touch' }],
              s.touchMode === 'localCursor' ? 'mouseRelative' : s.touchMode, (v) => writeSettings({ touchMode: v }), { block: true }))),
        el('section', { class: 'nv-section' }, el('h3', { class: 'nv-section__title' }, 'When a game starts'),
          switchRow('Go full screen', 'On the first click or tap in the stream.', s.enterFullscreenOnStreamStart, (v) => writeSettings({ enterFullscreenOnStreamStart: v })),
          switchRow('Keep sound on the PC too', 'Otherwise the PC stays quiet while you stream.', s.playAudioLocal, (v) => writeSettings({ playAudioLocal: v }))),
      )
      o.open()
    },
  }
})()

let about = { open() {} }

// ------------------------------------------------------------------------------ keyboard + gamepad

// Arrow keys (and a gamepad's D-pad or stick) move between tiles and buttons by position.
function focusables() {
  const scope = detail.isOpen() ? document.querySelector('.nv-detail') : document.querySelector('.nv-sheet.is-open, .nv-dialog.is-open') || app
  return [...scope.querySelectorAll('button:not([disabled]), a[href]')].filter((x) => x.offsetParent !== null && !x.closest('[inert]'))
}

function moveFocus(dx, dy) {
  const items = focusables()
  const cur = document.activeElement
  if (!items.includes(cur)) { (items.find((x) => x.classList.contains('nv-tile')) || items[0])?.focus(); return }
  const a = cur.getBoundingClientRect()
  const ax = a.left + a.width / 2
  const ay = a.top + a.height / 2
  let best = null
  let bestScore = Infinity
  for (const it of items) {
    if (it === cur) continue
    const b = it.getBoundingClientRect()
    const bx = b.left + b.width / 2
    const by = b.top + b.height / 2
    const along = dx ? (bx - ax) * dx : (by - ay) * dy
    if (along <= 4) continue
    const across = dx ? Math.abs(by - ay) : Math.abs(bx - ax)
    const score = along + across * 2.5
    if (score < bestScore) { bestScore = score; best = it }
  }
  if (best) { best.focus(); best.scrollIntoView({ block: 'nearest', behavior: 'smooth' }) }
}

document.addEventListener('keydown', (e) => {
  if (e.defaultPrevented || e.altKey || e.ctrlKey || e.metaKey) return
  if (e.target instanceof HTMLInputElement) return
  const dir = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key]
  if (dir && !document.activeElement?.closest('.nv-seg, .nv-menu-pop')) { e.preventDefault(); moveFocus(...dir) }
})

function gamepadLoop() {
  let last = {}
  let repeatAt = 0
  const tick = (t) => {
    const pad = [...(navigator.getGamepads?.() || [])].find(Boolean)
    if (pad) {
      const b = (i) => pad.buttons[i]?.pressed
      const x = pad.axes[0] || 0
      const y = pad.axes[1] || 0
      const now = {
        left: b(14) || x < -0.6, right: b(15) || x > 0.6, up: b(12) || y < -0.6, down: b(13) || y > 0.6,
        a: b(0), b: b(1), start: b(9),
      }
      const edge = (k) => now[k] && !last[k]
      const held = ['left', 'right', 'up', 'down'].find((k) => now[k])
      if (held && (edge(held) || t > repeatAt)) {
        moveFocus(...{ left: [-1, 0], right: [1, 0], up: [0, -1], down: [0, 1] }[held])
        repeatAt = t + (edge(held) ? 380 : 140)
      }
      if (edge('a')) document.activeElement?.click?.()
      if (edge('b')) document.activeElement?.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
      if (edge('start')) settings.open()
      last = now
    }
    requestAnimationFrame(tick)
  }
  requestAnimationFrame(tick)
}
addEventListener('gamepadconnected', () => { if (!gamepadLoop.started) { gamepadLoop.started = true; gamepadLoop(); toast('Controller connected: D-pad to move, A to choose, B to go back') } })

// ------------------------------------------------------------------------------ page

function render() {
  const main = el('main', { class: 'nv-main', id: 'main' })
  if (state.error) {
    main.append(state.error)
  } else {
    main.append(hostCard())
    const now = nowPlaying()
    if (now) main.append(now)
    main.append(el('div', { class: 'nv-lib__head' }, el('h1', { class: 'nv-lib__title' }, 'Library'), count))
    if (state.games.length) {
      renderGrid()
      main.append(grid)
    } else {
      main.append(el('div', { class: 'nv-empty' }, icon('gamepad', 'nv-icon--lg'),
        el('h2', {}, 'No games yet'),
        el('p', {}, "Nova's library is empty, or this browser client isn't allowed to see its games. Add games in Nova, or check the browser client's permissions in Devices."),
        state.session?.nova_url && el('a', { class: 'nv-btn nv-btn--secondary', href: `${state.session.nova_url}library`, target: '_blank', rel: 'noopener' }, icon('external'), 'Open the library in Nova')))
    }
  }
  const foot = el('footer', { class: 'nv-foot' }, creditLine(() => about.open()),
    el('div', { class: 'nv-foot__keys', 'aria-hidden': 'true' },
      el('span', {}, el('kbd', { class: 'nv-kbd' }, '←'), el('kbd', { class: 'nv-kbd' }, '→'), 'Move'),
      el('span', {}, el('kbd', { class: 'nv-kbd' }, 'Enter'), 'Open'),
      el('span', {}, el('kbd', { class: 'nv-kbd' }, 'Esc'), 'Back')))
  const skip = el('a', { class: 'nv-skip', href: '#main' }, 'Skip to games')
  app.replaceChildren(skip, header(), main, foot)
  app.setAttribute('aria-busy', 'false')
}

function errorBlock(title, text, action = 'Try again') {
  return el('div', { class: 'nv-error', role: 'alert' }, icon('alert', 'nv-icon--lg'), el('h2', {}, title), el('p', {}, text),
    el('button', { type: 'button', class: 'nv-btn nv-btn--secondary', onclick: () => (action === 'Reload' ? location.reload() : load()) }, icon('refresh'), action))
}

async function load() {
  if (!state.host) {
    app.replaceChildren(header(), el('main', { class: 'nv-main' },
      el('div', { class: 'nv-host' }, el('span', { class: 'nv-host__icon nv-skeleton' }), el('div', {},
        el('div', { class: 'nv-skeleton', style: { height: '18px', width: '140px' } }),
        el('div', { class: 'nv-skeleton', style: { height: '14px', width: '220px', marginTop: '8px' } }))),
      el('div', { class: 'nv-lib__head' }, el('h1', { class: 'nv-lib__title' }, 'Library')), skeleton()))
  }
  state.error = null
  try {
    const [session, hosts, library] = await Promise.all([
      getJson('/nova/session').catch((e) => { if (handleAuth(e)) throw e; return null }),
      getJson('/api/hosts', { lines: true, timeout: 20000 }),
      getJson('/nova/library').catch(() => ({ ok: false })),
    ])
    state.session = session
    if (!about.root) about = aboutDialog(session)
    const list = hosts?.hosts || []
    state.host = list.find((h) => h.paired === 'Paired') || null
    if (!state.host) {
      state.error = errorBlock('Not paired with Nova yet', 'The browser client pairs with Nova when this page loads. Reload to try again; if it keeps failing, check that Nova is running.', 'Reload')
      return render()
    }
    const id = state.host.host_id
    const [detailed, apps] = await Promise.all([
      getJson(`/api/host?host_id=${id}`, { timeout: 15000 }).then((r) => r.host).catch(() => null),
      state.host.server_state != null ? getJson(`/api/apps?host_id=${id}`, { timeout: 15000 }).then((r) => r.apps || []).catch(() => null) : Promise.resolve([]),
    ])
    state.detailed = detailed
    if (apps == null) {
      state.error = errorBlock("Couldn't load your games", 'Nova answered, but the game list did not arrive. Nova may be busy starting a game.')
      return render()
    }
    state.libraryOk = Boolean(library?.ok)
    const byName = new Map((library?.apps || []).map((a) => [a.name, a]))
    state.games = apps.map((a) => {
      const lib = byName.get(a.title)
      return { app_id: a.app_id, title: a.title, index: lib?.index ?? null, poster: lib?.poster, hero: lib?.hero, logo: lib?.logo, display: lib?.display ?? null }
    })
    render()
  } catch (e) {
    if (handleAuth(e)) return
    state.error = errorBlock("Couldn't reach the browser client", 'The page could not talk to Nova. Check that the PC is on and Nova is running.')
    render()
  }
}

load()
