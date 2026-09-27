<script setup>
/**
 * Add or edit an application in a side panel (full-screen sheet on small screens).
 * Fields validate when they lose focus; Save focuses the first invalid field. Closing
 * with unsaved edits asks first; the page's route guard uses askDiscard() the same way,
 * so Back never drops edits silently.
 *
 * Layout (Library design): hero banner with the poster overlapping it, name + source line and
 * "Change artwork", then Name / Command / Working folder, before-and-after commands, and an
 * "Advanced" disclosure (detached commands, behaviour, exit timeout, output log, variables).
 * Saved games in a host with the library API also get "Artwork" (every kind: upload, URL, search, reset)
 * and "Metadata" tabs (see AppArtworkPanel, AppMetadataPanel).
 *
 * Props: open, app (the app to edit, or null to add one), index (-1 to add),
 *        platform (host platform from /api/config), libraryApi (host has library artwork search),
 *        coverVersion (cache-busting counter for stored artwork).
 * Emits: saved(name) after the host accepts the change, close when it should close.
 * Exposes: isDirty (boolean), askDiscard(onDiscard), openArtwork().
 */
import { computed, nextTick, reactive, ref, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ImagePlus, Search, Trash2 } from '@lucide/vue'
import NvSheet from '../../components/NvSheet.vue'
import NvConfirmDialog from '../../components/NvConfirmDialog.vue'
import NvArt from '../../components/NvArt.vue'
import NvActionMenu from '../../components/NvActionMenu.vue'
import NvButton from '../../components/NvButton.vue'
import NvTextField from '../../components/NvTextField.vue'
import NvNumberField from '../../components/NvNumberField.vue'
import NvSwitch from '../../components/NvSwitch.vue'
import NvSettingRow from '../../components/NvSettingRow.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvSelect from '../../components/NvSelect.vue'
import NvSegmentedControl from '../../components/NvSegmentedControl.vue'
import ArtworkPicker from '../library/ArtworkPicker.vue'
import AppMetadataPanel from './AppMetadataPanel.vue'
import AppArtworkPanel from './AppArtworkPanel.vue'
import PathField from './PathField.vue'
import PrepCommandList from './PrepCommandList.vue'
import DetachedCommandList from './DetachedCommandList.vue'
import EnvVarsReference from './EnvVarsReference.vue'
import FileBrowserDialog from './FileBrowserDialog.vue'
import CoverFinderDialog from './CoverFinderDialog.vue'
import { fetchJson, postJson } from '../../api'
import { buildPayload, formFromApp, formsDiffer, newAppForm, validateForm } from './appForm'
import { applyArtwork, appRunner, appSource, artUrl, candidateUrl, pollJob, searchArtwork } from '../library/libraryApi'
import { imageToPngBase64, uploadCoverData } from '../library/artworkUpload'

const props = defineProps({
  open: { type: Boolean, default: false },
  app: { type: Object, default: null },
  index: { type: Number, default: -1 },
  platform: { type: String, default: '' },
  libraryApi: { type: Boolean, default: false },
  coverVersion: { type: Number, default: 0 },
})
const emit = defineEmits(['saved', 'close', 'delete', 'artwork-applied'])
const { t } = useI18n()

/** Field ids, in form order, for focusing the first error. */
const FIELD_IDS = { name: 'nv-app-name', exitTimeout: 'nv-app-exit-timeout' }

const form = ref(newAppForm())
const initial = shallowRef('')
const touched = reactive(new Set())
const submitted = shallowRef(false)
const saveError = shallowRef('')
const saving = shallowRef(false)
const discard = reactive({ open: false, then: null })
const coversOpen = shallowRef(false)
const artwork = reactive({ open: false, loading: false, error: '', busy: false, candidates: {}, choice: { poster: 'none' } })
const browser = ref({ open: false, type: 'any', title: '', start: '', apply: null })
/** Editor tab: 'details' (the form), 'artwork' or 'metadata'. */
const tab = shallowRef('details')

