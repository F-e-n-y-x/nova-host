<script setup>
/**
 * Editor for `art_source_priority`: every artwork source with an on/off switch, the ones
 * in use first in priority order, with buttons to move them up and down.
 *
 * v-model: comma-separated source list ("steam,steamgriddb,lutris,igdb").
 * Props: config (working copy; sources that need a key say so while it is missing).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { ArrowDown, ArrowUp } from '@lucide/vue'
import NvIconButton from '../../nova/components/NvIconButton.vue'
import NvSwitch from '../../nova/components/NvSwitch.vue'
import { ART_SOURCES, parseArtSources } from '../settings_schema.js'

const model = defineModel({ type: String, default: '' })
const props = defineProps({
  config: { type: Object, default: () => ({}) },
})
const { t } = useI18n()

const enabled = computed(() => parseArtSources(model.value))
/** Sources in use (priority order), then the rest (canonical order). */
const rows = computed(() => {
  const on = enabled.value
  const off = ART_SOURCES.filter((s) => !on.includes(s))
  return [...on, ...off].map((id) => ({
    id,
    on: on.includes(id),
    rank: on.indexOf(id),
    name: t(`nova.settings.art_source_${id}`),
    needsKey: needsKey(id),
  }))
})

function needsKey(source) {
  if (source === 'steamgriddb') return !props.config.steamgriddb_api_key
  if (source === 'igdb') return !props.config.igdb_client_id || !props.config.igdb_client_secret
  return false
}

function write(list) {
  model.value = list.join(',')
}

function toggle(source, on) {
  const list = enabled.value.filter((s) => s !== source)
  write(on ? [...list, source] : list)
}

function move(source, delta) {
  const list = [...enabled.value]
  const from = list.indexOf(source)
  const to = from + delta
  if (from < 0 || to < 0 || to >= list.length) return
  list.splice(to, 0, list.splice(from, 1)[0])
  write(list)
}
</script>

<template>
  <div class="nv-art-sources">
    <p v-if="enabled.length === 0" class="nv-art-sources__empty">{{ t('nova.settings.art_sources_none') }}</p>
    <ol class="nv-art-sources__list">
      <li v-for="row in rows" :key="row.id" :class="['nv-art-sources__item', { 'nv-art-sources__item--off': !row.on }]"
          :data-source="row.id">
        <span class="nv-art-sources__rank" aria-hidden="true">{{ row.on ? row.rank + 1 : '–' }}</span>
        <span class="nv-art-sources__name">
          {{ row.name }}
          <span v-if="row.on && row.needsKey" class="nv-art-sources__note">{{ t('nova.settings.art_source_needs_key') }}</span>
        </span>
        <NvIconButton size="sm" :label="t('nova.settings.art_sources_move_up', { source: row.name })"
                      :disabled="!row.on || row.rank === 0" @click="move(row.id, -1)">
          <ArrowUp :size="16" />
        </NvIconButton>
        <NvIconButton size="sm" :label="t('nova.settings.art_sources_move_down', { source: row.name })"
                      :disabled="!row.on || row.rank === enabled.length - 1" @click="move(row.id, 1)">
          <ArrowDown :size="16" />
        </NvIconButton>
        <NvSwitch :model-value="row.on" :label="t('nova.settings.art_sources_use', { source: row.name })"
                  @update:model-value="(v) => toggle(row.id, v)" />
      </li>
    </ol>
  </div>
</template>

<style>
@layer components {
  .nv-art-sources {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-art-sources__empty {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-warning, var(--nv-text-secondary));
  }

  .nv-art-sources__list {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
  }

  .nv-art-sources__item {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: 48px;
    padding: 0 var(--nv-space-3);
  }

  .nv-art-sources__item + .nv-art-sources__item {
    border-top: 1px solid var(--nv-border);
  }

  .nv-art-sources__item--off .nv-art-sources__name {
    color: var(--nv-text-secondary);
  }

  .nv-art-sources__rank {
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: 24px;
    height: 24px;
    border-radius: 12px;
    background: var(--nv-surface);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-art-sources__name {
    flex-grow: 1;
    min-width: 0;
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: var(--nv-space-2);
    font-weight: 500;
  }

  .nv-art-sources__note {
    font-size: var(--nv-text-xs);
    font-weight: 400;
    color: var(--nv-text-secondary);
  }
}
</style>
