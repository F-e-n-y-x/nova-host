<script setup>
/**
 * Editor for `dd_mode_remapping` (Windows display device): rewrite the resolution and/or
 * refresh rate a client asks for. Which list is edited depends on which of resolution /
 * refresh rate is set to "auto".
 *
 * v-model: { mixed: [], resolution_only: [], refresh_rate_only: [] }.
 * Props: config (working copy).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Plus, Trash2 } from '@lucide/vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvIconButton from '../../nova/components/NvIconButton.vue'
import NvTextField from '../../nova/components/NvTextField.vue'

const model = defineModel({ type: Object, default: () => ({ mixed: [], resolution_only: [], refresh_rate_only: [] }) })
const props = defineProps({
  config: { type: Object, required: true },
})
const { t } = useI18n()

const MIXED = 'mixed'
const RESOLUTION_ONLY = 'resolution_only'
const REFRESH_RATE_ONLY = 'refresh_rate_only'

const type = computed(() => {
  if (props.config.dd_resolution_option !== 'auto') return REFRESH_RATE_ONLY
  if (props.config.dd_refresh_rate_option !== 'auto') return RESOLUTION_ONLY
  return MIXED
})

const columns = computed(() => {
  const cols = []
  if (type.value !== REFRESH_RATE_ONLY) cols.push({ field: 'requested_resolution', label: 'config.dd_mode_remapping_requested_resolution', placeholder: '1920x1080' })
  if (type.value !== RESOLUTION_ONLY) cols.push({ field: 'requested_fps', label: 'config.dd_mode_remapping_requested_fps', placeholder: '60' })
  if (type.value !== REFRESH_RATE_ONLY) cols.push({ field: 'final_resolution', label: 'config.dd_mode_remapping_final_resolution', placeholder: '2560x1440' })
  if (type.value !== RESOLUTION_ONLY) cols.push({ field: 'final_refresh_rate', label: 'config.dd_mode_remapping_final_refresh_rate', placeholder: '119.95' })
  return cols
})

const rows = computed(() => model.value?.[type.value] ?? [])

const help = computed(() => [
  'config.dd_mode_remapping_desc_1',
  'config.dd_mode_remapping_desc_2',
  'config.dd_mode_remapping_desc_3',
  type.value === MIXED ? 'config.dd_mode_remapping_desc_4_final_values_mixed' : 'config.dd_mode_remapping_desc_4_final_values_non_mixed',
  ...(type.value === MIXED ? ['config.dd_mode_remapping_desc_5_sops_mixed_only'] : []),
  ...(type.value === RESOLUTION_ONLY ? ['config.dd_mode_remapping_desc_5_sops_resolution_only'] : []),
].map((key) => t(key)))

function write(list) {
  model.value = { ...model.value, [type.value]: list }
}

function update(index, field, value) {
  write(rows.value.map((row, i) => (i === index ? { ...row, [field]: value } : row)))
}

function add() {
  write([...rows.value, Object.fromEntries(columns.value.map((c) => [c.field, '']))])
}

function remove(index) {
  write(rows.value.filter((_, i) => i !== index))
}
</script>

<template>
  <div class="nv-remap">
    <ul class="nv-remap__help">
      <li v-for="(line, i) in help" :key="i">{{ line }}</li>
    </ul>
    <ol v-if="rows.length" class="nv-remap__list">
      <li v-for="(row, index) in rows" :key="index" class="nv-remap__row"
          :style="{ '--nv-remap-cols': columns.length }">
        <NvTextField v-for="col in columns" :key="col.field" :model-value="row[col.field] ?? ''" :label="t(col.label)" mono
                     :placeholder="col.placeholder" @update:model-value="(v) => update(index, col.field, v)" />
        <NvIconButton :label="`${t('nova.settings.remap_remove')} ${index + 1}`" @click="remove(index)">
          <Trash2 :size="16" />
        </NvIconButton>
      </li>
    </ol>
    <NvButton size="sm" @click="add"><Plus :size="16" aria-hidden="true" />{{ t('config.dd_mode_remapping_add') }}</NvButton>
  </div>
</template>

<style>
@layer components {
  .nv-remap {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-remap__help {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    margin: 0;
    padding-left: var(--nv-space-5);
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-remap__list {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    width: 100%;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-remap__row {
    display: grid;
    grid-template-columns: repeat(var(--nv-remap-cols), minmax(0, 1fr)) var(--nv-control-height);
    align-items: end;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
  }

  @media (max-width: 699px) {
    .nv-remap__row {
      grid-template-columns: minmax(0, 1fr);
    }
  }
}
</style>
