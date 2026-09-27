<script setup>
/**
 * Nova app shell (SPEC §4): sidebar (desktop ≥1024), icon rail (768–1023) or drawer (<768); the top
 * bar with page title, global search (Ctrl/⌘K command palette) and the page's one primary action;
 * the live strip while a stream runs (every page except Overview); the routed page in <main>; toasts.
 *
 * Pages talk to it through NvPage (title, subtitle and actions are teleported into the top bar).
 * Provides `nvShell` = { page: { title }, live, openDrawer() }.
 */
import { computed, onBeforeUnmount, onMounted, provide, reactive, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { Activity, CircleHelp, Gamepad2, KeyRound, LayoutDashboard, Link2, LogOut, Menu, Search, SlidersHorizontal, Smartphone, X } from '@lucide/vue'
import NovaLogo from './components/NovaLogo.vue'
import NvActionMenu from './components/NvActionMenu.vue'
import NvCommandPalette from './components/NvCommandPalette.vue'
import NvLiveStrip from './components/NvLiveStrip.vue'
import NvToastHost from './components/NvToastHost.vue'
import Notification from '../Notification.vue'
import { fetchJson, getConfig, logout } from './api'
import { useLiveSession } from './live'
import { openPalette, palette } from './search'
import { getThemePreference, setThemePreference } from '../theme'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()
const live = useLiveSession()

const drawerOpen = ref(false)
const username = ref('')
const hostName = ref('')
const reachable = ref(true)
const counts = reactive({ library: null, devices: null })
const theme = ref(getThemePreference())
const menuButton = ref(null)
const sidebar = ref(null)
const page = reactive({ title: '' })

provide('nvShell', { page, live, openDrawer: () => { drawerOpen.value = true } })

const hostNav = computed(() => [
  { key: 'overview', to: '/', label: t('nova.nav.overview'), icon: LayoutDashboard, exact: true },
  { key: 'library', to: '/library', label: t('nova.nav.library'), icon: Gamepad2, count: counts.library },
  { key: 'devices', to: '/devices', label: t('nova.nav.devices'), icon: Smartphone, count: counts.devices },
])
const systemNav = computed(() => [
  { key: 'pair', to: '/pair', label: t('nova.nav.pair'), icon: Link2 },
  { key: 'settings', to: '/settings', label: t('nova.nav.settings'), icon: SlidersHorizontal },
  { key: 'logs', to: '/logs', label: t('nova.nav.logs'), icon: Activity },
])

const hostState = computed(() => {
  if (!reachable.value) return { tone: 'danger', label: t('nova.shell.host_unreachable') }
  if (live.active.value) return { tone: 'success', label: t('nova.shell.host_streaming') }
  return { tone: 'success', label: t('nova.shell.host_ready') }
})
const displayHost = computed(() => hostName.value || globalThis.location?.hostname || 'Nova')
const showLiveStrip = computed(() => live.active.value && route.path !== '/')
const isMac = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform || '')

const accountItems = computed(() => [
  { id: 'theme-system', label: t('nova.theme.system'), checked: theme.value === 'system', onSelect: () => pickTheme('system') },
  { id: 'theme-light', label: t('nova.theme.light'), checked: theme.value === 'light', onSelect: () => pickTheme('light') },
  { id: 'theme-dark', label: t('nova.theme.dark'), checked: theme.value === 'dark', onSelect: () => pickTheme('dark') },
  { divider: true, id: 'sep' },
  { id: 'password', label: t('nova.nav.password'), icon: KeyRound, onSelect: () => router.push('/password') },
  { id: 'signout', label: t('nova.shell.sign_out'), icon: LogOut, onSelect: logout },
])

function pickTheme(value) {
  theme.value = value
  setThemePreference(value)
}

function isActive(item) {
  return item.exact ? route.path === item.to : route.path === item.to || route.path.startsWith(`${item.to}/`)
}

function closeDrawer(returnFocus = false) {
  if (!drawerOpen.value) return
  drawerOpen.value = false
  if (returnFocus) menuButton.value?.focus()
}

const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]), [tabindex]:not([tabindex="-1"])'

// While the drawer is open, Tab cycles between the menu button and the drawer only.
function trapTab(event) {
  const items = [menuButton.value, ...(sidebar.value?.querySelectorAll(FOCUSABLE) || [])].filter(Boolean)
  if (items.length === 0) return
  const first = items[0]
  const last = items[items.length - 1]
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault()
    last.focus()
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault()
    first.focus()
  } else if (!items.includes(document.activeElement)) {
    event.preventDefault()
    first.focus()
  }
}

function onKeydown(event) {
  if ((event.metaKey || event.ctrlKey) && !event.altKey && event.key.toLowerCase() === 'k') {
    event.preventDefault()
    closeDrawer(false)
    if (!palette.open) openPalette()
    return
  }
  if (!drawerOpen.value) return
  if (event.key === 'Escape') closeDrawer(true)
  else if (event.key === 'Tab') trapTab(event)
}

