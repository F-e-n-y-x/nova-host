<script setup>
/**
 * Editable list of background (detached) commands.
 *
 * v-model: array of strings. Every change emits a new array.
 * Emits: browse(index).
 */
import { useI18n } from 'vue-i18n'
import { Plus, Trash2 } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'
import NvIconButton from '../../components/NvIconButton.vue'
import PathField from './PathField.vue'

const model = defineModel({ type: Array, default: () => [] })
defineEmits(['browse'])
const { t } = useI18n()

function update(index, value) {
  model.value = model.value.map((c, i) => (i === index ? value : c))
}

function remove(index) {
  model.value = model.value.filter((_, i) => i !== index)
}
</script>

<template>
  <ol v-if="model.length" class="nv-cmd-list">
    <li v-for="(c, i) in model" :key="i" class="nv-detached">
      <PathField :model-value="c" :label="t('nova.apps.field_detached', { n: i + 1 })" class="nv-detached__field"
                 @update:model-value="update(i, $event)" @browse="$emit('browse', i)" />
      <NvIconButton :label="t('nova.apps.remove_detached', { n: i + 1 })" class="nv-detached__remove" @click="remove(i)">
        <Trash2 :size="18" aria-hidden="true" />
      </NvIconButton>
    </li>
  </ol>
  <div><NvButton size="sm" @click="model = [...model, '']"><Plus :size="16" aria-hidden="true" />{{ t('nova.apps.add_detached') }}</NvButton></div>
</template>

<style>
@layer components {
  .nv-detached {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-2);
  }

  .nv-detached__field {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-detached__remove {
    margin-top: calc(var(--nv-text-md) * var(--nv-leading) + var(--nv-space-1));
  }
}
</style>
