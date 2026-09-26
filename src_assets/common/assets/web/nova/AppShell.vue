<script setup>
/**
 * Nova app shell: skip link, sidebar navigation (a drawer below 900px), the routed page in
 * <main>, the toast host and the legacy notification area.
 */
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { Activity, CircleHelp, Gamepad2, LayoutDashboard, LogOut, Menu, SlidersHorizontal, Smartphone, X } from '@lucide/vue'
import NovaLogo from './components/NovaLogo.vue'
import NvThemeSwitcher from './components/NvThemeSwitcher.vue'
import NvToastHost from './components/NvToastHost.vue'
import Notification from '../Notification.vue'
import { getConfig, logout } from './api'

const { t } = useI18n()
const route = useRoute()

const drawerOpen = ref(false)
const username = ref('')
const menuButton = ref(null)
const sidebar = ref(null)

const primaryNav = computed(() => [
  { to: '/', label: t('nova.nav.dashboard'), icon: LayoutDashboard, exact: true },
  { to: '/apps', label: t('nova.nav.applications'), icon: Gamepad2 },
  { to: '/devices', label: t('nova.nav.devices'), icon: Smartphone },
  { to: '/settings', label: t('nova.nav.settings'), icon: SlidersHorizontal },
  { to: '/logs', label: t('nova.nav.logs'), icon: Activity },
])

function isActive(item) {
  return item.exact ? route.path === item.to : route.path === item.to || route.path.startsWith(`${item.to}/`)
}

function closeDrawer(returnFocus = false) {
  if (!drawerOpen.value) return
  drawerOpen.value = false
  if (returnFocus) menuButton.value?.focus()
}

function onKeydown(event) {
  if (event.key === 'Escape' && drawerOpen.value) closeDrawer(true)
}

watch(() => route.fullPath, () => closeDrawer(false))
watch(drawerOpen, (open) => {
  if (open) requestAnimationFrame(() => sidebar.value?.querySelector('a, button')?.focus())
})

onMounted(async () => {
  document.addEventListener('keydown', onKeydown)
  try {
    const config = await getConfig()
    username.value = config.username || ''
  } catch {
    // The account row just omits the name.
  }
})
onBeforeUnmount(() => document.removeEventListener('keydown', onKeydown))
</script>

