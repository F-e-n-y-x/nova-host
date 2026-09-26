<script setup>
/**
 * The paired-device list, with a search box once there are enough devices to need one.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvTextField from '../components/NvTextField.vue'
import NvButton from '../components/NvButton.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import DeviceRow from './DeviceRow.vue'
import { filterDevices } from './format'

/** Show the search box from this many devices on. */
const SEARCH_FROM = 8

const props = defineProps({
  devices: { type: Array, required: true },
  busy: { type: Object, default: () => ({}) },
})
const query = defineModel('query', { type: String, default: '' })
const emit = defineEmits(['open', 'toggle'])

const { t } = useI18n()
const showSearch = computed(() => props.devices.length >= SEARCH_FROM || query.value !== '')
const visible = computed(() => filterDevices(props.devices, query.value))
const resultText = computed(() => (query.value
  ? t('nova.devices.search_results', { n: visible.value.length, total: props.devices.length }, visible.value.length)
  : ''))
</script>

<template>
  <div class="nv-device-list">
    <div v-if="showSearch" class="nv-device-list__toolbar" role="search">
      <NvTextField v-model="query" type="search" :label="t('nova.devices.search_label')" hide-label
                   :placeholder="t('nova.devices.search_placeholder')" autocomplete="off" />
      <p class="nv-visually-hidden" role="status">{{ resultText }}</p>
    </div>

    <NvEmptyState v-if="!visible.length" compact :title="t('nova.devices.no_matches', { q: query })">
      <template #actions><NvButton size="sm" variant="secondary" @click="query = ''">{{ t('nova.devices.clear_search') }}</NvButton></template>
    </NvEmptyState>
    <ul v-else class="nv-device-list__items">
      <DeviceRow v-for="device in visible" :key="device.uuid" :device="device" :busy="!!busy[device.uuid]"
                 @open="(uuid) => emit('open', uuid)" @toggle="(uuid, value) => emit('toggle', uuid, value)" />
    </ul>
  </div>
</template>

<style scoped>
@layer components {
  .nv-device-list__toolbar {
    max-width: 360px;
    margin-bottom: var(--nv-space-3);
  }

  .nv-device-list__items {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
  }
}
</style>
