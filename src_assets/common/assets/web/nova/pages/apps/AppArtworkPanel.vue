<script setup>
/**
 * "Artwork" tab of the app editor: for each kind (poster, banner, logo, icon, background) the
 * current image, upload a file, paste an image URL (the host downloads it with SSRF protection),
 * pick from artwork found on Steam / SteamGridDB / IGDB, open artwork sites with the title
 * searched, or reset the kind to the automatic choice.
 *
 * Props: index (app index), app (saved app, for current images), name (saved name), coverVersion.
 * Emits: artwork-applied (the host stored new artwork for the app).
 */
import { computed, onBeforeUnmount, reactive, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ExternalLink, ImagePlus, Link, RotateCcw } from '@lucide/vue'
import NvAlert from '../../components/NvAlert.vue'
import NvArt from '../../components/NvArt.vue'
import NvButton from '../../components/NvButton.vue'
import NvSegmentedControl from '../../components/NvSegmentedControl.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import NvTextField from '../../components/NvTextField.vue'
import {
  ALL_ART_KINDS, applyArtwork, artUrl, artworkSites, candidateUrl, fileToBase64, pollJob, searchArtwork, setCustomArtwork,
} from '../library/libraryApi'

const props = defineProps({
  index: { type: Number, required: true },
  app: { type: Object, default: null },
  name: { type: String, default: '' },
  coverVersion: { type: Number, default: 0 },
})
const emit = defineEmits(['artwork-applied'])
const { t } = useI18n()

/** Shape of each kind's preview. */
const SHAPES = { poster: 'poster', hero: 'hero', logo: 'thumb', icon: 'icon', background: 'thumb' }

const kind = shallowRef('poster')
const url = shallowRef('')
const busy = shallowRef('')
const version = shallowRef(0)
const message = reactive({ variant: 'success', text: '' })
const found = reactive({ loading: false, loaded: false, artwork: {} })
let controller = new AbortController()

const kinds = computed(() => ALL_ART_KINDS.map((k) => ({ value: k, label: t(`nova.library.art_kind_${k}`) })))
const kindLabel = computed(() => t(`nova.library.art_kind_${kind.value}`))
const current = computed(() => {
  const src = artUrl(props.app, props.index, kind.value, props.coverVersion + version.value)
  // No background of its own: clients show the banner, so preview that.
  return src || (kind.value === 'background' ? artUrl(props.app, props.index, 'hero', props.coverVersion + version.value) : '')
})
const options = computed(() => found.artwork[kind.value] || [])
const sites = computed(() => artworkSites(props.name))

watch(() => props.index, () => {
  found.loaded = false
  found.artwork = {}
  message.text = ''
})
watch(kind, () => {
  message.text = ''
  if (!found.loaded && !found.loading) loadFound()
}, { immediate: true })
onBeforeUnmount(() => controller.abort())

async function loadFound() {
  found.loading = true
  try {
    const appid = props.app?.['nova-steam-appid']
    found.artwork = (await searchArtwork(appid ? { appid } : { q: props.name })).artwork
    found.loaded = true
  } catch {
    found.artwork = {}
  } finally {
    found.loading = false
  }
}

function say(variant, key, params = {}) {
  message.variant = variant
  message.text = t(key, { kind: kindLabel.value, ...params })
}

/**
 * Run one change and wait for its job.
 *
 * @param {string} name Busy id.
 * @param {() => Promise<string>} start Starts the job, returns its id.
 * @param {string} doneKey Message on success.
 */
async function run(name, start, doneKey) {
  busy.value = name
  message.text = ''
  try {
    const job = await pollJob(await start(), { signal: controller.signal })
    version.value += 1
    emit('artwork-applied')
    say(job.result?.cleared ? 'warning' : 'success', job.result?.cleared ? 'nova.library.art_cleared' : doneKey)
  } catch (error) {
    if (error?.name !== 'AbortError') say('danger', 'nova.library.art_failed', { error: error?.job?.error || error?.message || '' })
  } finally {
    busy.value = ''
  }
}

async function onFile(event) {
  const file = event.target.files?.[0]
  event.target.value = ''
  if (!file) return
  const data = await fileToBase64(file)
  run('upload', () => setCustomArtwork(props.index, kind.value, { data }), 'nova.library.art_saved')
}

function useUrl() {
  const link = url.value.trim()
  if (!link) return
  run('url', () => setCustomArtwork(props.index, kind.value, { url: link }), 'nova.library.art_saved').then(() => {
    if (message.variant === 'success') url.value = ''
  })
}

function useFound(option) {
  run(`found-${option.id}`, () => applyArtwork(props.index, { [kind.value]: option.id }), 'nova.library.art_saved')
}

function reset() {
  run('reset', () => setCustomArtwork(props.index, kind.value, { reset: true }), 'nova.library.art_reset_done')
}
</script>

