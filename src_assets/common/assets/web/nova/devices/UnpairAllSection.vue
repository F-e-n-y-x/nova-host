<script setup>
/**
 * Danger zone: unpair every device, behind a confirmation that needs an explicit checkbox.
 */
import { shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import NvCard from '../components/NvCard.vue'
import NvButton from '../components/NvButton.vue'
import NvDialog from '../components/NvDialog.vue'

const props = defineProps({
  count: { type: Number, required: true },
  busy: { type: Boolean, default: false },
})
const emit = defineEmits(['confirm'])

const { t } = useI18n()
const open = shallowRef(false)
const understood = shallowRef(false)

watch(open, (isOpen) => {
  if (isOpen) understood.value = false
})

/**
 * Close the dialog; the parent calls this after unpairing finishes.
 */
function close() {
  open.value = false
}

defineExpose({ close })
</script>

<template>
  <NvCard :title="t('nova.devices.danger_title')">
    <div class="nv-danger">
      <div class="nv-grow">
        <p class="nv-danger__title">{{ t('nova.devices.unpair_all') }}</p>
        <p class="nv-danger__desc">{{ t('nova.devices.unpair_all_desc') }}</p>
      </div>
      <NvButton variant="danger" @click="open = true">{{ t('nova.devices.unpair_all_button') }}</NvButton>
    </div>
  </NvCard>

  <NvDialog v-model:open="open" :title="t('nova.devices.unpair_all_confirm_title')"
            :description="t('nova.devices.unpair_all_confirm_desc', { n: props.count }, props.count)">
    <label class="nv-danger__check">
      <input v-model="understood" type="checkbox">
      <span>{{ t('nova.devices.unpair_all_check', { n: props.count }, props.count) }}</span>
    </label>
    <template #footer>
      <NvButton variant="secondary" @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton variant="danger" :disabled="!understood" :loading="busy" @click="emit('confirm')">
        {{ t('nova.devices.unpair_all_button') }}
      </NvButton>
    </template>
  </NvDialog>
</template>

<style scoped>
@layer components {
  .nv-danger {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
  }

  .nv-danger__title {
    margin: 0;
    font-weight: 500;
  }

  .nv-danger__desc {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-danger__check {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
    min-height: 44px;
    cursor: pointer;
  }

  .nv-danger__check input {
    width: 20px;
    height: 20px;
    margin: 2px 0 0;
    accent-color: var(--nv-danger-fill);
  }

  @media (max-width: 599px) {
    .nv-danger {
      flex-direction: column;
      align-items: stretch;
    }
  }
}
</style>