async function refreshCounts() {
  try {
    const data = await fetchJson('./api/apps')
    counts.library = Array.isArray(data?.apps) ? data.apps.length : null
  } catch {
    counts.library = null
  }
  try {
    const data = await fetchJson('./api/clients/list')
    counts.devices = Array.isArray(data?.named_certs) ? data.named_certs.length : null
  } catch {
    counts.devices = null
  }
}

watch(() => route.fullPath, () => closeDrawer(false))
watch(() => route.path, () => refreshCounts())
watch(drawerOpen, (open) => {
  document.documentElement.classList.toggle('nv-scroll-locked', open)
  if (open) requestAnimationFrame(() => sidebar.value?.querySelector('a, button')?.focus())
})

// The drawer only exists below 768px; widening the window closes it so nothing stays inert.
const wideQuery = globalThis.matchMedia?.('(min-width: 768px)')
function onWide(event) {
  if (event.matches) closeDrawer(false)
}

onMounted(async () => {
  document.addEventListener('keydown', onKeydown)
  wideQuery?.addEventListener?.('change', onWide)
  refreshCounts()
  try {
    const config = await getConfig()
    username.value = config.username || ''
    hostName.value = config.sunshine_name || ''
    reachable.value = true
  } catch {
    reachable.value = false
  }
})
onBeforeUnmount(() => {
  document.removeEventListener('keydown', onKeydown)
  wideQuery?.removeEventListener?.('change', onWide)
  document.documentElement.classList.remove('nv-scroll-locked')
})
</script>

<template>
  <div :class="['nv-shell', { 'nv-shell--drawer-open': drawerOpen }]">
    <a class="nv-skip" href="#nv-main">{{ t('nova.shell.skip_to_content') }}</a>

    <div v-if="drawerOpen" class="nv-shell__backdrop" aria-hidden="true" @click="closeDrawer(true)"></div>

    <aside id="nv-sidebar" ref="sidebar" class="nv-sidebar">
      <RouterLink to="/" class="nv-host" :aria-label="t('nova.shell.host_label', { name: displayHost, state: hostState.label })">
        <NovaLogo :size="26" :wordmark="false" />
        <span class="nv-host__text" aria-hidden="true">
          <span class="nv-host__name">{{ displayHost }}</span>
          <span class="nv-host__state"><span :class="['nv-host__dot', `nv-host__dot--${hostState.tone}`]"></span>{{ hostState.label }}</span>
        </span>
      </RouterLink>

      <nav :aria-label="t('nova.shell.main_nav')" class="nv-sidebar__nav">
        <span class="nv-sidebar__group" aria-hidden="true">{{ t('nova.nav.group_host') }}</span>
        <RouterLink v-for="item in hostNav" :key="item.key" :to="item.to" :title="item.label"
                    :class="['nv-nav-link', { 'nv-nav-link--active': isActive(item) }]"
                    :aria-current="isActive(item) ? 'page' : null">
          <component :is="item.icon" :size="18" aria-hidden="true" />
          <span class="nv-nav-link__label">{{ item.label }}</span>
          <span v-if="item.count !== null && item.count !== undefined" class="nv-nav-link__count">{{ item.count }}</span>
        </RouterLink>
        <span class="nv-sidebar__group" aria-hidden="true">{{ t('nova.nav.group_system') }}</span>
        <RouterLink v-for="item in systemNav" :key="item.key" :to="item.to" :title="item.label"
                    :class="['nv-nav-link', { 'nv-nav-link--active': isActive(item) }]"
                    :aria-current="isActive(item) ? 'page' : null">
          <component :is="item.icon" :size="18" aria-hidden="true" />
          <span class="nv-nav-link__label">{{ item.label }}</span>
        </RouterLink>
      </nav>

      <div class="nv-account">
        <span class="nv-account__avatar" aria-hidden="true">{{ (username || '?').charAt(0).toUpperCase() }}</span>
        <span class="nv-account__name">{{ username }}</span>
        <RouterLink to="/help" class="nv-account__icon" :aria-label="t('nova.nav.help')" :title="t('nova.nav.help')"
                    :aria-current="route.path.startsWith('/help') ? 'page' : null">
          <CircleHelp :size="18" aria-hidden="true" />
        </RouterLink>
        <NvActionMenu :label="t('nova.shell.account_menu')" :items="accountItems" align="start" />
      </div>
    </aside>

    <div class="nv-content">
      <header class="nv-topbar">
        <button ref="menuButton" type="button" class="nv-topbar__menu" :aria-expanded="drawerOpen ? 'true' : 'false'"
                aria-controls="nv-sidebar" :aria-label="drawerOpen ? t('nova.shell.close_menu') : t('nova.shell.open_menu')"
                @click="drawerOpen = !drawerOpen">
          <X v-if="drawerOpen" :size="22" aria-hidden="true" />
          <Menu v-else :size="22" aria-hidden="true" />
        </button>
        <div class="nv-topbar__heading" :inert="drawerOpen || null">
          <h1 v-if="page.title" class="nv-topbar__title">{{ page.title }}</h1>
          <div id="nv-topbar-sub" class="nv-topbar__sub"></div>
        </div>
        <div class="nv-topbar__end" :inert="drawerOpen || null">
          <button type="button" class="nv-topsearch" :aria-label="t('nova.search.open')" @click="openPalette()">
            <Search :size="16" aria-hidden="true" />
            <span class="nv-topsearch__text">{{ t('nova.search.placeholder') }}</span>
            <kbd class="nv-topsearch__kbd" aria-hidden="true">{{ isMac ? '⌘ K' : 'Ctrl K' }}</kbd>
          </button>
          <div id="nv-topbar-actions" class="nv-topbar__actions"></div>
        </div>
      </header>

      <main id="nv-main" class="nv-main" tabindex="-1" :inert="drawerOpen || null">
        <div v-if="showLiveStrip" class="nv-main__live">
          <NvLiveStrip :session="live.current.value" />
        </div>
        <slot />
      </main>
    </div>

    <NvCommandPalette />
    <NvToastHost />
    <Notification />
  </div>