const isWindows = computed(() => props.platform === 'windows')
/** Proton FSR choices: off, then 1 (sharpest) to 5. */
const fsrOptions = computed(() => [
  { value: '0', label: t('nova.apps.compat_fsr_off') },
  ...[1, 2, 3, 4, 5].map((n) => ({ value: String(n), label: t('nova.apps.compat_fsr_level', { n }) })),
])
const isNew = computed(() => props.index === -1)
const showTabs = computed(() => !isNew.value && props.libraryApi)
const tabs = computed(() => [
  { value: 'details', label: t('nova.library.tab_details') },
  { value: 'artwork', label: t('nova.library.art_tab') },
  { value: 'metadata', label: t('nova.library.tab_metadata') },
])
const title = computed(() => (isNew.value ? t('nova.apps.add_title') : t('nova.apps.edit_title', { name: props.app?.name || t('nova.apps.unnamed') })))
const isDirty = computed(() => formsDiffer(form.value, JSON.parse(initial.value || '{}')))
const allErrors = computed(() => validateForm(form.value))
const errors = computed(() => Object.fromEntries(Object.entries(allErrors.value)
  .filter(([key]) => submitted.value || touched.has(key))))
const coverPreview = computed(() => {
  if (isNew.value || !form.value['image-path']) return ''
  return form.value['image-path'] === props.app?.['image-path'] ? artUrl(props.app, props.index, 'poster', props.coverVersion) : ''
})
const heroSrc = computed(() => (isNew.value ? '' : artUrl(props.app, props.index, 'hero', props.coverVersion)))
const artTitle = computed(() => form.value.name || t('nova.apps.unnamed'))
const metaLine = computed(() => {
  const parts = []
  const source = appSource(props.app || {})
  parts.push(t(`nova.library.source_${source}`))
  const runner = appRunner(form.value)
  const runnerLabel = runner ? t(`nova.library.${runner}`) : ''
  if (runnerLabel && !parts.includes(runnerLabel)) parts.push(runnerLabel)
  return parts.join(' · ')
})
const footerMenu = computed(() => [
  { id: 'delete', label: t('nova.library.menu_delete'), icon: Trash2, danger: true, onSelect: () => emit('delete') },
])
const runGlobalPrep = computed({
  get: () => !form.value['exclude-global-prep-cmd'],
  set: (v) => { form.value['exclude-global-prep-cmd'] = !v },
})

watch(() => [props.open, props.app, props.index], ([isOpen]) => {
  if (!isOpen) return
  form.value = props.app ? formFromApp(props.app, props.index, props.platform) : newAppForm()
  initial.value = JSON.stringify(form.value)
  touched.clear()
  submitted.value = false
  saveError.value = ''
  discard.open = false
  tab.value = 'details'
}, { immediate: true })

function onFocusOut(event) {
  const key = Object.keys(FIELD_IDS).find((k) => FIELD_IDS[k] === event.target?.id)
  if (key) touched.add(key)
}

/**
 * Ask before throwing away edits; runs `onDiscard` right away when there are none.
 *
 * @param {() => void} onDiscard What to do once the user agrees.
 */
function askDiscard(onDiscard) {
  if (!isDirty.value || saving.value) {
    onDiscard()
    return
  }
  discard.then = onDiscard
  discard.open = true
}

function requestClose() {
  askDiscard(() => emit('close'))
}

/** NvSheet close guard: ask first when there are unsaved edits. */
function beforeClose() {
  requestClose()
  return false
}

/** Open the artwork sheet (poster candidates from the host's library search, or upload). */
async function openArtwork() {
  Object.assign(artwork, { open: true, error: '', candidates: {}, choice: { poster: 'none' }, loading: props.libraryApi })
  if (!props.libraryApi) return
  try {
    artwork.candidates = (await searchArtwork({ q: form.value.name })).artwork
  } catch {
    artwork.error = t('nova.addgames.search_failed')
  } finally {
    artwork.loading = false
  }
}

const ART_FIELDS = ['image-path', 'nova-hero', 'nova-logo', 'nova-icon', 'nova-background']
/** Match fields the Metadata tab changes on the host. */
const MATCH_FIELDS = ['nova-steam-appid', 'nova-igdb-id']

/**
 * Copy fields the host changed for this app into the form (and its saved snapshot) so a later
 * Save keeps them. Other unsaved edits are left alone.
 *
 * @param {string[]} keys Fields to copy; ones the host no longer has are removed.
 * @param {boolean} [keepMissing] Leave fields the host doesn't report as they are.
 */
async function syncFromHost(keys, keepMissing = false) {
  const saved = (await fetchJson('./api/apps')).apps?.[props.index]
  if (!saved) return
  const before = JSON.parse(initial.value)
  for (const key of keys) {
    if (saved[key] === undefined) {
      if (keepMissing) continue
      delete form.value[key]
      delete before[key]
    } else {
      form.value[key] = saved[key]
      before[key] = saved[key]
    }
  }
  initial.value = JSON.stringify(before)
}

