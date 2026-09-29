<script setup>
/**
 * Play in browser (/browser): switch Nova's browser client on or off, see where it listens and who
 * may reach it, and start a game in this tab. A game link opens the browser client on the same
 * host name, so this page's sign-in carries over and nothing else has to be typed.
 *
 * `?continue=/path` (set by the browser client's "Sign in with Nova" link) sends the browser
 * straight back to the browser client once signed in and the client is running.
 */
import { computed, onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRoute } from 'vue-router'
import { ExternalLink, Globe, MonitorPlay } from '@lucide/vue'
import NvAlert from '../components/NvAlert.vue'
import NvArt from '../components/NvArt.vue'
import NvButton from '../components/NvButton.vue'
import NvCard from '../components/NvCard.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import NvPage from '../components/NvPage.vue'
import NvSegmentedControl from '../components/NvSegmentedControl.vue'
import NvSkeleton from '../components/NvSkeleton.vue'
import NvStatusDot from '../components/NvStatusDot.vue'
import NvSwitch from '../components/NvSwitch.vue'
import { fetchJson } from '../api'
import { toast } from '../toast'
import { artUrl } from './library/libraryApi'
import { UPSTREAM, continuePath, getWebClient, playInBrowserUrl, setWebClient, webClientBase } from '../webClient'

const { t } = useI18n()
const route = useRoute()

const status = ref(null)
const statusError = ref('')
const switching = ref(false)
const apps = shallowRef(null)
const appsError = ref(false)
const mode = ref('virtual')
const continuing = ref(false)
let poll = null

const enabled = computed({
  get: () => Boolean(status.value?.enabled),
  set: (value) => toggle(value),
})
const running = computed(() => status.value?.state === 'running')
const base = computed(() => webClientBase(window.location, status.value?.port))
const target = computed(() => continuePath(route.query.continue))
const modes = computed(() => [
  { value: 'virtual', label: t('nova.browser.virtual') },
  { value: 'mirror', label: t('nova.browser.mirror') },
])
const stateView = computed(() => {
  const s = status.value
  if (!s) return null
  if (!s.installed) return { status: 'neutral', label: t('nova.browser.state_not_installed') }
  switch (s.state) {
    case 'running': return { status: 'success', label: t('nova.browser.state_running') }
    case 'starting': return { status: 'warning', label: t('nova.browser.state_starting') }
    case 'failed': return { status: 'danger', label: t('nova.browser.state_failed', { error: s.last_error || '?' }) }
    default: return { status: 'neutral', label: t('nova.browser.state_off') }
  }
})
const who = computed(() => (status.value?.allowed === 'pc' ? t('nova.browser.who_pc') : t('nova.browser.who_lan')))

async function refresh() {
  try {
    status.value = await getWebClient()
    statusError.value = ''
  } catch (e) {
    statusError.value = e?.status === 404 ? t('nova.browser.no_api') : t('nova.browser.status_failed')
  }
}

async function toggle(value) {
  switching.value = true
  try {
    status.value = await setWebClient(value)
    toast.success(value ? t('nova.browser.switched_on') : t('nova.browser.switched_off'))
  } catch {
    toast.danger(t('nova.browser.switch_failed'))
    await refresh()
  } finally {
    switching.value = false
  }
}

function playUrl(app) {
  return playInBrowserUrl(app.name, mode.value, { port: status.value?.port })
}

// Poll while the sidecar is starting (or restarting), so the page notices when it is up.
watch(() => status.value?.state, (state) => {
  clearInterval(poll)
  poll = null
  if (state === 'starting' || state === 'failed') poll = setInterval(refresh, 1500)
})

// Back from Nova's sign-in page: return to where the browser client sent us.
watch([running, target], ([isRunning, path]) => {
  if (isRunning && path && !continuing.value) {
    continuing.value = true
    window.location.assign(base.value + path)
  }
})

onMounted(async () => {
  await refresh()
  try {
    const body = await fetchJson('./api/apps')
    apps.value = (body?.apps || []).map((app, index) => ({ app, index }))
  } catch {
    appsError.value = true
  }
})
onBeforeUnmount(() => clearInterval(poll))
</script>

