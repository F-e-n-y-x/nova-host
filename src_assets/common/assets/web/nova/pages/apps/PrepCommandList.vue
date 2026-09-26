<script setup>
/**
 * Editable list of prep commands: each row has a "before" and an "after" command, plus
 * a run-as-administrator switch on Windows.
 *
 * v-model: array of { do, undo, elevated? }. Every change emits a new array.
 * Props: platform (host platform).
 * Emits: browse(index, field) with field 'do' | 'undo'.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Plus, Trash2 } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'
import NvSwitch from '../../components/NvSwitch.vue'
import PathField from './PathField.vue'
import { newPrepCmd } from './appForm'

const model = defineModel({ type: Array, default: () => [] })
const props = defineProps({
  platform: { type: String, default: '' },
})
defineEmits(['browse'])
const { t } = useI18n()

const isWindows = computed(() => props.platform === 'windows')

function update(index, key, value) {
  model.value = model.value.map((c, i) => (i === index ? { ...c, [key]: value } : c))
}

function remove(index) {
  model.value = model.value.filter((_, i) => i !== index)
}

function add() {
  model.value = [...model.value, newPrepCmd(props.platform)]
}
</script>

<template>
  <ol v-if="model.length" class="nv-cmd-list">
    <li v-for="(c, i) in model" :key="i" class="nv-prep">
      <PathField :model-value="c.do" :label="t('nova.apps.field_prep_do', { n: i + 1 })"
                 @update:model-value="update(i, 'do', $event)" @browse="$emit('browse', i, 'do')" />
      <PathField :model-value="c.undo" :label="t('nova.apps.field_prep_undo', { n: i + 1 })"
                 @update:model-value="update(i, 'undo', $event)" @browse="$emit('browse', i, 'undo')" />
      <div class="nv-prep__actions">
        <template v-if="isWindows">
          <NvSwitch :model-value="!!c.elevated" :label="t('nova.apps.field_prep_elevated', { n: i + 1 })" show-state
                    @update:model-value="update(i, 'elevated', $event)" />
          <span class="nv-prep__hint">{{ t('nova.apps.run_as_admin') }}</span>
        </template>
        <NvButton size="sm" variant="ghost" class="nv-prep__remove" @click="remove(i)">
          <Trash2 :size="16" aria-hidden="true" />{{ t('nova.apps.remove_prep', { n: i + 1 }) }}
        </NvButton>
      </div>
    </li>
  </ol>
  <div><NvButton size="sm" @click="add"><Plus :size="16" aria-hidden="true" />{{ t('nova.apps.add_prep') }}</NvButton></div>
</template>

<style>
@layer components {
  .nv-cmd-list {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-prep {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding: var(--nv-space-4);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
  }

  .nv-prep__actions {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-3);
  }

  .nv-prep__hint {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-prep__remove {
    margin-left: auto;
  }
}
</style>