</template>

<style>
@layer components {
  .nv-shell {
    display: flex;
    min-height: 100vh;
    background: var(--nv-bg);
  }

  .nv-skip {
    position: absolute;
    left: var(--nv-space-3);
    top: -60px;
    z-index: 1300;
    padding: var(--nv-space-2) var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    background: var(--nv-accent);
    color: var(--nv-on-accent);
    font-weight: 500;
  }

  .nv-skip:focus {
    top: var(--nv-space-3);
    color: var(--nv-on-accent);
  }

  /* ------------------------------------------------------------ sidebar */
  .nv-sidebar {
    position: sticky;
    top: 0;
    width: var(--nv-sidebar-width);
    height: 100vh;
    flex-shrink: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 14px var(--nv-space-3) var(--nv-space-4);
    overflow-y: auto;
    background: var(--nv-sidebar);
    border-right: 1px solid var(--nv-border);
  }

  .nv-host {
    display: flex;
    align-items: center;
    gap: 10px;
    min-height: 52px;
    margin-bottom: 14px;
    padding: var(--nv-space-2) 10px;
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
    color: var(--nv-text);
  }

  .nv-host:hover {
    border-color: var(--nv-chip-border);
    text-decoration: none;
  }

  .nv-host__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
    line-height: 1.25;
  }

  .nv-host__name {
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-host__state {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-host__dot {
    width: 6px;
    height: 6px;
    flex-shrink: 0;
    border-radius: 3px;
    background: var(--nv-success);
  }

  .nv-host__dot--danger {
    background: var(--nv-danger);
  }

  .nv-sidebar__nav {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nv-sidebar__group {
    padding: 10px 10px 6px;
    font-size: var(--nv-text-2xs);
    font-weight: 500;
    line-height: 16px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--nv-text-muted);
  }

  .nv-sidebar__group:not(:first-child) {
    padding-top: 18px;
  }

  .nv-nav-link {
    display: flex;
    align-items: center;
    gap: 10px;
    min-height: 36px;
    padding: 0 10px;
    border-radius: var(--nv-radius-md);
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-md);
    transition: background-color 150ms ease-out, color 150ms ease-out;
  }

  .nv-nav-link:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
    text-decoration: none;
  }

  .nv-nav-link--active,
  .nv-nav-link--active:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
    font-weight: 500;
  }

  .nv-nav-link__label {
    flex-grow: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-nav-link__count {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    font-variant-numeric: tabular-nums;
    color: var(--nv-text-muted);
  }

  .nv-account {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-top: auto;
    padding: 10px 6px 0 10px;
    border-top: 1px solid var(--nv-border);
  }

  .nv-account__avatar {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 28px;
    height: 28px;
    flex-shrink: 0;
    border-radius: 14px;
    background: var(--nv-border);
    font-size: var(--nv-text-xs);
    font-weight: 600;
  }

  .nv-account__name {
    flex-grow: 1;
    min-width: 0;
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-account__icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 32px;
    height: 32px;
    flex-shrink: 0;
    border-radius: var(--nv-radius-md);
    color: var(--nv-text-secondary);
  }

  .nv-account__icon:hover,
  .nv-account__icon[aria-current="page"] {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  /* ------------------------------------------------------------ content */
  .nv-content {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-topbar {
    position: sticky;
    top: 0;
    z-index: 1010;
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    height: var(--nv-topbar-height);
    padding: 0 var(--nv-space-8);
    border-bottom: 1px solid var(--nv-border);
    background: var(--nv-bg);
  }

  .nv-topbar__menu {
    display: none;
  }

  .nv-topbar__heading {
    display: flex;
    align-items: baseline;
    gap: var(--nv-space-3);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-topbar__title {
    font-size: var(--nv-text-lg);
    line-height: 20px;
    white-space: nowrap;
  }

  .nv-topbar__sub {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-width: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
    overflow: hidden;
    white-space: nowrap;
  }

  .nv-topbar__end {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
  }

  .nv-topbar__actions {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-topbar__actions:empty {
    display: none;
  }

  .nv-topsearch {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    width: 300px;
    height: 36px;
    padding: 0 6px 0 var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-surface);
    color: var(--nv-text-muted);
    font: inherit;
    font-size: var(--nv-text-sm);
    cursor: text;
    text-align: left;
  }

  .nv-topsearch:hover {
    border-color: var(--nv-text-muted);
  }

  .nv-topsearch__text {
    flex-grow: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-topsearch__kbd {
    padding: 2px 6px;
    border-radius: 4px;
    border: 1px solid var(--nv-chip-border);
    color: var(--nv-text-muted);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-2xs);
    line-height: 14px;
  }

  .nv-main {
    flex-grow: 1;
    min-width: 0;
    outline: none;
  }

  .nv-main__live {
    padding: var(--nv-space-6) var(--nv-space-8) 0;
  }

  .nv-shell__backdrop {
    display: none;
  }

  /* ------------------------------------------------------ tablet: rail */
  @media (min-width: 768px) and (max-width: 1023px) {
    .nv-sidebar {
      width: var(--nv-rail-width);
      padding: 14px var(--nv-space-2) var(--nv-space-3);
      align-items: center;
    }

    .nv-host {
      justify-content: center;
      width: 44px;
      min-height: 44px;
      padding: 0;
    }

    .nv-host__text,
    .nv-sidebar__group,
    .nv-nav-link__label,
    .nv-nav-link__count,
    .nv-account__name,
    .nv-account__avatar {
      display: none;
    }

    .nv-nav-link {
      justify-content: center;
      width: 44px;
      min-height: 44px;
      padding: 0;
    }

    .nv-sidebar__nav {
      gap: var(--nv-space-1);
    }

    .nv-account {
      flex-direction: column;
      gap: var(--nv-space-1);
      padding: var(--nv-space-2) 0 0;
    }

    .nv-topbar {
      padding: 0 var(--nv-space-6);
    }

    .nv-topsearch {
      width: 220px;
    }

    .nv-main__live {
      padding: var(--nv-space-5) var(--nv-space-6) 0;
    }
  }

  /* ------------------------------------------------------ phone: drawer */
  @media (max-width: 767px) {
    .nv-shell {
      flex-direction: column;
    }

    .nv-topbar {
      height: var(--nv-topbar-height-phone);
      gap: var(--nv-space-2);
      padding: 0 var(--nv-space-2);
    }

    .nv-topbar__menu {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 44px;
      height: 44px;
      flex-shrink: 0;
      border: 0;
      border-radius: var(--nv-radius-md);
      background: transparent;
      color: var(--nv-text);
      cursor: pointer;
    }

    .nv-topbar__sub {
      display: none;
    }

    .nv-topsearch {
      width: 44px;
      height: 44px;
      justify-content: center;
      padding: 0;
      border-color: transparent;
      background: transparent;
      color: var(--nv-text-secondary);
    }

    .nv-topsearch__text,
    .nv-topsearch__kbd {
      display: none;
    }

    .nv-topbar__actions .nv-btn {
      min-height: 44px;
    }

    .nv-sidebar {
      position: fixed;
      top: var(--nv-topbar-height-phone);
      left: 0;
      z-index: 1020;
      height: calc(100vh - var(--nv-topbar-height-phone));
      width: min(300px, 86vw);
      transform: translateX(-100%);
      visibility: hidden;
      transition: transform 200ms var(--nv-ease), visibility 0s linear 200ms;
    }

    .nv-shell--drawer-open .nv-sidebar {
      transform: none;
      visibility: visible;
      overscroll-behavior: contain;
      transition: transform 200ms var(--nv-ease);
    }

    .nv-nav-link,
    .nv-account__icon {
      min-height: 44px;
    }

    .nv-account__icon {
      width: 44px;
    }

    .nv-shell__backdrop {
      display: block;
      position: fixed;
      inset: var(--nv-topbar-height-phone) 0 0;
      z-index: 1015;
      background: var(--nv-scrim);
    }

    .nv-main__live {
      padding: 0;
    }
  }
}
</style>
