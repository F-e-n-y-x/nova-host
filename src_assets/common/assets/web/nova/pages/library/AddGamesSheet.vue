<script setup>
/**
 * "Add games" sheet (SPEC: Add games sheet, 4 steps):
 *   1. sources — scan a folder, or import from Lutris / Steam / Heroic;
 *   2. scanning — progress of the host scan job, with Stop;
 *   3. review — detected games with match confidence, per-game checkbox, Change match / Choose
 *      artwork, "Already in your library" duplicates unselected;
 *   4. artwork — pick poster / hero / logo / icon for one game, and search a better title match.
 * Nothing is added until "Add N games"; the import runs as a host job, then `imported` fires.
 *
 * v-model:open — boolean.
 * Props: settingsKeyUrl (link to the SteamGridDB key setting).
 * Emits: imported({ imported, duplicates, failed }) after a finished import.
 */
import { computed, nextTick, reactive, ref, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronLeft, Folder, Gamepad2, Gauge, Shield, SearchCheck } from '@lucide/vue'
import NvSheet from '../../components/NvSheet.vue'
import NvButton from '../../components/NvButton.vue'
import NvPathField from '../../components/NvPathField.vue'
import NvCheckbox from '../../components/NvCheckbox.vue'
import NvArt from '../../components/NvArt.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvBadge from '../../components/NvBadge.vue'
import NvTextField from '../../components/NvTextField.vue'
import NvActionMenu from '../../components/NvActionMenu.vue'
import NvEmptyState from '../../components/NvEmptyState.vue'
import ArtworkPicker from './ArtworkPicker.vue'
import {
  cancelJob, candidateUrl, importPayload, initialReview, jobFraction, matchLevel, normalizeArtwork,
  pollJob, searchArtwork, startImport, startScan,
} from './libraryApi'

const open = defineModel('open', { type: Boolean, default: false })
defineProps({
  settingsKeyUrl: { type: String, default: '/settings#steamgriddb_api_key' },
})
const emit = defineEmits(['imported'])
const { t } = useI18n()

const FOLDER_KEY = 'nova.library.scanFolder'
const SOURCE_ICONS = { folder: Folder, lutris: Gamepad2, steam: Gauge, heroic: Shield }

function storedFolder() {
  try {
    return globalThis.localStorage?.getItem(FOLDER_KEY) || ''
  } catch {
    return ''
  }
}

const step = shallowRef('sources')
const folder = ref(storedFolder())
const folderError = shallowRef('')
const unavailable = reactive({})
const job = reactive({ id: '', source: '', snapshot: null, error: '' })
const items = ref([])
const skipped = ref([])
const review = ref({})
const artworkFor = shallowRef('')
const artDraft = ref({})
const matchQuery = shallowRef('')
const matchResults = ref([])
const matchBusy = shallowRef(false)
const matchError = shallowRef('')
const importing = shallowRef(false)
const importError = shallowRef('')
let controller = null

const selectedCount = computed(() => items.value.filter((i) => review.value[i.temp_id]?.selected).length)
const selectable = computed(() => items.value.filter((i) => !i.already_in_library))
const allSelected = computed({
  get: () => selectable.value.length > 0 && selectable.value.every((i) => review.value[i.temp_id]?.selected),
  set: (on) => { for (const i of selectable.value) review.value[i.temp_id].selected = on },
})
const someSelected = computed(() => selectedCount.value > 0 && !allSelected.value)
const duplicates = computed(() => items.value.filter((i) => i.already_in_library).length)
const needsCheck = computed(() => items.value.filter((i) => !i.already_in_library && matchLevel(i) !== 'sure').length)
const fraction = computed(() => jobFraction(job.snapshot))
const artItem = computed(() => items.value.find((i) => i.temp_id === artworkFor.value) || null)

const title = computed(() => ({
  sources: t('nova.addgames.title'),
  scanning: t(`nova.addgames.scanning_title_${job.source || 'folder'}`),
  review: t('nova.addgames.review_title', items.value.length),
  artwork: t('nova.addgames.artwork_title', { title: review.value[artworkFor.value]?.title || '' }),
}[step.value]))
const subtitle = computed(() => ({
  sources: t('nova.addgames.subtitle'),
  scanning: t('nova.addgames.scanning_subtitle'),
  review: t('nova.addgames.review_subtitle'),
  artwork: t('nova.addgames.artwork_subtitle'),
}[step.value]))

