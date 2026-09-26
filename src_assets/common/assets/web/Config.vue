<script setup>
/**
 * Settings page: every host option, grouped into sections with a section nav, search,
 * "Changed" markers against defaults, and a sticky bar to save (and restart) or discard.
 */
import { computed, nextTick, onBeforeUnmount, onMounted, shallowRef, watch } from 'vue'
import { onBeforeRouteLeave, useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { SearchX } from '@lucide/vue'
import NvAlert from './nova/components/NvAlert.vue'
import NvButton from './nova/components/NvButton.vue'
import NvDialog from './nova/components/NvDialog.vue'
import NvEmptyState from './nova/components/NvEmptyState.vue'
import NvPage from './nova/components/NvPage.vue'
import NvSkeleton from './nova/components/NvSkeleton.vue'
import NvTextField from './nova/components/NvTextField.vue'
import SettingsNav from './configs/components/SettingsNav.vue'
import SettingsSaveBar from './configs/components/SettingsSaveBar.vue'
import SettingsSection from './configs/components/SettingsSection.vue'
import { toast } from './nova/toast.js'
import { useScrollSpy } from './configs/useScrollSpy.js'
import { useSettingsForm } from './configs/useSettingsForm.js'
import { useSettingsSections } from './configs/useSettingsSections.js'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()
const form = useSettingsForm()
const query = shallowRef(typeof route.query.q === 'string' ? route.query.q : '')
const { sections, matchCount, searching } = useSettingsSections(form, query)
const sectionIds = computed(() => sections.value.map((s) => s.id))
const { active } = useScrollSpy(sectionIds)
const errorCount = computed(() => Object.keys(form.visibleErrors.value).length)
const dirtyCount = computed(() => form.dirtyKeys.value.length)

const SEARCH_ID = 'settings-search'

/** Focus the first invalid setting, in page order. */
async function focusFirstError() {
  await nextTick()
  const keys = sections.value.flatMap((s) => [...s.items, ...s.primaryGroups.flatMap((g) => g.items),
    ...s.otherGroups.flatMap((g) => g.items)]).filter((i) => i.kind === 'field').map((i) => i.key)
  const first = keys.find((key) => form.errors.value[key])
  if (!first) return
  const row = document.getElementById(first)
  const control = row?.querySelector('[aria-invalid="true"], input, select, button[role="switch"]')
  row?.scrollIntoView({ block: 'center' })
  ;(control || row)?.focus()
}

async function save() {
  try {
    const ok = await form.save()
    if (ok) toast.success(t('nova.settings.saved'))
    else await focusFirstError()
  } catch (error) {
    toast.danger(t('nova.settings.save_failed', { error: error.message }))
  }
}

async function saveAndRestart() {
  try {
    const ok = await form.saveAndRestart()
    if (ok === true) toast.success(t('nova.settings.restarted'))
    else if (ok === false) toast.warning(t('nova.settings.restart_slow'))
    else await focusFirstError()
  } catch (error) {
    toast.danger(t('nova.settings.save_failed', { error: error.message }))
  }
}

async function restart() {
  const ok = await form.restart()
  if (ok) toast.success(t('nova.settings.restarted'))
  else toast.warning(t('nova.settings.restart_slow'))
}

/** Scroll to and focus the section or setting named by the URL hash. */
async function revealHash() {
  const id = decodeURIComponent(route.hash.replace(/^#/, ''))
  if (!id || !form.config.value) return
  await nextTick()
  const el = document.getElementById(id)
  if (!el) return
  el.scrollIntoView({ block: 'start', behavior: 'smooth' })
  el.focus({ preventScroll: true })
  if (el.classList.contains('nv-cfg-row')) {
    el.classList.add('nv-cfg-row--flash')
    setTimeout(() => el.classList.remove('nv-cfg-row--flash'), 2400)
  }
}
watch(() => route.hash, revealHash)

// Discarding edits loses them: confirm first.
const discardOpen = shallowRef(false)
function confirmDiscard() {
  discardOpen.value = false
  form.discard()
}

// Search is part of the URL (?q=), so a filtered view can be shared or restored with Back.
let queryTimer = null
watch(query, (q) => {
  clearTimeout(queryTimer)
  queryTimer = setTimeout(() => {
    const next = { ...route.query }
    if (q.trim()) next.q = q
    else delete next.q
    // Drop the hash while searching so the router doesn't scroll back to a section.
    router.replace({ query: next, hash: q.trim() ? '' : route.hash })
  }, 300)
})

// Leaving with unsaved changes: ask first.
const leaveOpen = shallowRef(false)
let resolveLeave = null
onBeforeRouteLeave(() => {
  if (dirtyCount.value === 0) return true
  leaveOpen.value = true
  return new Promise((resolve) => { resolveLeave = resolve })
})
function answerLeave(leave) {
  leaveOpen.value = false
  resolveLeave?.(leave)
  resolveLeave = null
}

function onBeforeUnload(event) {
  if (dirtyCount.value > 0) event.preventDefault()
}

/** "/" jumps to search, like most settings screens. */
function onKeydown(event) {
  if (event.key !== '/' || event.ctrlKey || event.metaKey || event.altKey) return
  const target = event.target
  if (target instanceof HTMLElement && (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName))) return
  event.preventDefault()
  document.getElementById(SEARCH_ID)?.focus()
}

onMounted(async () => {
  window.addEventListener('beforeunload', onBeforeUnload)
  window.addEventListener('keydown', onKeydown)
  await form.load()
  revealHash()
})
onBeforeUnmount(() => {
  window.removeEventListener('beforeunload', onBeforeUnload)
  window.removeEventListener('keydown', onKeydown)
  clearTimeout(queryTimer)
})
</script>

<template>
  <NvPage :title="t('nova.nav.settings')">
    <template #subtitle>{{ t('nova.settings.subtitle') }}</template>
    <template #actions>
      <div class="nv-settings-search" role="search">
        <NvTextField :id="SEARCH_ID" v-model="query" type="search" :label="t('nova.settings.search')" hide-label
                     :placeholder="t('nova.settings.search_placeholder')" autocomplete="off" />
        <p class="nv-visually-hidden" role="status" aria-live="polite">
          {{ searching ? t('nova.settings.results', { n: matchCount }, matchCount) : '' }}
        </p>
      </div>
    </template>

    <div v-if="form.loading.value && !form.config.value" class="nv-settings-layout" aria-busy="true">
      <span class="nv-visually-hidden" role="status">{{ t('nova.common.loading') }}</span>
      <div class="nv-settings-skeleton-nav"><NvSkeleton v-for="n in 8" :key="n" height="28px" /></div>
      <div class="nv-settings-body">
        <div v-for="n in 3" :key="n" class="nv-settings-skeleton-card">
          <NvSkeleton width="40%" height="18px" />
          <NvSkeleton :lines="3" />
        </div>
      </div>
    </div>

    <NvAlert v-else-if="form.loadError.value" variant="danger" :title="t('nova.settings.load_failed')" live>
      {{ form.loadError.value.message }}
      <template #actions><NvButton size="sm" @click="form.load()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <div v-else-if="form.config.value" class="nv-settings-layout">
      <SettingsNav :sections="sections" :active="active" :searching="searching" />
      <div class="nv-settings-body">
        <NvEmptyState v-if="searching && sections.length === 0" :title="t('nova.settings.no_results', { q: query.trim() })"
                      :description="t('nova.settings.no_results_hint')">
          <template #icon><SearchX :size="28" /></template>
          <template #actions><NvButton @click="query = ''">{{ t('nova.settings.clear_search') }}</NvButton></template>
        </NvEmptyState>

        <SettingsSection v-for="section in sections" :key="section.id" :section="section" :config="form.config.value"
                         :platform="form.platform.value" :errors="form.visibleErrors.value"
                         :other-open="section.otherMatches" @update="form.setValue" @reset="form.resetValue"
                         @touch="form.touch" />

        <SettingsSaveBar :count="dirtyCount" :error-count="errorCount" :saving="form.saving.value"
                         :restarting="form.restarting.value" :needs-restart="form.needsRestart.value"
                         @discard="discardOpen = true" @save="save" @save-restart="saveAndRestart" @restart="restart"
                         @dismiss="form.needsRestart.value = false" />
      </div>
    </div>

    <NvDialog v-model:open="leaveOpen" :title="t('nova.settings.leave_title')"
              :description="t('nova.settings.leave_desc', { n: dirtyCount }, dirtyCount)" @close="answerLeave(false)">
      <template #footer>
        <NvButton autofocus @click="answerLeave(false)">{{ t('nova.settings.leave_stay') }}</NvButton>
        <NvButton variant="danger-solid" @click="answerLeave(true)">{{ t('nova.settings.leave_discard') }}</NvButton>
      </template>
    </NvDialog>

    <NvDialog v-model:open="discardOpen" :title="t('nova.settings.discard_title')"
              :description="t('nova.settings.discard_desc', { n: dirtyCount }, dirtyCount)">
      <template #footer>
        <NvButton autofocus @click="discardOpen = false">{{ t('nova.common.cancel') }}</NvButton>
        <NvButton variant="danger-solid" @click="confirmDiscard">{{ t('nova.settings.discard_confirm') }}</NvButton>
      </template>
    </NvDialog>
  </NvPage>
</template>

<style>
@layer components {
  .nv-settings-search {
    width: 320px;
  }

  .nv-settings-layout {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-8);
  }

  .nv-settings-body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-8);
    flex-grow: 1;
    min-width: 0;
    max-width: 880px;
  }

  .nv-settings-skeleton-nav {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    width: 196px;
    flex-shrink: 0;
  }

  .nv-settings-skeleton-card {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding: var(--nv-space-5);
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
  }

  @media (max-width: 1023px) {
    .nv-settings-layout {
      flex-direction: column;
      align-items: stretch;
      gap: var(--nv-space-5);
    }

    .nv-settings-skeleton-nav {
      flex-direction: row;
      width: auto;
    }

    .nv-settings-body {
      max-width: none;
    }
  }

  @media (max-width: 699px) {
    .nv-settings-search {
      width: 100%;
    }

    .nv-page__actions:has(.nv-settings-search) {
      width: 100%;
    }
  }
}
</style>
