<script setup>
/**
 * Desktop preview (Final_Dashboard, direction C): a 16:9 frame of what a device sees, the display
 * chip (with a display switcher when there are several), refresh, and a "Live" / "Ready — nobody
 * watching" badge. Frames come from the shared poller in usePreview.js, which only runs while this
 * card is on screen and the tab is visible.
 *
 * Props: live (a stream is running), device (name of the watching device), displays (/api/displays
 *        list or null).
 * Emits: details (open session details while live).
 */
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { Monitor, RefreshCw } from '@lucide/vue'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import NvIconButton from '../../components/NvIconButton.vue'
import NvActionMenu from '../../components/NvActionMenu.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { displayLabel, pickDisplay } from './format'
import { previewState, refreshPreview, selectPreviewDisplay, usePreviewWhileVisible } from './usePreview'

const props = defineProps({
  live: { type: Boolean, default: false },
  device: { type: String, default: '' },
  displays: { type: Array, default: null },
})
defineEmits(['details'])

const { t } = useI18n()
const root = ref(null)
usePreviewWhileVisible(root)

const current = computed(() => pickDisplay(props.displays || [], previewState.display))
const chip = computed(() => displayLabel(current.value))
const displayMenu = computed(() => (props.displays || []).map((d) => ({
  id: d.name,
  label: displayLabel(d),
  checked: current.value?.name === d.name,
  onSelect: () => selectPreviewDisplay(d.name),
})))
const caption = computed(() => (props.live
  ? t('nova.overview.preview_live_caption', { device: props.device || t('nova.live.unknown_device') })
  : t('nova.overview.preview_idle_caption')))
const fullUrl = computed(() => {
  const q = new URLSearchParams({ w: '1280' })
  if (current.value?.name) q.set('display', current.value.name)
  return `./api/preview?${q}`
})
const alt = computed(() => t('nova.overview.preview_alt', { display: current.value?.name || t('nova.overview.this_desktop') }))
</script>

<template>
  <NvCard ref="root" :title="t('nova.overview.preview_title')" class="nv-pv">
      <template #actions>
        <span v-if="chip && displayMenu.length <= 1" class="nv-mono nv-pv__chip">{{ chip }}</span>
        <NvActionMenu v-else-if="displayMenu.length > 1" :label="t('nova.overview.switch_display')" :items="displayMenu" size="sm">
          <template #trigger><span class="nv-mono nv-pv__chip nv-pv__chip--menu"><Monitor :size="14" aria-hidden="true" />{{ chip }}</span></template>
        </NvActionMenu>
        <NvIconButton :label="t('nova.overview.refresh_preview')" size="sm" @click="refreshPreview()"><RefreshCw :size="16" /></NvIconButton>
      </template>

      <div class="nv-pv__frame" :aria-busy="previewState.loading ? 'true' : null">
        <img v-if="previewState.url" :src="previewState.url" :alt="alt" class="nv-pv__img">
        <div v-else-if="previewState.loading || previewState.available === null" class="nv-pv__placeholder" aria-hidden="true">
          <NvSkeleton width="100%" height="100%" radius="0" />
        </div>
        <div v-else class="nv-pv__placeholder nv-pv__placeholder--msg">
          <Monitor :size="28" aria-hidden="true" />
          <span>{{ t('nova.overview.preview_unavailable') }}</span>
        </div>
        <span :class="['nv-pv__badge', { 'nv-pv__badge--live': live }]">
          <span class="nv-pv__badge-dot" aria-hidden="true"></span>{{ live ? t('nova.live.live') : t('nova.overview.preview_ready') }}
        </span>
        <span class="nv-visually-hidden" aria-live="polite">{{ previewState.unavailable && !previewState.url ? t('nova.overview.preview_unavailable') : '' }}</span>
      </div>

      <div class="nv-pv__foot">
        <span class="nv-pv__caption">{{ caption }}</span>
        <NvButton v-if="live" size="sm" @click="$emit('details')">{{ t('nova.overview.session_details') }}</NvButton>
        <NvButton v-else size="sm" :href="fullUrl" target="_blank" rel="noopener">{{ t('nova.overview.open_full_preview') }}</NvButton>
      </div>
  </NvCard>
</template>

<style>
@layer components {
  .nv-pv .nv-card__body {
    gap: var(--nv-space-4);
    padding: var(--nv-space-4);
  }

  .nv-pv__chip {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
    white-space: nowrap;
  }

  .nv-pv__chip--menu {
    padding: 0 var(--nv-space-2);
  }

  .nv-pv__frame {
    position: relative;
    aspect-ratio: 16 / 9;
    border-radius: var(--nv-radius-md);
    overflow: hidden;
    border: 1px solid var(--nv-divider);
    background: var(--nv-raised);
  }

  .nv-pv__img,
  .nv-pv__placeholder {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
  }

  .nv-pv__img {
    object-fit: contain;
    background: #000;
  }

  .nv-pv__placeholder--msg {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: var(--nv-space-2);
    padding: var(--nv-space-4);
    text-align: center;
    color: var(--nv-text-secondary);
  }

  .nv-pv__badge {
    position: absolute;
    left: var(--nv-space-3);
    top: var(--nv-space-3);
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 3px 10px;
    border-radius: var(--nv-radius-sm);
    font-size: var(--nv-text-xs);
    font-weight: 500;
    background: var(--nv-scrim);
    color: var(--nv-text);
  }

  .nv-pv__badge-dot {
    width: 6px;
    height: 6px;
    border-radius: 3px;
    background: var(--nv-success);
  }

  .nv-pv__badge--live {
    background: var(--nv-danger-tint);
    color: var(--nv-danger);
  }

  .nv-pv__badge--live .nv-pv__badge-dot {
    background: var(--nv-danger);
  }

  .nv-pv__foot {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
  }

  @media (max-width: 479px) {
    .nv-pv__chip:not(.nv-pv__chip--menu) {
      display: none;
    }
  }

  .nv-pv__caption {
    flex-grow: 1;
    min-width: 0;
    color: var(--nv-text-secondary);
  }
}
</style>