/** The Metadata tab changed the app on the host: pick up the new fields (best effort). */
async function onMetaChanged(keys, keepMissing) {
  try {
    await syncFromHost(keys, keepMissing)
  } catch {
    // The next open of the editor reads the app again.
  }
}

async function onMetaArtwork() {
  await onMetaChanged(ART_FIELDS, true)
  emit('artwork-applied')
}

/**
 * Apply full-quality library artwork to this (already saved) app on the host, then copy the new
 * file paths into the form so a later Save keeps them. Other unsaved edits are left alone.
 *
 * @returns {Promise<boolean>} False when the host can't apply artwork (older build).
 */
async function applyOnHost() {
  let job
  try {
    job = await applyArtwork(props.index, artwork.choice)
  } catch (error) {
    if (error?.status === 404) return false
    throw error
  }
  await pollJob(job)
  await syncFromHost(ART_FIELDS, true)
  emit('artwork-applied')
  return true
}

async function useArtwork() {
  const chosen = Object.values(artwork.choice || {}).some((id) => id && id !== 'none')
  if (!chosen) {
    artwork.open = false
    return
  }
  artwork.busy = true
  artwork.error = ''
  try {
    if (props.app && props.index >= 0 && await applyOnHost()) {
      artwork.open = false
      return
    }
    const id = artwork.choice.poster
    if (!id || id === 'none') {
      artwork.open = false
      return
    }
    const candidate = artwork.candidates.poster.find((c) => c.id === id)
    const data = await imageToPngBase64(candidateUrl(candidate))
    form.value['image-path'] = await uploadCoverData(`library_${id}`, data)
    artwork.open = false
  } catch {
    artwork.error = t('nova.library.art_apply_failed')
  } finally {
    artwork.busy = false
  }
}

async function onUpload(event) {
  const file = event.target.files?.[0]
  event.target.value = ''
  if (!file) return
  artwork.busy = true
  artwork.error = ''
  const url = URL.createObjectURL(file)
  try {
    const data = await imageToPngBase64(url)
    form.value['image-path'] = await uploadCoverData(`upload_${Date.now()}`, data)
    artwork.open = false
  } catch {
    artwork.error = t('nova.library.art_upload_failed')
  } finally {
    URL.revokeObjectURL(url)
    artwork.busy = false
  }
}

function confirmDiscard() {
  const then = discard.then
  discard.open = false
  discard.then = null
  initial.value = JSON.stringify(form.value)
  then?.()
}

/**
 * Open the file browser; `apply` receives the chosen path.
 *
 * @param {string} type Browse type for /api/browse.
 * @param {string} titleKey i18n key for the dialog title.
 * @param {string} start Path to start in.
 * @param {(path: string) => void} apply Where to put the result.
 */
function browse(type, titleKey, start, apply) {
  browser.value = { open: true, type, title: t(titleKey), start: start || '', apply }
}

function browseField(key, type, titleKey) {
  browse(type, titleKey, form.value[key], (p) => { form.value[key] = p })
}

function browseCompatPrefix() {
  const compat = form.value['nova-compat']
  browse('directory', 'nova.apps.browse_folder', compat.prefix, (p) => { compat.prefix = p })
}

function browsePrep(index, field) {
  browse('executable', 'nova.apps.browse_program', form.value['prep-cmd'][index][field], (p) => {
    form.value['prep-cmd'] = form.value['prep-cmd'].map((c, i) => (i === index ? { ...c, [field]: p } : c))
  })
}

function browseDetached(index) {
  browse('executable', 'nova.apps.browse_program', form.value.detached[index], (p) => {
    form.value.detached = form.value.detached.map((c, i) => (i === index ? p : c))
  })
}

async function save() {
  submitted.value = true
  const invalid = Object.keys(FIELD_IDS).find((key) => allErrors.value[key])
  if (invalid) {
    tab.value = 'details'
    await nextTick()
    document.getElementById(FIELD_IDS[invalid])?.focus()
    return
  }
  saving.value = true
  saveError.value = ''
  try {
    const payload = buildPayload(form.value)
    await postJson('./api/apps', payload)
    initial.value = JSON.stringify(form.value)
    emit('saved', payload.name)
  } catch {
    saveError.value = t('nova.apps.save_failed')
  } finally {
    saving.value = false
  }
}

defineExpose({ isDirty, askDiscard, openArtwork })
</script>