watch(open, (isOpen) => {
  if (isOpen) reset()
  else stopPolling()
})

function reset() {
  stopPolling()
  step.value = 'sources'
  Object.assign(job, { id: '', source: '', snapshot: null, error: '' })
  items.value = []
  skipped.value = []
  review.value = {}
  folderError.value = ''
  importError.value = ''
  importing.value = false
}

function focusInput(id) {
  const el = document.getElementById(id)
  ;(el?.matches('input') ? el : el?.querySelector('input'))?.focus()
}

function stopPolling() {
  controller?.abort()
  controller = null
}

/**
 * Start a scan and move to the scanning step; failures go back to the sources list, and a
 * library that isn't installed marks that source unavailable.
 *
 * @param {'folder'|'lutris'|'steam'|'heroic'} source Source to scan.
 */
async function scan(source) {
  job.error = ''
  if (source === 'folder') {
    const path = folder.value.trim()
    if (!path.startsWith('/') && !/^[a-z]:[\\/]/i.test(path)) {
      folderError.value = t('nova.addgames.folder_required')
      await nextTick()
      focusInput('nv-addgames-folder')
      return
    }
    folderError.value = ''
    try {
      globalThis.localStorage?.setItem(FOLDER_KEY, path)
    } catch {
      // Storage blocked: the folder just isn't remembered.
    }
  }
  delete unavailable[source]
  Object.assign(job, { id: '', source, snapshot: null })
  step.value = 'scanning'
  controller = new AbortController()
  try {
    job.id = await startScan(source, folder.value.trim())
    const done = await pollJob(job.id, { signal: controller.signal, onUpdate: (s) => { job.snapshot = s } })
    items.value = done.result?.items || []
    skipped.value = done.result?.skipped || []
    review.value = initialReview(items.value)
    step.value = 'review'
  } catch (error) {
    if (error.name === 'AbortError') return
    const message = error.job?.error || ''
    if (source !== 'folder' && /not (installed|found)|no .*(install|library|database)/i.test(message)) {
      unavailable[source] = message
    } else {
      job.error = message || t('nova.addgames.scan_failed', { source: t(`nova.addgames.source_${source}`) })
    }
    step.value = 'sources'
  } finally {
    controller = null
  }
}

function stopScan() {
  stopPolling()
  if (job.id) cancelJob(job.id)
  step.value = 'sources'
}

function openArtwork(item, search = false) {
  artworkFor.value = item.temp_id
  artDraft.value = { ...review.value[item.temp_id].art }
  matchQuery.value = review.value[item.temp_id].title
  matchResults.value = []
  matchError.value = ''
  step.value = 'artwork'
  if (search) nextTick(() => document.getElementById('nv-addgames-match')?.querySelector('input')?.focus())
}

/** Search for a better title match; the results offer their Steam app id. */
async function findMatches() {
  matchBusy.value = true
  matchError.value = ''
  try {
    matchResults.value = (await searchArtwork({ q: matchQuery.value })).matches
    if (!matchResults.value.length) matchError.value = t('nova.addgames.no_matches', { q: matchQuery.value })
  } catch {
    matchError.value = t('nova.addgames.search_failed')
  } finally {
    matchBusy.value = false
  }
}

/**
 * Use a search result as this game's match: take its name and artwork.
 *
 * @param {{appid: number, name: string, confidence: number}} match Chosen match.
 */
async function useMatch(match) {
  matchBusy.value = true
  try {
    const { artwork } = await searchArtwork({ appid: match.appid })
    items.value = items.value.map((i) => (i.temp_id === artworkFor.value
      ? { ...i, matched: { ...match, confidence: 1 }, artwork: normalizeArtwork(artwork) }
      : i))
    review.value[artworkFor.value].title = match.name
    artDraft.value = Object.fromEntries(Object.entries(normalizeArtwork(artwork)).map(([k, list]) => [k, list[0]?.id || 'none']))
    matchResults.value = []
  } catch {
    matchError.value = t('nova.addgames.search_failed')
  } finally {
    matchBusy.value = false
  }
}

