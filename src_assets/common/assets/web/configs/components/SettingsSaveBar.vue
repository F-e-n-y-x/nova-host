<script setup>
/**
 * Sticky bar at the bottom of the Settings page. With unsaved changes it offers Discard,
 * Save and Save and restart; after a save without restart it offers Restart now.
 *
 * Props: count (unsaved changes), errorCount (errors currently shown), saving, restarting, needsRestart.
 * Save stays enabled with errors: pressing it shows every error and focuses the first.
 * Emits: discard, save, save-restart, restart, dismiss.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvButton from '../../nova/components/NvButton.vue'

const props = defineProps({
  count: { type: Number, default: 0 },
  errorCount: { type: Number, default: 0 },
  saving: { type: Boolean, default: false },
  restarting: { type: Boolean, default: false },
  needsRestart: { type: Boolean, default: false },
})
defineEmits(['discard', 'save', 'save-restart', 'restart', 'dismiss'])
const { t } = useI18n()

const mode = computed(() => {
  if (props.restarting) return 'restarting'
  if (props.count > 0) return 'dirty'
  if (props.needsRestart) return 'restart'
  return null
})
const message = computed(() => {
  if (mode.value === 'restarting') return t('nova.settings.restarting')
  if (mode.value === 'restart') return t('nova.settings.saved_restart')
  if (props.errorCount > 0) return t('nova.settings.fix_errors', { n: props.errorCount }, props.errorCount)
  return t('nova.settings.unsaved', { n: props.count }, props.count)
})
</script>

<template>
  <div v-if="mode" class="nv-savebar" role="region" :aria-label="t('nova.settings.savebar_label')">
    <p class="nv-savebar__message" role="status" aria-live="polite">
      <span :class="['nv-savebar__dot', { 'nv-savebar__dot--error': errorCount > 0 && mode === 'dirty' }]"
            aria-hidden="true"></span>{{ message }}
    </p>
    <div v-if="mode === 'dirty'" class="nv-savebar__actions">
      <NvButton :disabled="saving" @click="$emit('discard')">{{ t('nova.settings.discard') }}</NvButton>
      <NvButton :loading="saving" @click="$emit('save')">{{ t('_common.save') }}</NvButton>
      <NvButton variant="primary" :disabled="saving" @click="$emit('save-restart')">
        {{ t('nova.settings.save_restart') }}
      </NvButton>
    </div>
    <div v-else-if="mode === 'restart'" class="nv-savebar__actions">
      <NvButton @click="$emit('dismiss')">{{ t('nova.settings.later') }}</NvButton>
      <NvButton variant="primary" @click="$emit('restart')">{{ t('nova.settings.restart_now') }}</NvButton>
    </div>
  </div>
</template>

<style>
@layer components {
  .nv-savebar {
    position: sticky;
    bottom: calc(var(--nv-space-4) + env(safe-area-inset-bottom, 0px));
    z-index: 20;
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3) var(--nv-space-3) var(--nv-space-3) var(--nv-space-5);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-raised);
    border: 1px solid var(--nv-border-strong);
    box-shadow: var(--nv-shadow);
  }

  .nv-savebar__message {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    flex-grow: 1;
    margin: 0;
    font-weight: 500;
    font-variant-numeric: tabular-nums;
  }

  .nv-savebar__dot {
    width: 8px;
    height: 8px;
    border-radius: 4px;
    background: var(--nv-accent-text);
  }

  .nv-savebar__dot--error {
    background: var(--nv-danger);
  }

  .nv-savebar__actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  @media (max-width: 699px) {
    .nv-savebar {
      bottom: calc(var(--nv-space-2) + env(safe-area-inset-bottom, 0px));
      padding: var(--nv-space-3);
    }

    .nv-savebar__actions {
      width: 100%;
    }

    .nv-savebar__actions > * {
      flex: 1 1 0;
    }
  }

  @media (max-width: 899px) {
    .nv-savebar__actions > .nv-btn {
      min-height: 44px;
    }
  }
}
</style>