<template>
  <NvSheet :open="open" :title="title" :before-close="beforeClose" body-class="nv-editor__body">
    <div class="nv-editor__hero">
      <NvArt class="nv-editor__hero-art" :title="artTitle" :src="heroSrc" kind="hero" :show-title="false" decorative />
    </div>
    <div class="nv-editor__ident">
      <div class="nv-editor__poster">
        <NvArt :title="artTitle" :src="coverPreview" kind="poster" :show-title="false" decorative />
      </div>
      <div class="nv-editor__ident-text">
        <span class="nv-editor__name">{{ artTitle }}</span>
        <span class="nv-editor__meta">{{ metaLine }}</span>
      </div>
      <NvButton size="sm" class="nv-editor__change-art" @click="openArtwork"><ImagePlus :size="16" aria-hidden="true" />{{ t('nova.library.change_artwork') }}</NvButton>
    </div>

    <div v-if="showTabs" class="nv-editor__tabs">
      <NvSegmentedControl v-model="tab" :label="t('nova.library.tabs_label')" :options="tabs" size="sm" />
    </div>
    <AppMetadataPanel v-if="showTabs && tab === 'metadata'" :index="index" :name="app?.name || ''" :draft-name="form.name"
                      @match-changed="onMetaChanged(MATCH_FIELDS)" @artwork-applied="onMetaArtwork" />
    <AppArtworkPanel v-if="showTabs && tab === 'artwork'" :index="index" :app="app" :name="app?.name || ''"
                     :cover-version="coverVersion" @artwork-applied="onMetaArtwork" />

    <form v-show="tab === 'details'" id="nv-app-editor" class="nv-editor" novalidate @submit.prevent="save" @focusout="onFocusOut">
      <NvAlert v-if="saveError" variant="danger" live :title="t('nova.apps.save_failed_title')">{{ saveError }}</NvAlert>

      <section class="nv-editor__section" :aria-label="t('nova.apps.section_basics')">
        <NvTextField v-model="form.name" :label="t('nova.apps.field_name')" :hint="t('nova.apps.field_name_hint')"
                     :error="errors.name ? t(errors.name) : ''" :id="FIELD_IDS.name" required />
        <PathField v-model="form.cmd" :label="t('nova.apps.field_command')" :hint="t('nova.apps.field_command_hint')"
                   @browse="browseField('cmd', 'executable', 'nova.apps.browse_program')" />
        <PathField v-model="form['working-dir']" :label="t('nova.apps.field_working_dir')" :hint="t('nova.apps.field_working_dir_hint')"
                   @browse="browseField('working-dir', 'directory', 'nova.apps.browse_folder')" />
      </section>

      <section class="nv-editor__section" aria-labelledby="nv-editor-prep">
        <h3 id="nv-editor-prep" class="nv-editor__heading">{{ t('nova.apps.section_prep') }}</h3>
        <p class="nv-editor__desc">{{ t('nova.apps.prep_desc') }}</p>
        <div class="nv-editor__group">
          <NvSettingRow :label="t('nova.apps.field_global_prep')" :description="t('nova.apps.field_global_prep_hint')">
            <template #default="{ labelId, descriptionId }">
              <NvSwitch v-model="runGlobalPrep" :labelledby="labelId" :describedby="descriptionId" show-state />
            </template>
          </NvSettingRow>
        </div>
        <PrepCommandList v-model="form['prep-cmd']" :platform="platform" @browse="browsePrep" />
      </section>

      <section v-if="!isWindows && form['nova-compat']" class="nv-editor__section" aria-labelledby="nv-editor-compat">
        <h3 id="nv-editor-compat" class="nv-editor__heading">{{ t('nova.apps.section_compat') }}</h3>
        <p class="nv-editor__desc">{{ t('nova.apps.compat_desc') }}</p>
        <PathField v-model="form['nova-compat'].prefix" :label="t('nova.apps.compat_prefix')" :hint="t('nova.apps.compat_prefix_hint')"
                   @browse="browseCompatPrefix" />
        <NvSelect v-model="form['nova-compat'].fsr" :label="t('nova.apps.compat_fsr')" :hint="t('nova.apps.compat_fsr_hint')" :options="fsrOptions" />
        <NvNumberField v-model="form['nova-compat'].fps_cap" :label="t('nova.apps.compat_fps_cap')" :unit="t('nova.apps.compat_fps')"
                       :min="0" :hint="t('nova.apps.compat_fps_cap_hint')" />
        <div class="nv-editor__group">
          <NvSettingRow :label="t('nova.apps.compat_mangohud')" :description="t('nova.apps.compat_mangohud_hint')">
            <template #default="{ labelId, descriptionId }">
              <NvSwitch v-model="form['nova-compat'].mangohud" :labelledby="labelId" :describedby="descriptionId" show-state />
            </template>
          </NvSettingRow>
        </div>
        <NvTextField v-model="form['nova-compat'].proton_version" :label="t('nova.apps.compat_proton')" :hint="t('nova.apps.compat_proton_hint')"
                     placeholder="latest" mono />
        <NvTextField v-model="form['nova-compat'].extra_env" :label="t('nova.apps.compat_env')" :hint="t('nova.apps.compat_env_hint')"
                     placeholder="DXVK_HUD=fps" mono multiline />
      </section>

      <details class="nv-editor__advanced">
        <summary>{{ t('nova.library.advanced') }}</summary>
        <div class="nv-editor__advanced-body">
          <section class="nv-editor__section" aria-labelledby="nv-editor-cover">
            <h3 id="nv-editor-cover" class="nv-editor__heading">{{ t('nova.apps.section_cover') }}</h3>
            <PathField v-model="form['image-path']" :label="t('nova.apps.field_image')" :hint="t('nova.apps.field_image_hint')"
                       @browse="browseField('image-path', 'file', 'nova.apps.browse_file')" />
            <div><NvButton size="sm" @click="coversOpen = true"><Search :size="16" aria-hidden="true" />{{ t('nova.apps.find_cover') }}</NvButton></div>
          </section>

          <section class="nv-editor__section" aria-labelledby="nv-editor-detached">
            <h3 id="nv-editor-detached" class="nv-editor__heading">{{ t('nova.apps.section_detached') }}</h3>
            <p class="nv-editor__desc">{{ t('nova.apps.detached_desc') }}</p>
            <DetachedCommandList v-model="form.detached" @browse="browseDetached" />
          </section>

          <section class="nv-editor__section" aria-labelledby="nv-editor-behaviour">
            <h3 id="nv-editor-behaviour" class="nv-editor__heading">{{ t('nova.apps.section_behaviour') }}</h3>
            <div class="nv-editor__group">
              <NvSettingRow :label="t('nova.apps.field_auto_detach')" :description="t('nova.apps.field_auto_detach_hint')">
                <template #default="{ labelId, descriptionId }">
                  <NvSwitch v-model="form['auto-detach']" :labelledby="labelId" :describedby="descriptionId" show-state />
                </template>
              </NvSettingRow>
              <NvSettingRow :label="t('nova.apps.field_wait_all')" :description="t('nova.apps.field_wait_all_hint')">
                <template #default="{ labelId, descriptionId }">
                  <NvSwitch v-model="form['wait-all']" :labelledby="labelId" :describedby="descriptionId" show-state />
                </template>
              </NvSettingRow>
              <NvSettingRow v-if="isWindows" :label="t('nova.apps.field_elevated')" :description="t('nova.apps.field_elevated_hint')">
                <template #default="{ labelId, descriptionId }">
                  <NvSwitch v-model="form.elevated" :labelledby="labelId" :describedby="descriptionId" show-state />
                </template>
              </NvSettingRow>
            </div>
            <NvNumberField v-model="form['exit-timeout']" :label="t('nova.apps.field_exit_timeout')" :unit="t('nova.apps.seconds')"
                           :min="0" :hint="t('nova.apps.field_exit_timeout_hint')"
                           :error="errors.exitTimeout ? t(errors.exitTimeout) : ''" :id="FIELD_IDS.exitTimeout" />
            <PathField v-model="form.output" :label="t('nova.apps.field_output')" :hint="t('nova.apps.field_output_hint')"
                       @browse="browseField('output', 'any', 'nova.apps.browse_file')" />
          </section>

          <EnvVarsReference :platform="platform" />
        </div>
      </details>
    </form>

    <template v-if="!isNew" #footer-start>
      <NvActionMenu align="start" :label="t('nova.apps.more')" :items="footerMenu" />
    </template>
    <template #footer>
      <NvButton @click="requestClose">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton type="submit" form="nv-app-editor" variant="primary" :loading="saving">{{ t('nova.apps.save') }}</NvButton>
    </template>
  </NvSheet>

  <NvSheet v-model:open="artwork.open" size="lg" :title="t('nova.library.artwork_title', { name: artTitle })"
           :subtitle="t('nova.library.artwork_subtitle')">
    <NvAlert v-if="artwork.error" variant="danger" live :title="t('nova.library.art_error_title')">{{ artwork.error }}</NvAlert>
    <ArtworkPicker v-if="libraryApi" v-model="artwork.choice" :artwork="artwork.candidates" :title="artTitle"
                   :kinds="['poster']" :loading="artwork.loading">
      <template #footnote><p class="nv-editor__desc">{{ t('nova.library.art_other_kinds') }}</p></template>
    </ArtworkPicker>
    <label class="nv-editor__upload">
      <ImagePlus :size="18" aria-hidden="true" />
      <span>{{ t('nova.library.art_upload') }}</span>
      <input type="file" accept="image/png,image/jpeg,image/webp" class="nv-visually-hidden" @change="onUpload" />
    </label>
    <div><NvButton size="sm" variant="ghost" @click="coversOpen = true"><Search :size="16" aria-hidden="true" />{{ t('nova.apps.find_cover') }}</NvButton></div>
    <template #footer>
      <NvButton @click="artwork.open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton variant="primary" :loading="artwork.busy" @click="useArtwork">{{ t('nova.addgames.use_artwork') }}</NvButton>
    </template>
  </NvSheet>

  <NvConfirmDialog v-model:open="discard.open" :title="t('nova.apps.discard_title')" :description="t('nova.apps.discard_desc')"
                   :confirm-label="t('nova.apps.discard')" @confirm="confirmDiscard" />

  <FileBrowserDialog v-model:open="browser.open" :type="browser.type" :title="browser.title" :start-path="browser.start"
                     @select="(p) => browser.apply?.(p)" />
  <CoverFinderDialog v-model:open="coversOpen" :initial-query="form.name" @chosen="(p) => { form['image-path'] = p; artwork.open = false }" />
