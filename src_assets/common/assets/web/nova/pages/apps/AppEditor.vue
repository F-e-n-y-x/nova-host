<script setup>
/**
 * Add or edit an application in a side panel (full-screen sheet on small screens).
 * Fields validate when they lose focus; Save focuses the first invalid field. Closing
 * with unsaved edits asks first; the page's route guard uses askDiscard() the same way,
 * so Back never drops edits silently.
 *
 * Props: open, app (the app to edit, or null to add one), index (-1 to add),
 *        platform (host platform from /api/config).
 * Emits: saved(name) after the host accepts the change, close when it should close.
 * Exposes: isDirty (boolean), askDiscard(onDiscard).
 */
import { computed, nextTick, reactive, ref, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Search } from '@lucide/vue'
import NvDialog from '../../components/NvDialog.vue'
import NvButton from '../../components/NvButton.vue'
import NvTextField from '../../components/NvTextField.vue'
import NvNumberField from '../../components/NvNumberField.vue'
import NvSwitch from '../../components/NvSwitch.vue'
import NvSettingRow from '../../components/NvSettingRow.vue'
import NvAlert from '../../components/NvAlert.vue'
import AppSheet from './AppSheet.vue'
import PathField from './PathField.vue'
import PrepCommandList from './PrepCommandList.vue'
import DetachedCommandList from './DetachedCommandList.vue'
import EnvVarsReference from './EnvVarsReference.vue'
import FileBrowserDialog from './FileBrowserDialog.vue'
import CoverFinderDialog from './CoverFinderDialog.vue'
import { postJson } from '../../api'
import { buildPayload, formFromApp, formsDiffer, newAppForm, validateForm } from './appForm'

const props = defineProps({
  open: { type: Boolean, default: false },
  app: { type: Object, default: null },
  index: { type: Number, default: -1 },
  platform: { type: String, default: '' },
})
const emit = defineEmits(['saved', 'close'])
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
const browser = ref({ open: false, type: 'any', title: '', start: '', apply: null })

const isWindows = computed(() => props.platform === 'windows')
const isNew = computed(() => props.index === -1)
const title = computed(() => (isNew.value ? t('nova.apps.add_title') : t('nova.apps.edit_title', { name: props.app?.name || t('nova.apps.unnamed') })))
const isDirty = computed(() => formsDiffer(form.value, JSON.parse(initial.value || '{}')))
const allErrors = computed(() => validateForm(form.value))
const errors = computed(() => Object.fromEntries(Object.entries(allErrors.value)
  .filter(([key]) => submitted.value || touched.has(key))))
const coverPreview = computed(() => {
  if (isNew.value || !form.value['image-path']) return ''
  return form.value['image-path'] === props.app?.['image-path'] ? `./api/covers/${props.index}` : ''
})
const coverInitial = computed(() => (form.value.name || '?').charAt(0).toUpperCase())
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

defineExpose({ isDirty, askDiscard })
</script>

<template>
  <AppSheet :open="open" :title="title" @close-request="requestClose">
    <form id="nv-app-editor" class="nv-editor" novalidate @submit.prevent="save" @focusout="onFocusOut">
      <NvAlert v-if="saveError" variant="danger" live :title="t('nova.apps.save_failed_title')">{{ saveError }}</NvAlert>

      <section class="nv-editor__section" aria-labelledby="nv-editor-basics">
        <h3 id="nv-editor-basics" class="nv-editor__heading">{{ t('nova.apps.section_basics') }}</h3>
        <NvTextField v-model="form.name" :label="t('nova.apps.field_name')" :hint="t('nova.apps.field_name_hint')"
                     :error="errors.name ? t(errors.name) : ''" :id="FIELD_IDS.name" required />
        <PathField v-model="form.cmd" :label="t('nova.apps.field_command')" :hint="t('nova.apps.field_command_hint')"
                   @browse="browseField('cmd', 'executable', 'nova.apps.browse_program')" />
        <PathField v-model="form['working-dir']" :label="t('nova.apps.field_working_dir')" :hint="t('nova.apps.field_working_dir_hint')"
                   @browse="browseField('working-dir', 'directory', 'nova.apps.browse_folder')" />
      </section>

      <section class="nv-editor__section" aria-labelledby="nv-editor-cover">
        <h3 id="nv-editor-cover" class="nv-editor__heading">{{ t('nova.apps.section_cover') }}</h3>
        <div class="nv-editor__cover">
          <div class="nv-editor__cover-preview">
            <img v-if="coverPreview" :src="coverPreview" alt="" class="nv-editor__cover-img" />
            <span v-else aria-hidden="true">{{ coverInitial }}</span>
          </div>
          <div class="nv-editor__cover-fields">
            <PathField v-model="form['image-path']" :label="t('nova.apps.field_image')" :hint="t('nova.apps.field_image_hint')"
                       @browse="browseField('image-path', 'file', 'nova.apps.browse_file')" />
            <div>
              <NvButton size="sm" @click="coversOpen = true"><Search :size="16" aria-hidden="true" />{{ t('nova.apps.find_cover') }}</NvButton>
            </div>
          </div>
        </div>
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
    </form>

    <template #footer>
      <NvButton @click="requestClose">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton type="submit" form="nv-app-editor" variant="primary" :loading="saving">{{ t('nova.apps.save') }}</NvButton>
    </template>
  </AppSheet>

  <NvDialog v-model:open="discard.open" :title="t('nova.apps.discard_title')" :description="t('nova.apps.discard_desc')">
    <template #footer>
      <NvButton autofocus @click="discard.open = false">{{ t('nova.apps.keep_editing') }}</NvButton>
      <NvButton variant="danger-solid" @click="confirmDiscard">{{ t('nova.apps.discard') }}</NvButton>
    </template>
  </NvDialog>

  <FileBrowserDialog v-model:open="browser.open" :type="browser.type" :title="browser.title" :start-path="browser.start"
                     @select="(p) => browser.apply?.(p)" />
  <CoverFinderDialog v-model:open="coversOpen" :initial-query="form.name" @chosen="(p) => (form['image-path'] = p)" />
</template>

<style>
@layer components {
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
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-editor__desc {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-editor__group {
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-border);
    background: var(--nv-raised);
  }

  .nv-editor__cover {
    display: flex;
    gap: var(--nv-space-4);
    align-items: flex-start;
  }

  .nv-editor__cover-preview {
    flex-shrink: 0;
    width: 96px;
    aspect-ratio: 3 / 4;
    display: flex;
    align-items: center;
    justify-content: center;
    overflow: hidden;
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-2xl);
    font-weight: 600;
  }

  .nv-editor__cover-img {
    width: 100%;
    height: 100%;
    object-fit: cover;
  }

  .nv-editor__cover-fields {
    flex-grow: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  @media (max-width: 600px) {
    .nv-editor__group {
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-border);
    background: var(--nv-raised);
  }

  .nv-editor__cover {
      flex-direction: column;
    }
  }
}
</style>
