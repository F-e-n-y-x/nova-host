<script setup>
/**
 * Windows driver status list: installed version, latest release and compatibility for the
 * Virtual HID Driver and ViGEmBus.
 *
 * Props: drivers ({id, name, statusVariant, statusText, facts, url}[]), refreshing.
 * Emits: refresh.
 */
import { useI18n } from 'vue-i18n'
import { Download, RefreshCw } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'
import NvBadge from '../../components/NvBadge.vue'
import NvDataList from '../../components/NvDataList.vue'

defineProps({
  drivers: { type: Array, required: true },
  refreshing: { type: Boolean, default: false },
})
defineEmits(['refresh'])

const { t } = useI18n()
</script>

<template>
  <section class="nv-vi__section" aria-labelledby="nv-vi-drivers">
    <div class="nv-vi__section-head">
      <div>
        <h3 id="nv-vi-drivers" class="nv-vi__h3">{{ t('nova.logs.vi.virtual_gamepad_drivers') }}</h3>
        <p class="nv-secondary">{{ t('nova.logs.vi.virtual_gamepad_drivers_desc') }}</p>
      </div>
      <NvButton variant="secondary" :loading="refreshing" @click="$emit('refresh')">
        <RefreshCw :size="16" aria-hidden="true" />{{ t('nova.logs.vi.driver_refresh') }}
      </NvButton>
    </div>
    <ul class="nv-vi__drivers">
      <li v-for="driver in drivers" :key="driver.id" class="nv-vi__item">
        <div class="nv-vi__section-head">
          <p class="nv-vi__item-title">{{ driver.name }}</p>
          <NvBadge :variant="driver.statusVariant">{{ driver.statusText }}</NvBadge>
        </div>
        <NvDataList term-width="152px" :items="driver.facts" />
        <div>
          <NvButton variant="secondary" size="sm" :href="driver.url" target="_blank" rel="noopener noreferrer">
            <Download :size="16" aria-hidden="true" />{{ t('nova.logs.vi.driver_download') }}
          </NvButton>
        </div>
      </li>
    </ul>
  </section>
</template>