</template>

<style>
@layer components {
  .nv-sheet__body.nv-editor__body {
    padding-top: 0;
  }

  .nv-editor__hero {
    flex-shrink: 0;
    margin: 0 calc(-1 * var(--nv-space-6));
    height: 160px;
    overflow: hidden;
  }

  .nv-editor__ident,
  .nv-editor {
    flex-shrink: 0;
  }

  .nv-editor__hero-art {
    width: 100%;
    height: 100%;
  }

  .nv-editor__ident {
    position: relative;
    display: flex;
    align-items: flex-end;
    gap: var(--nv-space-4);
    margin-top: -56px;
    margin-bottom: var(--nv-space-5);
  }

  .nv-editor__poster {
    flex-shrink: 0;
    width: 76px;
    border: 2px solid var(--nv-panel);
    border-radius: var(--nv-radius-md);
    overflow: hidden;
    box-shadow: var(--nv-shadow);
  }

  .nv-editor__ident-text {
    flex-grow: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding-bottom: var(--nv-space-1);
  }

  .nv-editor__name {
    font-size: var(--nv-text-lg);
    font-weight: 600;
    overflow-wrap: anywhere;
  }

  .nv-editor__meta,
  .nv-editor__desc {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-editor__change-art {
    flex-shrink: 0;
  }

  .nv-editor__tabs {
    flex-shrink: 0;
    margin-bottom: var(--nv-space-5);
  }

  .nv-editor {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-6);
  }

  .nv-editor__section {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
  }

  .nv-editor__heading {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-editor__group {
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-border);
    background: var(--nv-raised);
  }

  .nv-editor__advanced {
    border-top: 1px solid var(--nv-divider);
    padding-top: var(--nv-space-3);
  }

  .nv-editor__advanced > summary {
    min-height: 36px;
    display: flex;
    align-items: center;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
    cursor: pointer;
  }

  .nv-editor__advanced-body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-6);
    padding-top: var(--nv-space-4);
  }

  .nv-editor__upload {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 56px;
    padding: 0 var(--nv-space-4);
    border: 1px dashed var(--nv-border-strong);
    border-radius: var(--nv-radius-lg);
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-editor__upload:focus-within {
    outline: var(--nv-focus-width) solid var(--nv-focus);
    outline-offset: 2px;
  }

  @media (max-width: 1023px) {
    .nv-editor__hero {
      margin: 0 calc(-1 * var(--nv-space-4));
    }
  }

  @media (max-width: 600px) {
    .nv-editor__ident {
      flex-wrap: wrap;
    }
  }
}
</style>