<template>
  <NvPage :title="t('nova.browser.page_title')">
    <template #subtitle>{{ t('nova.browser.page_subtitle') }}</template>

    <NvAlert v-if="statusError" variant="danger" live>{{ statusError }}</NvAlert>

    <NvCard :title="t('nova.browser.client_title')">
      <div v-if="!status && !statusError" class="nv-bp__loading"><NvSkeleton width="60%" /><NvSkeleton width="40%" /></div>
      <div v-else-if="status" class="nv-bp__client">
        <div class="nv-bp__row">
          <div class="nv-bp__text">
            <p id="nv-bp-switch" class="nv-bp__label">{{ t('nova.browser.switch_label') }}</p>
            <p id="nv-bp-switch-desc" class="nv-bp__hint">{{ t('nova.browser.switch_desc') }}</p>
          </div>
          <NvSwitch v-model="enabled" labelledby="nv-bp-switch" describedby="nv-bp-switch-desc" show-state
                    id="nv-bp-toggle" :disabled="switching || !status.installed" />
        </div>
        <NvStatusDot v-if="stateView" :status="stateView.status" :label="stateView.label" data-nv-web-client-state />
        <NvAlert v-if="!status.installed" variant="info">{{ t('nova.browser.not_installed_hint') }}</NvAlert>
        <dl v-if="status.installed" class="nv-bp__facts">
          <div><dt>{{ t('nova.browser.address') }}</dt><dd><code data-nv-web-client-url>{{ base }}</code></dd></div>
          <div><dt>{{ t('nova.browser.who') }}</dt><dd>{{ who }}</dd></div>
          <div><dt>{{ t('nova.browser.signin') }}</dt><dd>{{ t('nova.browser.signin_desc') }}</dd></div>
          <div><dt>{{ t('nova.browser.ports') }}</dt><dd>{{ t('nova.browser.ports_desc', { tcp: status.port, udp: `${status.udp_min}–${status.udp_max}` }) }}</dd></div>
        </dl>
        <div v-if="running" class="nv-bp__actions">
          <NvButton :href="base + '/'" :icon="ExternalLink" data-nv-web-client-open>{{ t('nova.browser.open_client') }}</NvButton>
        </div>
        <p class="nv-bp__credit">
          {{ t('nova.browser.credit_before') }}
          <a :href="UPSTREAM.url" target="_blank" rel="noopener noreferrer">{{ UPSTREAM.name }}</a>
          {{ t('nova.browser.credit_after', { licence: UPSTREAM.licence }) }}
          <span v-if="status.version" class="nv-bp__version">({{ status.version }})</span>
        </p>
      </div>
    </NvCard>

    <NvAlert v-if="continuing" variant="info" live>{{ t('nova.browser.continuing') }}</NvAlert>

    <NvCard :title="t('nova.browser.games_title')">
      <div class="nv-bp__mode">
        <span id="nv-bp-display" class="nv-bp__label">{{ t('nova.browser.display') }}</span>
        <NvSegmentedControl v-model="mode" labelledby="nv-bp-display" :options="modes" />
        <p class="nv-bp__hint">{{ mode === 'virtual' ? t('nova.browser.virtual_hint') : t('nova.browser.mirror_hint') }}</p>
      </div>
      <NvAlert v-if="status && !running" variant="warning">{{ t('nova.browser.games_need_client') }}</NvAlert>
      <NvAlert v-if="appsError" variant="danger">{{ t('nova.browser.apps_failed') }}</NvAlert>
      <div v-else-if="apps === null" class="nv-bp__loading"><NvSkeleton width="100%" height="120px" /></div>
      <NvEmptyState v-else-if="!apps.length" :title="t('nova.browser.no_games')">
        <template #icon><MonitorPlay :size="28" /></template>
      </NvEmptyState>
      <ul v-else class="nv-bp__games" data-nv-browser-games>
        <li v-for="{ app, index } in apps" :key="index">
          <a v-if="running" class="nv-bp__game" :href="playUrl(app)" data-nv-browser-play>
            <NvArt :title="app.name || '?'" :src="artUrl(app, index, 'poster')" :show-title="false" decorative />
            <span class="nv-bp__game-name">{{ app.name }}</span>
          </a>
          <span v-else class="nv-bp__game nv-bp__game--off" aria-disabled="true">
            <NvArt :title="app.name || '?'" :src="artUrl(app, index, 'poster')" :show-title="false" decorative />
            <span class="nv-bp__game-name">{{ app.name }}</span>
          </span>
        </li>
      </ul>
      <p class="nv-bp__hint nv-bp__tip"><Globe :size="14" aria-hidden="true" /> {{ t('nova.browser.tip') }}</p>
    </NvCard>
  </NvPage>
</template>

<style>
@layer components {
  .nv-bp__client,
  .nv-bp__loading {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-bp__row {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: var(--nv-space-4);
  }

  .nv-bp__label {
    margin: 0;
    font-weight: 500;
  }

  .nv-bp__hint {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-bp__facts {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: var(--nv-space-3) var(--nv-space-5);
    margin: 0;
  }

  .nv-bp__facts dt {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-xs);
  }

  .nv-bp__facts dd {
    margin: 0;
    font-size: var(--nv-text-sm);
    overflow-wrap: anywhere;
  }

  .nv-bp__actions {
    display: flex;
    gap: var(--nv-space-2);
  }

  .nv-bp__credit {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-xs);
  }

  .nv-bp__mode {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-2);
    margin-bottom: var(--nv-space-4);
  }

  .nv-bp__games {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(132px, 1fr));
    gap: var(--nv-space-4);
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-bp__game {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    color: inherit;
    text-decoration: none;
    border-radius: var(--nv-radius-md);
  }

  .nv-bp__game .nv-art {
    aspect-ratio: 2 / 3;
    width: 100%;
    transition: transform 120ms ease-out;
  }

  a.nv-bp__game:hover .nv-art,
  a.nv-bp__game:focus-visible .nv-art {
    transform: translateY(-2px);
  }

  a.nv-bp__game:focus-visible {
    outline: 2px solid var(--nv-focus);
    outline-offset: 3px;
  }

  .nv-bp__game--off {
    opacity: 0.55;
    cursor: not-allowed;
  }

  .nv-bp__game-name {
    font-size: var(--nv-text-sm);
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-bp__tip {
    display: flex;
    align-items: center;
    gap: var(--nv-space-1);
    margin-top: var(--nv-space-4);
  }

  @media (prefers-reduced-motion: reduce) {
    .nv-bp__game .nv-art {
      transition: none;
    }
  }
}
</style>