function applyArtwork() {
  review.value[artworkFor.value].art = { ...artDraft.value }
  backToReview()
}

async function backToReview() {
  const id = artworkFor.value
  step.value = 'review'
  await nextTick()
  document.getElementById(`nv-addgames-row-${id}`)?.querySelector('button')?.focus()
}

async function addSelected() {
  importing.value = true
  importError.value = ''
  try {
    const id = await startImport(importPayload(items.value, review.value))
    controller = new AbortController()
    const done = await pollJob(id, { signal: controller.signal })
    const result = {
      imported: done.result?.imported?.length ?? 0,
      duplicates: done.result?.duplicates?.length ?? 0,
      failed: done.result?.failed?.length ?? 0,
    }
    emit('imported', result)
    open.value = false
  } catch (error) {
    if (error.name !== 'AbortError') importError.value = error.job?.error || t('nova.addgames.import_failed')
  } finally {
    importing.value = false
    controller = null
  }
}

function posterFor(item) {
  const id = review.value[item.temp_id]?.art.poster
  const candidate = normalizeArtwork(item.artwork).poster.find((c) => c.id === id)
  return candidate ? candidateUrl(candidate) : ''
}

function rowMenu(item) {
  return [
    { id: 'match', label: t('nova.addgames.change_match'), onSelect: () => openArtwork(item, true) },
    { id: 'art', label: t('nova.addgames.choose_artwork'), onSelect: () => openArtwork(item) },
  ]
}

function where(item) {
  return item.source === 'folder' ? (item.working_dir || item.launch_cmd) : (item.launch_cmd || item.source_id)
}

function beforeClose() {
  if (importing.value) return false
  return true
}
</script>

