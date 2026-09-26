<script setup>
/**
 * Editor for `keybindings`: pairs of Windows virtual-key codes (client key → host key),
 * stored as a flat list "[0x10,0xA0,…]". Incomplete or invalid pairs are not saved.
 *
 * v-model: serialized list string.
 */
import { ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ArrowRight, ExternalLink, Plus, Trash2 } from '@lucide/vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvIconButton from '../../nova/components/NvIconButton.vue'
import VirtualKeyCodeSelect from '../VirtualKeyCodeSelect.vue'
import { isValidVirtualKeyCode } from '../virtual_key_codes.js'

const model = defineModel({ type: String, default: '' })
const { t } = useI18n()

let nextKeybindingId = 0

/** One editable pair with a stable key for rendering. */
function createKeybinding(source = '', destination = '') {
  return { id: nextKeybindingId++, source, destination }
}

/** Parse "[0x10,0xA0,…]" into pairs. */
function parseKeybindings(value) {
  const serialized = String(value ?? '').trim()
  const contents = serialized.startsWith('[') && serialized.endsWith(']') ? serialized.slice(1, -1) : serialized
  if (contents.trim() === '') return []
  const values = contents.split(',').map((code) => code.trim())
  const pairs = []
  for (let index = 0; index < values.length; index += 2) pairs.push(createKeybinding(values[index], values[index + 1] ?? ''))
  return pairs
}

/** Serialize complete, valid pairs. */
function serializeKeybindings(pairs) {
  const values = pairs
    .filter((pair) => isValidVirtualKeyCode(pair.source) && isValidVirtualKeyCode(pair.destination))
    .flatMap((pair) => [pair.source.trim(), pair.destination.trim()])
  return `[${values.join(',')}]`
}

const keybindingPairs = ref(parseKeybindings(model.value))

function addKeybinding() {
  keybindingPairs.value.push(createKeybinding())
}

function removeKeybinding(index) {
  keybindingPairs.value.splice(index, 1)
}

watch(keybindingPairs, (pairs) => {
  const serialized = serializeKeybindings(pairs)
  if (serialized !== model.value) model.value = serialized
}, { deep: true })

// Reset / Discard replace the value from outside: rebuild the rows unless it's our own write.
watch(model, (value) => {
  if (value !== serializeKeybindings(keybindingPairs.value)) keybindingPairs.value = parseKeybindings(value)
})
</script>

<template>
  <div class="nv-keymap">
    <a class="nv-keymap__reference" href="https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes"
       target="_blank" rel="noopener noreferrer">
      {{ t('config.keybindings_reference') }}<ExternalLink :size="14" aria-hidden="true" />
    </a>

    <p v-if="keybindingPairs.length === 0" class="nv-keymap__empty">{{ t('config.keybindings_empty') }}</p>

    <div v-else class="keybinding-grid">
      <div class="nv-keymap__row nv-keymap__row--head" aria-hidden="true">
        <span>{{ t('config.keybindings_source') }}</span><span></span>
        <span>{{ t('config.keybindings_destination') }}</span><span></span>
      </div>
      <div v-for="(binding, index) in keybindingPairs" :key="binding.id" class="nv-keymap__row">
        <VirtualKeyCodeSelect v-model="binding.source" class="nv-keymap__source"
                              :label="`${t('config.keybindings_source')} ${index + 1}`" />
        <ArrowRight class="nv-keymap__arrow" :size="18" aria-hidden="true" />
        <VirtualKeyCodeSelect v-model="binding.destination" class="nv-keymap__destination"
                              :label="`${t('config.keybindings_destination')} ${index + 1}`" />
        <NvIconButton :label="`${t('config.keybindings_remove')} ${index + 1}`" @click="removeKeybinding(index)">
          <Trash2 :size="16" />
        </NvIconButton>
      </div>
    </div>

    <NvButton size="sm" class="nv-keymap__add" @click="addKeybinding">
      <Plus :size="16" aria-hidden="true" />{{ t('config.keybindings_add') }}
    </NvButton>
  </div>
</template>

<style>
@layer components {
  .nv-keymap {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-keymap__reference {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    color: var(--nv-accent-text);
    font-size: var(--nv-text-sm);
    font-weight: 500;
  }

  .nv-keymap__empty {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .keybinding-grid {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    width: 100%;
    max-width: 720px;
  }

  .nv-keymap__row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 18px minmax(0, 1fr) var(--nv-control-height);
    align-items: start;
    gap: var(--nv-space-3);
  }

  .nv-keymap__row--head {
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-text-secondary);
  }

  .nv-keymap__arrow {
    margin-top: 11px;
    color: var(--nv-text-muted);
  }

  @media (max-width: 699px) {
    .nv-keymap__row--head {
      display: none;
    }

    .nv-keymap__row {
      grid-template-columns: minmax(0, 1fr) var(--nv-control-height);
      padding-bottom: var(--nv-space-2);
      border-bottom: 1px solid var(--nv-border);
    }

    .nv-keymap__source {
      grid-column: 1 / -1;
    }

    .nv-keymap__arrow {
      display: none;
    }
  }
}
</style>
