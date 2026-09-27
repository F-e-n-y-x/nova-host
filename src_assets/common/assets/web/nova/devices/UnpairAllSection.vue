<script setup>
/**
 * Danger zone at the bottom of Devices: unpair every device, behind a confirmation that needs an
 * explicit checkbox. The parent performs the action and calls `close()` when it succeeds.
 */
import { shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import NvButton from '../components/NvButton.vue'
import NvConfirmDialog from '../components/NvConfirmDialog.vue'

const props = defineProps({
  count: { type: Number, required: true },
  busy: { type: Boolean, default: false },
})
const emit = defineEmits(['confirm'])

const { t } = useI18n()
const open = shallowRef(false)

/** Close the dialog; the parent calls this after unpairing finishes. */
function close() {
  open.value = false
}

defineExpose({ close })
</script>

<template>
  <section class="nv-danger-zone" aria-labelledby="nv-danger-zone-title">
    <div class="nv-danger-zone__text">
      <h2 id="nv-danger-zone-title" class="nv-danger-zone__title">{{ t('nova.devices.unpair_all_button') }}</h2>
      <p class="nv-danger-zone__desc">{{ t('nova.devices.unpair_all_desc') }}</p>
    </div>
    <NvButton variant="danger" @click="open = true">{{ t('nova.devices.unpair_all_ellipsis') }}</NvButton>
  </section>
  <NvConfirmDialog v-model:open="open" :title="t('nova.devices.unpair_all_confirm_title')"
                   :description="t('nova.devices.unpair_all_confirm_desc', { n: props.count }, props.count)"
                   :confirm-label="t('nova.devices.unpair_all_button')"
                   :require-check="t('nova.devices.unpair_all_check', { n: props.count }, props.count)"
                   :loading="busy" @confirm="emit('confirm')" />
</template>

<style scoped>
@layer components {
  .nv-danger-zone {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    padding: var(--nv-space-4) var(--nv-space-5);
    border: 1px solid var(--nv-danger-zone-border);
    border-radius: var(--nv-radius-xl);
    background: var(--nv-danger-zone);
  }

  .nv-danger-zone__text {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-danger-zone__title {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-danger-zone__desc {
    margin: 2px 0 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  @media (max-width: 599px) {
    .nv-danger-zone {
      flex-direction: column;
      align-items: stretch;
    }
  }
}
</style>
