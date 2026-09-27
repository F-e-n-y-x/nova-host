<script setup>
/**
 * "Metadata" tab of the app editor: which game the app is matched to (Steam or IGDB id,
 * name and confidence) and when its details were fetched, with Change match (search Steam
 * and IGDB), Re-fetch details, Reset details to auto and Reset artwork to auto.
 *
 * Props: index (app index), name (saved app name; the host matches by it), draftName (name
 *        in the form, used as the default search text).
 * Emits: match-changed (the host changed the app's match fields), artwork-applied (the host
 *        stored new artwork for the app).
 */
import { computed, onBeforeUnmount, reactive, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { RefreshCw, RotateCcw, Search } from '@lucide/vue'
import NvAlert from '../../components/NvAlert.vue'
import NvBadge from '../../components/NvBadge.vue'
import NvButton from '../../components/NvButton.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import NvTextField from '../../components/NvTextField.vue'
import { ART_KINDS, applyArtwork, pollJob, searchArtwork } from '../library/libraryApi'
import {
  getAppMetadata, hasOverride, matchCandidates, matchChange, relativeTime, searchMetadata, setAppMetadata,
} from '../library/metadataApi'

const props = defineProps({
  index: { type: Number, required: true },
  name: { type: String, default: '' },
  draftName: { type: String, default: '' },
})
const emit = defineEmits(['match-changed', 'artwork-applied'])
const { t, locale } = useI18n()

const status = shallowRef(null)
const loading = shallowRef(true)
const loadError = shallowRef(false)
/** Which action is running ('match' | 'refetch' | 'reset' | 'artwork'), or ''. */
const busy = shallowRef('')
const message = reactive({ variant: 'success', text: '' })
const search = reactive({ open: false, q: '', loading: false, error: '', searched: '', results: [], igdb: true })
let controller = new AbortController()

const match = computed(() => status.value?.match || null)
const sourceLabel = computed(() => (match.value ? t(`nova.library.meta_source_${match.value.source}`) : ''))
const confidence = computed(() => Math.round((match.value?.confidence ?? 0) * 100))
const manual = computed(() => hasOverride(status.value))
const fetchedText = computed(() => {
  const when = relativeTime(status.value?.details?.fetched_at, locale.value)
  return when ? t('nova.library.meta_fetched', { when }) : t('nova.library.meta_not_fetched')
})
const renamed = computed(() => props.draftName.trim() !== '' && props.draftName.trim() !== props.name)

async function load() {
  loading.value = true
  loadError.value = false
  try {
    status.value = await getAppMetadata(props.index)
  } catch {
    loadError.value = true
  } finally {
    loading.value = false
  }
}

watch(() => props.index, () => {
  search.open = false
  message.text = ''
  load()
}, { immediate: true })

onBeforeUnmount(() => controller.abort())

function say(variant, key, params = {}) {
  message.variant = variant
  message.text = t(key, params)
}

/**
 * Run one action with the busy state and error message handled.
 *
 * @param {string} name Action id.
 * @param {() => Promise<void>} work The action.
 */
async function run(name, work) {
  busy.value = name
  message.text = ''
  try {
    await work()
  } catch (error) {
    if (error?.name !== 'AbortError') say('danger', 'nova.library.meta_failed', { error: error?.message || '' })
  } finally {
    busy.value = ''
  }
}

/** Apply a change, wait for its re-fetch job, and show the new status. */
async function change(body, doneKey) {
  const reply = await setAppMetadata(props.index, body)
  status.value = reply
  emit('match-changed')
  if (reply.job_id) await pollJob(reply.job_id, { signal: controller.signal })
  await load()
  say('success', doneKey)
}

function openSearch() {
  search.open = true
  search.q = props.draftName || props.name
  search.results = []
  search.searched = ''
  search.error = ''
  runSearch()
}

async function runSearch() {
  const q = search.q.trim()
  if (!q) return
  search.loading = true
  search.error = ''
  try {
    const results = await searchMetadata(q)
    search.results = matchCandidates(results)
    search.igdb = results.igdb.length > 0
    search.searched = q
  } catch {
    search.error = t('nova.library.meta_search_failed')
  } finally {
    search.loading = false
  }
}

function choose(candidate) {
  run('match', async () => {
    search.open = false
    await change(matchChange(candidate), 'nova.library.meta_saved')
  })
}

function refetch() {
  run('refetch', () => change({ refetch: true }, 'nova.library.meta_refetched'))
}

function resetDetails() {
  run('reset', () => change({ reset: true, refetch: true }, 'nova.library.meta_refetched'))
}

/** Replace the app's artwork with the first automatic candidate of each kind. */
function resetArtwork() {
  run('artwork', async () => {
    const query = match.value?.source === 'steam' && match.value.id ? { appid: match.value.id } : { q: props.name }
    const { artwork } = await searchArtwork(query)
    const choices = Object.fromEntries(ART_KINDS.map((k) => [k, artwork[k][0]?.id || 'none']))
    if (ART_KINDS.every((k) => choices[k] === 'none')) {
      say('warning', 'nova.library.meta_artwork_none')
      return
    }
    await pollJob(await applyArtwork(props.index, choices), { signal: controller.signal })
    emit('artwork-applied')
    say('success', 'nova.library.meta_artwork_reset')
  })
}
</script>

<template>
  <div class="nv-meta">
    <NvAlert v-if="message.text" :variant="message.variant" live>{{ message.text }}</NvAlert>
    <p v-if="renamed" class="nv-meta__note">{{ t('nova.library.meta_unsaved') }}</p>

    <section class="nv-meta__section" aria-labelledby="nv-meta-match">
      <h3 id="nv-meta-match" class="nv-meta__heading">{{ t('nova.library.meta_match') }}</h3>
      <div v-if="loading" class="nv-meta__card"><NvSkeleton height="44px" /></div>
      <NvAlert v-else-if="loadError" variant="danger">{{ t('nova.library.meta_load_failed') }}</NvAlert>
      <div v-else class="nv-meta__card" data-test="meta-match">
        <template v-if="match">
          <div class="nv-meta__match">
            <span class="nv-meta__name">{{ match.name || name }}</span>
            <span class="nv-meta__id">{{ sourceLabel }} · {{ match.id }}</span>
          </div>
          <div class="nv-meta__badges">
            <NvBadge v-if="manual" variant="accent">{{ t('nova.library.meta_manual') }}</NvBadge>
            <NvBadge v-else :variant="confidence >= 85 ? 'success' : 'warning'">
              {{ t('nova.library.meta_confidence', { percent: confidence }) }}
            </NvBadge>
          </div>
        </template>
        <p v-else class="nv-meta__empty">{{ t('nova.library.meta_no_match') }}</p>
        <p class="nv-meta__fetched">{{ fetchedText }}</p>
      </div>
    </section>

    <div class="nv-meta__actions">
      <NvButton size="sm" :disabled="!!busy" @click="openSearch"><Search :size="16" aria-hidden="true" />{{ t('nova.library.meta_change') }}</NvButton>
      <NvButton size="sm" :loading="busy === 'refetch'" :disabled="!!busy && busy !== 'refetch'" @click="refetch">
        <RefreshCw :size="16" aria-hidden="true" />{{ t('nova.library.meta_refetch') }}
      </NvButton>
      <NvButton size="sm" variant="ghost" :loading="busy === 'reset'" :disabled="!!busy && busy !== 'reset'" @click="resetDetails">
        <RotateCcw :size="16" aria-hidden="true" />{{ t('nova.library.meta_reset_details') }}
      </NvButton>
      <NvButton size="sm" variant="ghost" :loading="busy === 'artwork'" :disabled="!!busy && busy !== 'artwork'" @click="resetArtwork">
        <RotateCcw :size="16" aria-hidden="true" />{{ t('nova.library.meta_reset_artwork') }}
      </NvButton>
    </div>

    <section v-if="search.open" class="nv-meta__section" aria-labelledby="nv-meta-search">
      <h3 id="nv-meta-search" class="nv-meta__heading">{{ t('nova.library.meta_change') }}</h3>
      <form class="nv-meta__search" @submit.prevent="runSearch">
        <div class="nv-meta__search-field">
          <NvTextField v-model="search.q" :label="t('nova.library.meta_search_label')" hide-label />
        </div>
        <NvButton type="submit" size="sm" :loading="search.loading">{{ t('nova.library.meta_search') }}</NvButton>
      </form>
      <NvAlert v-if="search.error" variant="danger" live>{{ search.error }}</NvAlert>
      <p v-else-if="search.searched && !search.loading && search.results.length === 0" class="nv-meta__empty">
        {{ t('nova.library.meta_search_empty', { q: search.searched }) }}
      </p>
      <ul v-if="search.results.length" class="nv-meta__results">
        <li v-for="candidate in search.results" :key="candidate.key" class="nv-meta__result">
          <div class="nv-meta__match">
            <span class="nv-meta__name">{{ candidate.name }}<template v-if="candidate.year"> ({{ candidate.year }})</template></span>
            <span class="nv-meta__id">
              {{ t(`nova.library.meta_source_${candidate.source}`) }} · {{ candidate.id }} ·
              {{ t('nova.library.meta_confidence', { percent: Math.round(candidate.confidence * 100) }) }}
            </span>
          </div>
          <NvButton size="sm" :disabled="!!busy" @click="choose(candidate)">{{ t('nova.library.meta_use') }}</NvButton>
        </li>
      </ul>
      <p v-if="search.searched && !search.igdb" class="nv-meta__note">{{ t('nova.library.meta_igdb_hint') }}</p>
    </section>
  </div>
</template>

<style>
@layer components {
  .nv-meta {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
  }

  .nv-meta__section {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-meta__heading {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-meta__card {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-2) var(--nv-space-3);
    padding: var(--nv-space-4);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-raised);
  }

  .nv-meta__match {
    display: flex;
    flex-direction: column;
    gap: 2px;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-meta__name {
    font-weight: 600;
    overflow-wrap: anywhere;
  }

  .nv-meta__id,
  .nv-meta__fetched,
  .nv-meta__empty,
  .nv-meta__note {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-meta__id {
    font-family: var(--nv-font-mono);
  }

  .nv-meta__fetched {
    flex-basis: 100%;
  }

  .nv-meta__actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-meta__search {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-meta__search-field {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-meta__results {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-raised);
  }

  .nv-meta__result {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3) var(--nv-space-4);
  }

  .nv-meta__result + .nv-meta__result {
    border-top: 1px solid var(--nv-border);
  }
}
</style>