<template>
  <div :class="['nv-shell', { 'nv-shell--drawer-open': drawerOpen }]">
    <a class="nv-skip" href="#nv-main">{{ t('nova.shell.skip_to_content') }}</a>

    <header class="nv-topbar">
      <button ref="menuButton" type="button" class="nv-topbar__menu" :aria-expanded="drawerOpen ? 'true' : 'false'"
              aria-controls="nv-sidebar" :aria-label="drawerOpen ? t('nova.shell.close_menu') : t('nova.shell.open_menu')"
              @click="drawerOpen = !drawerOpen">
        <X v-if="drawerOpen" :size="22" aria-hidden="true" />
        <Menu v-else :size="22" aria-hidden="true" />
      </button>
      <RouterLink to="/" class="nv-topbar__brand"><NovaLogo :size="24" /></RouterLink>
    </header>

    <div v-if="drawerOpen" class="nv-shell__backdrop" aria-hidden="true" @click="closeDrawer(true)"></div>

    <aside id="nv-sidebar" ref="sidebar" class="nv-sidebar">
      <RouterLink to="/" class="nv-sidebar__brand"><NovaLogo /></RouterLink>
      <nav :aria-label="t('nova.shell.main_nav')" class="nv-sidebar__nav">
        <RouterLink v-for="item in primaryNav" :key="item.to" :to="item.to"
                    :class="['nv-nav-link', { 'nv-nav-link--active': isActive(item) }]"
                    :aria-current="isActive(item) ? 'page' : null">
          <component :is="item.icon" :size="18" aria-hidden="true" />
          <span>{{ item.label }}</span>
        </RouterLink>
      </nav>
      <div class="nv-sidebar__bottom">
        <RouterLink to="/help" :class="['nv-nav-link', { 'nv-nav-link--active': route.path.startsWith('/help') }]"
                    :aria-current="route.path.startsWith('/help') ? 'page' : null">
          <CircleHelp :size="18" aria-hidden="true" />
          <span>{{ t('nova.nav.help') }}</span>
        </RouterLink>
        <div class="nv-sidebar__theme"><NvThemeSwitcher /></div>
        <div class="nv-account">
          <div class="nv-account__avatar" aria-hidden="true">{{ (username || '?').charAt(0).toUpperCase() }}</div>
          <div class="nv-account__text">
            <RouterLink v-if="username" to="/password" class="nv-account__name" :title="t('nova.nav.password')">{{ username }}</RouterLink>
            <button type="button" class="nv-account__signout" @click="logout">
              <LogOut :size="14" aria-hidden="true" />{{ t('nova.shell.sign_out') }}
            </button>
          </div>
        </div>
      </div>
    </aside>

    <main id="nv-main" class="nv-main" tabindex="-1">
      <slot />
    </main>

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

  .nv-topbar {
    display: none;
  }

  .nv-sidebar {
    position: sticky;
    top: 0;
    width: var(--nv-sidebar-width);
    height: 100vh;
    flex-shrink: 0;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    padding: var(--nv-space-5) var(--nv-space-3);
    overflow-y: auto;
    background: var(--nv-sidebar);
    border-right: 1px solid var(--nv-border);
  }

  .nv-sidebar__brand {
    display: flex;
    padding: var(--nv-space-1) 10px var(--nv-space-5);
    color: var(--nv-text);
  }

  .nv-sidebar__brand:hover {
    text-decoration: none;
  }

  .nv-sidebar__nav {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
  }

  .nv-nav-link {
    display: flex;
    align-items: center;
    gap: 10px;
    min-height: var(--nv-control-height);
    padding: 0 10px;
    border-radius: var(--nv-radius-md);
    color: var(--nv-text-secondary);
    font-weight: 400;
  }

  .nv-nav-link:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
    text-decoration: none;
  }

  .nv-nav-link--active,
  .nv-nav-link--active:hover {
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
    font-weight: 500;
  }

  .nv-sidebar__bottom {
    margin-top: auto;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    padding-top: var(--nv-space-4);
  }

  .nv-sidebar__theme {
    padding: 0 6px;
  }

  .nv-account {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: var(--nv-space-3) 10px 0;
    border-top: 1px solid var(--nv-border);
  }

  .nv-account__avatar {
    width: 32px;
    height: 32px;
    flex-shrink: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 16px;
    background: var(--nv-raised);
    border: 1px solid var(--nv-border);
    font-size: var(--nv-text-sm);
    font-weight: 600;
  }

  .nv-account__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
    line-height: 1.3;
  }

  .nv-account__name {
    color: var(--nv-text);
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .nv-account__signout {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    padding: 2px 0;
    border: 0;
    background: transparent;
    color: var(--nv-text-muted);
    font: inherit;
    font-size: var(--nv-text-xs);
    cursor: pointer;
  }

  .nv-account__signout:hover {
    color: var(--nv-text);
    text-decoration: underline;
  }

  .nv-main {
    flex-grow: 1;
    min-width: 0;
    outline: none;
  }

  .nv-shell__backdrop {
    display: none;
  }

  @media (max-width: 899px) {
    .nv-shell {
      flex-direction: column;
    }

    .nv-topbar {
      position: sticky;
      top: 0;
      z-index: 1010;
      display: flex;
      align-items: center;
      gap: var(--nv-space-2);
      height: 56px;
      padding: 0 var(--nv-space-2);
      background: var(--nv-sidebar);
      border-bottom: 1px solid var(--nv-border);
    }

    .nv-topbar__menu {
      width: 44px;
      height: 44px;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      border: 0;
      border-radius: var(--nv-radius-md);
      background: transparent;
      color: var(--nv-text);
      cursor: pointer;
    }

    .nv-topbar__brand {
      display: flex;
      color: var(--nv-text);
    }

    .nv-topbar__brand:hover {
      text-decoration: none;
    }

    .nv-sidebar {
      position: fixed;
      top: 56px;
      left: 0;
      z-index: 1020;
      height: calc(100vh - 56px);
      width: min(300px, 86vw);
      transform: translateX(-100%);
      visibility: hidden;
      transition: transform 160ms ease, visibility 0s linear 160ms;
    }

    .nv-sidebar__brand {
      display: none;
    }

    .nv-shell--drawer-open .nv-sidebar {
      transform: none;
      visibility: visible;
      transition: transform 160ms ease;
    }

    .nv-shell__backdrop {
      display: block;
      position: fixed;
      inset: 56px 0 0;
      z-index: 1015;
      background: rgba(8, 9, 11, 0.55);
    }
  }
}
</style>