<template>
  <NvSheet v-model:open="open" size="lg" :title="title" :subtitle="subtitle" :before-close="beforeClose"
           body-class="nv-addgames__body">
    <!-- 1 · sources -->
    <template v-if="step === 'sources'">
      <NvAlert v-if="job.error" variant="danger" live :title="t('nova.addgames.scan_failed_title')">{{ job.error }}</NvAlert>
      <ul class="nv-addgames__sources" :aria-label="t('nova.addgames.sources')">
        <li class="nv-addgames__source nv-addgames__source--primary">
          <span class="nv-addgames__icon" aria-hidden="true"><Folder :size="18" /></span>
          <div class="nv-addgames__source-main">
            <h3 class="nv-addgames__source-title">{{ t('nova.addgames.source_folder_title') }}</h3>
            <p class="nv-addgames__source-desc">{{ t('nova.addgames.source_folder_desc') }}</p>
            <form class="nv-addgames__folder" novalidate @submit.prevent="scan('folder')">
              <NvPathField id="nv-addgames-folder" v-model="folder" type="directory" :label="t('nova.addgames.folder_label')"
                           :error="folderError" :browse-title="t('nova.addgames.folder_browse')" />
              <NvButton type="submit" variant="primary" class="nv-addgames__scan">{{ t('nova.addgames.scan') }}</NvButton>
            </form>
          </div>
        </li>
        <li v-for="s in ['lutris', 'steam', 'heroic']" :key="s"
            :class="['nv-addgames__source', { 'nv-addgames__source--off': unavailable[s] }]">
          <span class="nv-addgames__icon" aria-hidden="true"><component :is="SOURCE_ICONS[s]" :size="18" /></span>
          <div class="nv-addgames__source-main">
            <h3 class="nv-addgames__source-title">{{ t(`nova.addgames.source_${s}_title`) }}</h3>
            <p class="nv-addgames__source-desc">
              {{ unavailable[s] ? t('nova.addgames.not_installed', { source: t(`nova.addgames.source_${s}`) }) : t(`nova.addgames.source_${s}_desc`) }}
            </p>
          </div>
          <NvButton v-if="!unavailable[s]" size="sm" @click="scan(s)">{{ t('nova.addgames.import') }}</NvButton>
        </li>
      </ul>
      <p class="nv-addgames__note">{{ t('nova.addgames.sources_note') }}</p>
      <p class="nv-addgames__note">
        {{ t('nova.addgames.sgdb_hint') }}
        <RouterLink :to="settingsKeyUrl" class="nv-link">{{ t('nova.addgames.sgdb_link') }}</RouterLink>
      </p>
    </template>

    <!-- 2 · scanning -->
    <template v-else-if="step === 'scanning'">
      <section class="nv-addgames__progress" aria-live="polite">
        <div class="nv-addgames__progress-head">
          <h3 class="nv-addgames__source-title">
            {{ job.source === 'folder' ? t('nova.addgames.scanning_path', { path: folder }) : t(`nova.addgames.scanning_title_${job.source}`) }}
          </h3>
          <span v-if="job.snapshot?.progress?.total" class="nv-mono nv-addgames__count">
            {{ t('nova.addgames.progress_count', { done: job.snapshot.progress.done, total: job.snapshot.progress.total }) }}
          </span>
        </div>
        <div class="nv-addgames__bar" role="progressbar" :aria-label="t('nova.addgames.progress_label')"
             aria-valuemin="0" aria-valuemax="100" :aria-valuenow="fraction === null ? undefined : Math.round(fraction * 100)">
          <span :class="['nv-addgames__bar-fill', { 'nv-addgames__bar-fill--busy': fraction === null }]"
                :style="fraction === null ? null : { width: `${fraction * 100}%` }"></span>
        </div>
        <p class="nv-addgames__source-desc">{{ job.snapshot?.stage || t('nova.addgames.starting') }}</p>
      </section>
    </template>

    <!-- 3 · review -->
    <template v-else-if="step === 'review'">
      <NvAlert v-if="importError" variant="danger" live :title="t('nova.addgames.import_failed_title')">{{ importError }}</NvAlert>
      <NvEmptyState v-if="!items.length" compact :title="t('nova.addgames.none_found')"
                    :description="t('nova.addgames.none_found_desc')">
        <template #icon><SearchCheck :size="24" aria-hidden="true" /></template>
        <template #actions><NvButton @click="step = 'sources'">{{ t('nova.addgames.try_other') }}</NvButton></template>
      </NvEmptyState>
      <template v-else>
        <div class="nv-addgames__review-head">
          <NvCheckbox v-model="allSelected" :indeterminate="someSelected" :label="t('nova.addgames.select_all')" />
          <span class="nv-addgames__summary">
            {{ t('nova.addgames.found', items.length) }}<template v-if="needsCheck"> · {{ t('nova.addgames.needs_check', needsCheck) }}</template><template v-if="duplicates"> · {{ t('nova.addgames.already_count', duplicates) }}</template>
          </span>
        </div>
        <ul class="nv-addgames__list" :aria-label="t('nova.addgames.detected')">
          <li v-for="item in items" :id="`nv-addgames-row-${item.temp_id}`" :key="item.temp_id"
              :class="['nv-addgames__row', {
                'nv-addgames__row--check': !item.already_in_library && matchLevel(item) !== 'sure',
                'nv-addgames__row--dup': item.already_in_library }]">
            <NvCheckbox v-model="review[item.temp_id].selected" :disabled="item.already_in_library" hide-label
                        :label="t('nova.addgames.include', { title: review[item.temp_id].title })" />
            <NvArt class="nv-addgames__thumb" :title="review[item.temp_id].title" :src="posterFor(item)" kind="poster"
                   :show-title="false" decorative />
            <div class="nv-addgames__row-main">
              <span class="nv-addgames__row-title">{{ review[item.temp_id].title }}</span>
              <span class="nv-mono nv-addgames__row-path" :title="where(item)">{{ where(item) }}</span>
              <span class="nv-addgames__row-meta">
                <NvBadge>{{ t(`nova.addgames.source_${item.source}`) }}</NvBadge>
                <span v-if="item.already_in_library" class="nv-addgames__level">{{ t('nova.addgames.already') }}</span>
                <span v-else :class="['nv-addgames__level', `nv-addgames__level--${matchLevel(item)}`]">
                  {{ matchLevel(item) === 'check'
                    ? t('nova.addgames.level_check', { name: item.matched.name })
                    : t(`nova.addgames.level_${matchLevel(item)}`) }}
                </span>
              </span>
            </div>
            <div class="nv-addgames__row-actions">
              <NvButton size="sm" @click="openArtwork(item, true)">{{ t('nova.addgames.change_match') }}</NvButton>
              <NvButton size="sm" variant="ghost" @click="openArtwork(item)">{{ t('nova.addgames.choose_artwork') }}</NvButton>
            </div>
            <NvActionMenu class="nv-addgames__row-menu" :label="t('nova.addgames.row_menu', { title: review[item.temp_id].title })"
                          :items="rowMenu(item)" />
          </li>
        </ul>
        <details v-if="skipped.length" class="nv-addgames__skipped">
          <summary>{{ t('nova.addgames.skipped', skipped.length) }}</summary>
          <ul>
            <li v-for="s in skipped" :key="s.path"><span class="nv-mono">{{ s.path }}</span> — {{ s.reason }}</li>
          </ul>
        </details>
      </template>
    </template>

    <!-- 4 · artwork -->
    <template v-else-if="step === 'artwork' && artItem">
      <form id="nv-addgames-match" class="nv-addgames__match" role="search" @submit.prevent="findMatches">
        <NvTextField v-model="matchQuery" :label="t('nova.addgames.match_label')" :hint="artItem.matched
          ? t('nova.addgames.match_current', { name: artItem.matched.name }) : t('nova.addgames.match_none')" />
        <NvButton type="submit" :loading="matchBusy">{{ t('nova.addgames.search') }}</NvButton>
      </form>
      <p v-if="matchError" class="nv-addgames__match-error" role="alert">{{ matchError }}</p>
      <ul v-if="matchResults.length" class="nv-addgames__matches" :aria-label="t('nova.addgames.match_results')">
        <li v-for="m in matchResults" :key="m.appid">
          <button type="button" class="nv-addgames__match-item" @click="useMatch(m)">
            <span>{{ m.name }}</span>
            <span class="nv-mono nv-addgames__count">{{ Math.round((m.confidence ?? 0) * 100) }}%</span>
          </button>
        </li>
      </ul>
      <ArtworkPicker v-model="artDraft" :artwork="artItem.artwork" :title="review[artItem.temp_id].title" :loading="matchBusy">
        <template #footnote>
          <p class="nv-addgames__note">{{ t('nova.addgames.art_note') }}</p>
        </template>
      </ArtworkPicker>
    </template>

    <template #footer-start>
      <NvButton v-if="step === 'artwork'" variant="ghost" @click="backToReview">
        <ChevronLeft :size="16" aria-hidden="true" />{{ t('nova.addgames.back') }}
      </NvButton>
      <NvButton v-else-if="step === 'review'" variant="ghost" @click="step = 'sources'">
        <ChevronLeft :size="16" aria-hidden="true" />{{ t('nova.addgames.back') }}
      </NvButton>
      <span v-else-if="step === 'sources'" class="nv-addgames__footnote">{{ t('nova.addgames.nothing_yet') }}</span>
    </template>
    <template #footer>
      <template v-if="step === 'scanning'">
        <NvButton @click="stopScan">{{ t('nova.addgames.stop') }}</NvButton>
      </template>
      <template v-else-if="step === 'review' && items.length">
        <span class="nv-addgames__footnote">{{ t('nova.addgames.selected', { n: selectedCount }) }}</span>
        <NvButton variant="primary" :disabled="!selectedCount" :loading="importing" class="nv-addgames__add" @click="addSelected">
          {{ t('nova.addgames.add_n', selectedCount) }}
        </NvButton>
      </template>
      <template v-else-if="step === 'artwork'">
        <NvButton @click="backToReview">{{ t('nova.common.cancel') }}</NvButton>
        <NvButton variant="primary" @click="applyArtwork">{{ t('nova.addgames.use_artwork') }}</NvButton>
      </template>
      <NvButton v-else @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
    </template>
  </NvSheet>
