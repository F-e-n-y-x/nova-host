<script setup>
/**
 * One application: a cover tile (grid) or a single-line row (list). Pressing the cover
 * or the name opens the editor, as in game library apps; delete sits beside it.
 *
 * Props: app (required), coverUrl ('' shows the initial letter), layout ('grid' | 'list').
 * Emits: edit, delete, cover-error.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Pencil, Trash2 } from '@lucide/vue'
import NvIconButton from '../../components/NvIconButton.vue'
import NvBadge from '../../components/NvBadge.vue'
import { appFlags } from './appForm'

const props = defineProps({
  app: { type: Object, required: true },
  coverUrl: { type: String, default: '' },
  layout: { type: String, default: 'grid' },
})
defineEmits(['edit', 'delete', 'cover-error'])
const { t } = useI18n()

const name = computed(() => props.app.name || t('nova.apps.unnamed'))
const initial = computed(() => (props.app.name || '?').charAt(0).toUpperCase())
const flags = computed(() => appFlags(props.app))
const command = computed(() => props.app.cmd || t('nova.apps.no_command'))
</script>

<template>
  <li :class="['nv-app', `nv-app--${layout}`]">
    <button type="button" class="nv-app__open" :aria-label="t('nova.apps.edit_named', { name })" @click="$emit('edit')">
      <span class="nv-app__cover">
        <img v-if="coverUrl" :src="coverUrl" alt="" loading="lazy" class="nv-app__img" @error="$emit('cover-error')" />
        <span v-else class="nv-app__initial" aria-hidden="true">{{ initial }}</span>
        <span v-if="layout === 'grid'" class="nv-app__hint" aria-hidden="true"><Pencil :size="14" />{{ t('nova.apps.edit') }}</span>
      </span>
      <span class="nv-app__text">
        <span class="nv-app__name" :title="name">{{ name }}</span>
        <span v-if="layout === 'list'" class="nv-app__cmd" :title="app.cmd">{{ command }}</span>
      </span>
    </button>
    <div class="nv-app__meta">
      <div v-if="flags.length" class="nv-app__flags">
        <NvBadge v-for="flag in flags" :key="flag">{{ t(flag) }}</NvBadge>
      </div>
      <NvIconButton size="sm" class="nv-app__delete" :label="t('nova.apps.delete_named', { name })" @click="$emit('delete')">
        <Trash2 :size="16" aria-hidden="true" />
      </NvIconButton>
    </div>
  </li>
</template>

<style>
@layer components {
  .nv-app {
    display: flex;
    min-width: 0;
  }

  .nv-app__open {
    display: flex;
    min-width: 0;
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }

  .nv-app__cover {
    position: relative;
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    overflow: hidden;
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
    font-weight: 600;
  }

  .nv-app__img {
    width: 100%;
    height: 100%;
    object-fit: cover;
  }

  .nv-app__text {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .nv-app__name {
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-app__cmd {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-app__meta {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-width: 0;
  }

  .nv-app__flags {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-1);
    min-width: 0;
  }

  /* Grid: cover tile, name under it, badges and delete in the last row. */
  .nv-app--grid {
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-app--grid .nv-app__open {
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-app--grid .nv-app__cover {
    width: 100%;
    aspect-ratio: 3 / 4;
    font-size: 44px;
  }

  .nv-app--grid .nv-app__meta {
    justify-content: space-between;
    align-items: flex-start;
  }

  .nv-app--grid .nv-app__delete {
    margin-left: auto;
    flex-shrink: 0;
  }

  .nv-app__hint {
    position: absolute;
    left: var(--nv-space-2);
    bottom: var(--nv-space-2);
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    padding: 4px 10px;
    border-radius: 999px;
    background: var(--nv-surface);
    border: 1px solid var(--nv-border-strong);
    color: var(--nv-text);
    font-size: var(--nv-text-xs);
    font-weight: 500;
    opacity: 0;
  }

  .nv-app--grid .nv-app__open:hover .nv-app__cover,
  .nv-app--grid .nv-app__open:focus-visible .nv-app__cover {
    border-color: var(--nv-accent);
    box-shadow: 0 0 0 1px var(--nv-accent);
  }

  .nv-app__open:hover .nv-app__hint,
  .nv-app__open:focus-visible .nv-app__hint {
    opacity: 1;
  }

  .nv-app--grid .nv-app__open:focus-visible {
    outline: none;
  }

  .nv-app--grid .nv-app__open:focus-visible .nv-app__cover {
    outline: var(--nv-focus-width) solid var(--nv-focus);
    outline-offset: 2px;
  }

  /* List: thumbnail, name and command, badges, delete — one line. */
  .nv-app--list {
    align-items: center;
    gap: var(--nv-space-3);
    padding: var(--nv-space-2) var(--nv-space-3) var(--nv-space-2) var(--nv-space-2);
  }

  .nv-app--list + .nv-app--list {
    border-top: 1px solid var(--nv-border);
  }

  .nv-app--list .nv-app__open {
    flex-grow: 1;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 56px;
    padding: var(--nv-space-1);
    border-radius: var(--nv-radius-md);
  }

  .nv-app--list .nv-app__open:hover {
    background: var(--nv-raised);
  }

  .nv-app--list .nv-app__cover {
    width: 36px;
    height: 48px;
    font-size: var(--nv-text-lg);
  }

  .nv-app--list .nv-app__meta {
    flex-shrink: 0;
  }

  /* With a mouse, delete appears on hover or keyboard focus; touch screens always show it. */
  @media (hover: hover) {
    .nv-app__delete {
      opacity: 0;
    }

    .nv-app:hover .nv-app__delete,
    .nv-app:focus-within .nv-app__delete {
      opacity: 1;
    }
  }

  @media (max-width: 600px) {
    .nv-app--list .nv-app__flags {
      display: none;
    }
  }

  @media (prefers-reduced-motion: no-preference) {
    .nv-app__hint,
    .nv-app__delete,
    .nv-app__cover {
      transition: opacity 120ms ease, border-color 120ms ease, box-shadow 120ms ease;
    }
  }
}
</style>
