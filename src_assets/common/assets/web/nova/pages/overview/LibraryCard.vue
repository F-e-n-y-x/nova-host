<script setup>
/**
 * Library card (Final_Dashboard): the first few applications with their icon art, what kind they
 * are, a "Running" line for the running one and a ⋯ menu (Edit, Play in browser, and Close for
 * the running app).
 *
 * Props: data ({ apps, runningIndex, runningName } or null), loading, error.
 * Emits: close (running app), retry.
 */
import { computed, reactive } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import { Gamepad2 } from '@lucide/vue'
import NvCard from '../../components/NvCard.vue'
import NvArt from '../../components/NvArt.vue'
import NvActionMenu from '../../components/NvActionMenu.vue'
import NvButton from '../../components/NvButton.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvEmptyState from '../../components/NvEmptyState.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import PlayInBrowserDialog from '../library/PlayInBrowserDialog.vue'
import { appKind } from './format'

const PREVIEW = 7

const props = defineProps({
  data: { type: Object, default: null },
  loading: { type: Boolean, default: false },
  error: { type: [Object, Error, String], default: null },
})
const emit = defineEmits(['close', 'retry'])

const { t } = useI18n()
const router = useRouter()

const total = computed(() => props.data?.apps?.length ?? 0)

const SOURCE_LABELS = { lutris: 'Lutris', steam: 'Steam', heroic: 'Heroic', folder: '' }

function isRunning(app, index) {
  if (props.data?.runningIndex === index) return true
  return props.data?.runningIndex === null && props.data?.runningName && props.data.runningName === app.name
}

function subtitle(app, kind) {
  const source = SOURCE_LABELS[app['nova-source']]
  if (source) return /umu-run|run-windows-exe/.test(app.cmd || '') ? `${source} · GE-Proton` : source
  if (kind === 'desktop') return t('nova.overview.your_desktop')
  if (/lutris/i.test(app.cmd || '')) return 'Lutris'
  if (/steam/i.test(app.cmd || '')) return 'Steam'
  return t(`nova.overview.kind_${kind}`)
}

function iconUrl(app, index) {
  if (app['nova-icon']) return `./api/covers/${index}/icon`
  if (app['image-path']) return `./api/covers/${index}`
  return ''
}

const rows = computed(() => {
  const list = (props.data?.apps || []).map((app, index) => {
    const kind = appKind(app)
    const running = isRunning(app, index)
    return { app, index, kind, running, sub: running ? t('nova.overview.running') : subtitle(app, kind), icon: iconUrl(app, index) }
  })
  list.sort((a, b) => Number(b.running) - Number(a.running))
  return list.slice(0, PREVIEW)
})

function menu(row) {
  const items = [
    { id: 'edit', label: t('nova.overview.edit'), onSelect: () => router.push({ path: '/library', query: { edit: String(row.index) } }) },
    { id: 'browser', label: t('nova.overview.play_browser'), onSelect: () => playInBrowser(row.app) },
  ]
  if (row.running) items.push({ id: 'close', label: t('nova.overview.close_app'), danger: true, onSelect: () => emitClose(row) })
  return items
}

const browserPlay = reactive({ open: false, app: null })

function playInBrowser(app) {
  browserPlay.app = app
  browserPlay.open = true
}

function emitClose(row) {
  emit('close', row.app)
}
</script>

<template>
  <NvCard :title="t('nova.overview.library_title')" flush class="nv-lib">
    <template #actions>
      <span v-if="total" class="nv-muted">{{ t('nova.overview.apps_count', { n: total }, total) }}</span>
      <RouterLink to="/library">{{ t('nova.overview.view_all') }}</RouterLink>
    </template>

    <div v-if="loading" class="nv-lib__pad" aria-busy="true">
      <span class="nv-visually-hidden">{{ t('nova.overview.loading_apps') }}</span>
      <NvSkeleton v-for="i in 4" :key="i" height="40px" />
    </div>
    <div v-else-if="error" class="nv-lib__pad">
      <NvAlert variant="danger" :title="t('nova.overview.apps_failed')">
        <template #actions><NvButton size="sm" @click="$emit('retry')">{{ t('nova.common.retry') }}</NvButton></template>
      </NvAlert>
    </div>
    <NvEmptyState v-else-if="!total" compact :title="t('nova.overview.no_apps')" :description="t('nova.overview.no_apps_desc')">
      <template #icon><Gamepad2 :size="28" /></template>
      <template #actions><NvButton size="sm" variant="primary" to="/library">{{ t('nova.overview.add_games') }}</NvButton></template>
    </NvEmptyState>
    <ul v-else class="nv-lib__list">
      <li v-for="row in rows" :key="`${row.index}-${row.app.name}`" class="nv-lib__row">
        <RouterLink :to="{ path: '/library', query: { edit: String(row.index) } }" class="nv-lib__main">
          <NvArt :title="row.app.name || '?'" :src="row.icon" kind="icon" decorative class="nv-lib__icon" />
          <span class="nv-lib__text">
            <span class="nv-lib__name" :title="row.app.name">{{ row.app.name }}</span>
            <span :class="['nv-lib__sub', { 'nv-lib__sub--running': row.running }]">{{ row.sub }}</span>
          </span>
        </RouterLink>
        <span class="nv-lib__kind">{{ t(`nova.overview.kind_${row.kind}`) }}</span>
        <NvActionMenu :label="t('nova.overview.actions_for', { name: row.app.name })" :items="menu(row)" size="sm" />
      </li>
    </ul>
    <p v-if="total > rows.length" class="nv-lib__more">
      <RouterLink to="/library">{{ t('nova.overview.more_apps', { n: total - rows.length }) }}</RouterLink>
    </p>
    <PlayInBrowserDialog v-model:open="browserPlay.open" :app="browserPlay.app" />
  </NvCard>
</template>

<style>
@layer components {
  .nv-lib__pad {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding: var(--nv-space-4) var(--nv-space-5);
  }

  .nv-lib__list {
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-lib__row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 120px 40px;
    align-items: center;
    min-height: 56px;
    padding: 0 var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-lib__row:last-child {
    border-bottom: 0;
  }

  .nv-lib__main {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-width: 0;
    color: inherit;
    text-decoration: none;
  }

  .nv-lib__main:hover .nv-lib__name {
    text-decoration: underline;
  }

  .nv-lib__icon {
    width: 40px;
    height: 40px;
    flex-shrink: 0;
    border-radius: var(--nv-radius-md);
  }

  .nv-lib__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
    line-height: 1.3;
  }

  .nv-lib__name {
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-lib__sub {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-lib__sub--running {
    color: var(--nv-success);
  }

  .nv-lib__kind {
    color: var(--nv-text-secondary);
  }

  .nv-lib__more {
    margin: 0;
    padding: var(--nv-space-3) var(--nv-space-5);
    border-top: 1px solid var(--nv-divider);
    font-size: var(--nv-text-sm);
  }

  @media (max-width: 767px) {
    .nv-lib__row {
      grid-template-columns: minmax(0, 1fr) 40px;
      padding: 0 var(--nv-space-4);
    }

    .nv-lib__kind {
      display: none;
    }
  }
}
</style>