<template>
  <div class="nv-artpanel">
    <NvSegmentedControl v-model="kind" :label="t('nova.library.art_tab')" :options="kinds" size="sm" />
    <NvAlert v-if="message.text" :variant="message.variant" live>{{ message.text }}</NvAlert>

    <div class="nv-artpanel__current">
      <NvArt :class="['nv-artpanel__preview', `nv-artpanel__preview--${kind}`]" :title="name" :src="current"
             :kind="SHAPES[kind]" decorative />
      <div class="nv-artpanel__actions">
        <label class="nv-artpanel__upload">
          <ImagePlus :size="16" aria-hidden="true" />
          <span>{{ t('nova.library.art_upload_file') }}</span>
          <input type="file" accept="image/png,image/jpeg" class="nv-visually-hidden" :disabled="!!busy" @change="onFile" />
        </label>
        <NvButton size="sm" variant="ghost" :loading="busy === 'reset'" :disabled="!!busy && busy !== 'reset'" @click="reset">
          <RotateCcw :size="16" aria-hidden="true" />{{ t('nova.library.art_reset_kind') }}
        </NvButton>
      </div>
    </div>

    <form class="nv-artpanel__url" @submit.prevent="useUrl">
      <div class="nv-artpanel__url-field">
        <NvTextField v-model="url" type="url" :label="t('nova.library.art_paste_url')" :hint="t('nova.library.art_url_hint')"
                     placeholder="https://" mono />
      </div>
      <NvButton type="submit" size="sm" :loading="busy === 'url'" :disabled="!url.trim() || (!!busy && busy !== 'url')">
        <Link :size="16" aria-hidden="true" />{{ t('nova.library.art_use_url') }}
      </NvButton>
    </form>

    <section class="nv-artpanel__section" aria-labelledby="nv-artpanel-found">
      <h3 id="nv-artpanel-found" class="nv-artpanel__heading">{{ t('nova.library.art_search_results') }}</h3>
      <div v-if="found.loading" class="nv-artpanel__grid" aria-busy="true">
        <NvSkeleton v-for="n in 4" :key="n" height="120px" />
      </div>
      <p v-else-if="options.length === 0" class="nv-artpanel__empty">{{ t('nova.library.art_search_empty', { kind: kindLabel }) }}</p>
      <ul v-else :class="['nv-artpanel__grid', `nv-artpanel__grid--${SHAPES[kind]}`]">
        <li v-for="option in options" :key="option.id">
          <button type="button" class="nv-artpanel__option" :disabled="!!busy"
                  :aria-label="`${t('nova.library.art_use')}: ${option.label || ''}`" @click="useFound(option)">
            <NvArt :title="name" :src="candidateUrl(option)" :kind="SHAPES[kind]" decorative />
            <span class="nv-artpanel__label">{{ option.label }}</span>
          </button>
        </li>
      </ul>
    </section>

    <section class="nv-artpanel__section" aria-labelledby="nv-artpanel-sites">
      <h3 id="nv-artpanel-sites" class="nv-artpanel__heading">{{ t('nova.library.art_open_in') }}</h3>
      <div class="nv-artpanel__sites">
        <a v-for="site in sites" :key="site.id" class="nv-artpanel__site" :href="site.url" target="_blank" rel="noopener noreferrer">
          {{ site.label }}<ExternalLink :size="14" aria-hidden="true" />
        </a>
      </div>
    </section>
  </div>
</template>

<style>
@layer components {
  .nv-artpanel {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
  }

  .nv-artpanel > .nv-seg {
    align-self: flex-start;
    max-width: 100%;
    overflow-x: auto;
  }

  .nv-artpanel__current {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-end;
    gap: var(--nv-space-4);
  }

  .nv-artpanel__preview {
    width: 120px;
    border: 1px solid var(--nv-border);
  }

  .nv-artpanel__preview--hero,
  .nv-artpanel__preview--background,
  .nv-artpanel__preview--logo {
    width: min(100%, 320px);
  }

  .nv-artpanel__preview--icon {
    width: 72px;
  }

  .nv-artpanel__actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-artpanel__upload {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: 32px;
    padding: 0 var(--nv-space-3);
    border: 1px solid var(--nv-border-strong);
    border-radius: var(--nv-radius-md);
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-artpanel__upload:focus-within {
    outline: var(--nv-focus-width) solid var(--nv-focus);
    outline-offset: 2px;
  }

  .nv-artpanel__url {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-2);
  }

  .nv-artpanel__url > button {
    margin-top: 26px;
  }

  .nv-artpanel__url-field {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-artpanel__section {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-artpanel__heading {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-artpanel__empty {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-artpanel__grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(96px, 1fr));
    gap: var(--nv-space-3);
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-artpanel__grid--hero,
  .nv-artpanel__grid--thumb {
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
  }

  .nv-artpanel__option {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    width: 100%;
    padding: 0;
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-xs);
    text-align: left;
    cursor: pointer;
  }

  .nv-artpanel__option:focus-visible {
    outline: var(--nv-focus-width) solid var(--nv-focus);
    outline-offset: 3px;
  }

  .nv-artpanel__option:hover .nv-art {
    box-shadow: 0 0 0 2px var(--nv-focus);
  }

  .nv-artpanel__sites {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-artpanel__site {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    min-height: 32px;
    padding: 0 var(--nv-space-3);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-pill);
    color: var(--nv-text);
    font-size: var(--nv-text-sm);
    text-decoration: none;
  }

  .nv-artpanel__site:hover {
    background: var(--nv-raised);
  }

  @media (max-width: 600px) {
    .nv-artpanel__url {
      flex-direction: column;
      align-items: stretch;
    }

    .nv-artpanel__url > button {
      margin-top: 0;
    }
  }
}
</style>
