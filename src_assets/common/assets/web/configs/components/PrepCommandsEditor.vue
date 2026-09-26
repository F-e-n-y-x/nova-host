<script setup>
/**
 * Editor for `global_prep_cmd`: commands run before an app starts ("do") and after it
 * ends ("undo"); on Windows each can run elevated.
 *
 * v-model: array of { do, undo, elevated? }.
 * Props: platform.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Plus, Trash2 } from '@lucide/vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvIconButton from '../../nova/components/NvIconButton.vue'
import NvSwitch from '../../nova/components/NvSwitch.vue'
import NvTextField from '../../nova/components/NvTextField.vue'

const model = defineModel({ type: Array, default: () => [] })
const props = defineProps({
  platform: { type: String, required: true },
})
const { t } = useI18n()

const windows = computed(() => props.platform === 'windows')
const commands = computed(() => (Array.isArray(model.value) ? model.value : []))

function update(index, field, value) {
  model.value = commands.value.map((c, i) => (i === index ? { ...c, [field]: value } : c))
}

function add() {
  const template = windows.value ? { do: '', undo: '', elevated: false } : { do: '', undo: '' }
  model.value = [...commands.value, template]
}

function remove(index) {
  model.value = commands.value.filter((_, i) => i !== index)
}
</script>

<template>
  <div class="nv-prep">
    <p v-if="commands.length === 0" class="nv-prep__empty">{{ t('nova.settings.prep_empty') }}</p>
    <ol v-else class="nv-prep__list">
      <li v-for="(command, index) in commands" :key="index" class="nv-prep__item">
        <span class="nv-prep__index" aria-hidden="true">{{ index + 1 }}</span>
        <div class="nv-prep__fields">
          <NvTextField :model-value="command.do ?? ''" :label="t('_common.do_cmd')" mono
                       :placeholder="t('nova.settings.prep_do_placeholder')"
                       @update:model-value="(v) => update(index, 'do', v)" />
          <NvTextField :model-value="command.undo ?? ''" :label="t('_common.undo_cmd')" mono
                       :placeholder="t('nova.settings.prep_undo_placeholder')"
                       @update:model-value="(v) => update(index, 'undo', v)" />
          <label v-if="windows" class="nv-prep__elevated">
            <NvSwitch :model-value="!!command.elevated" :label="`${t('_common.run_as')} ${index + 1}`"
                      @update:model-value="(v) => update(index, 'elevated', v)" />
            <span aria-hidden="true">{{ t('_common.run_as') }}</span>
          </label>
        </div>
        <NvIconButton :label="`${t('nova.settings.prep_remove')} ${index + 1}`" @click="remove(index)">
          <Trash2 :size="16" />
        </NvIconButton>
      </li>
    </ol>
    <NvButton size="sm" @click="add"><Plus :size="16" aria-hidden="true" />{{ t('nova.settings.prep_add') }}</NvButton>
  </div>
</template>

<style>
@layer components {
  .nv-prep {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-prep__empty {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-prep__list {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    width: 100%;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-prep__item {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
  }

  .nv-prep__index {
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: 24px;
    height: 24px;
    margin-top: 30px;
    border-radius: 12px;
    background: var(--nv-surface);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-prep__fields {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: var(--nv-space-3);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-prep__elevated {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    font-size: var(--nv-text-sm);
  }

  .nv-prep__item > .nv-icon-btn,
  .nv-prep__item > button {
    margin-top: 26px;
  }

  @media (max-width: 699px) {
    .nv-prep__fields {
      grid-template-columns: minmax(0, 1fr);
    }

    .nv-prep__index {
      display: none;
    }
  }
}
</style>