</template>

<style>
@layer components {
  .nv-addgames__body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
  }

  .nv-addgames__sources,
  .nv-addgames__list,
  .nv-addgames__matches {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-addgames__source {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-4);
    padding: var(--nv-space-4);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
  }

  .nv-addgames__source--primary {
    border-color: var(--nv-accent-text);
    background: var(--nv-accent-tint);
  }

  .nv-addgames__icon {
    flex-shrink: 0;
    width: 36px;
    height: 36px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    color: var(--nv-accent-text);
  }

  .nv-addgames__source--off .nv-addgames__icon {
    color: var(--nv-text-muted);
  }

  .nv-addgames__source-main {
    flex-grow: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
  }

  .nv-addgames__source-title {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-addgames__source-desc,
  .nv-addgames__note,
  .nv-addgames__summary,
  .nv-addgames__footnote {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-addgames__folder {
    display: flex;
    align-items: flex-end;
    gap: var(--nv-space-3);
    margin-top: var(--nv-space-3);
  }

  .nv-addgames__folder > :first-child {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-addgames__progress {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding: var(--nv-space-5);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
  }

  .nv-addgames__progress-head,
  .nv-addgames__review-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-3);
    flex-wrap: wrap;
  }

  .nv-addgames__count {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-addgames__bar {
    height: 6px;
    border-radius: var(--nv-radius-pill);
    background: var(--nv-raised);
    overflow: hidden;
  }

  .nv-addgames__bar-fill {
    display: block;
    height: 100%;
    background: var(--nv-accent);
    transition: width 200ms var(--nv-ease);
  }

  .nv-addgames__bar-fill--busy {
    width: 30%;
    animation: nv-addgames-busy 1.2s var(--nv-ease) infinite alternate;
  }

  @keyframes nv-addgames-busy {
    from { transform: translateX(-20%); }
    to { transform: translateX(260%); }
  }

  @media (prefers-reduced-motion: reduce) {
    .nv-addgames__bar-fill--busy { animation: none; width: 100%; opacity: 0.5; }
  }

  .nv-addgames__row {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    padding: var(--nv-space-3) var(--nv-space-4);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
  }

  .nv-addgames__row--check {
    border-color: var(--nv-warning-border);
    background: var(--nv-warning-tint);
  }

  .nv-addgames__row--dup .nv-addgames__row-title {
    color: var(--nv-text-secondary);
  }

  .nv-addgames__thumb {
    flex-shrink: 0;
    width: 48px;
  }

  .nv-addgames__row-main {
    flex-grow: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nv-addgames__row-title {
    font-weight: 600;
    overflow-wrap: anywhere;
  }

  .nv-addgames__row-path {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-xs);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .nv-addgames__row-meta {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-2);
    margin-top: 2px;
    font-size: var(--nv-text-sm);
  }

  .nv-addgames__level { color: var(--nv-text-secondary); }
  .nv-addgames__level--sure { color: var(--nv-success); }
  .nv-addgames__level--check { color: var(--nv-warning); }

  .nv-addgames__row-actions {
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: var(--nv-space-1);
  }

  .nv-addgames__row-menu { display: none; }

  .nv-addgames__skipped {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-addgames__skipped summary { cursor: pointer; min-height: 32px; display: flex; align-items: center; }
  .nv-addgames__skipped ul { margin: var(--nv-space-2) 0 0; padding-left: var(--nv-space-5); }

  .nv-addgames__match {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-addgames__match > :first-child { flex-grow: 1; min-width: 0; }
  .nv-addgames__match > .nv-btn { margin-top: 26px; }

  .nv-addgames__match-error {
    margin: 0;
    color: var(--nv-danger);
    font-size: var(--nv-text-sm);
  }

  .nv-addgames__match-item {
    width: 100%;
    min-height: 44px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-3);
    padding: 0 var(--nv-space-4);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-surface);
    color: var(--nv-text);
    font: inherit;
    text-align: start;
    cursor: pointer;
  }

  .nv-addgames__match-item:hover { background: var(--nv-raised); }

  @media (max-width: 767px) {
    .nv-addgames__folder { flex-direction: column; align-items: stretch; }
    .nv-addgames__row-actions { display: none; }
    .nv-addgames__row-menu { display: inline-flex; }
    .nv-addgames__footnote { display: none; }
    .nv-addgames__add { flex-grow: 1; min-height: 44px; }
  }
}
</style>
