<script setup>
/**
 * Artwork picker: tabs for Poster (2:3), Hero (banner), Logo and Icon, each a grid of candidate
 * images from the host. "No image" is always offered, so a kind can be left out.
 *
 * v-model: { poster, hero, logo, icon } — the chosen candidate id per kind, or 'none'.
 * Props: artwork ({ poster|hero|logo|icon: [{ id, label, url? }] }), title (game title, for alt
 *        text and placeholders), kinds (tabs to show), loading.
 * Slots: footnote (below the grid, e.g. the SteamGridDB hint).
 */
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Check } from '@lucide/vue'
import NvArt from '../../components/NvArt.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { ART_KINDS, candidateUrl, normalizeArtwork } from './libraryApi'

const model = defineModel({ type: Object, default: () => ({}) })
const props = defineProps({
  artwork: { type: Object, default: () => ({}) },
  title: { type: String, default: '' },
  kinds: { type: Array, default: () => ART_KINDS },
  loading: { type: Boolean, default: false },
})
const { t } = useI18n()

const tab = ref(props.kinds[0])
watch(() => props.kinds, (k) => { if (!k.includes(tab.value)) tab.value = k[0] })

const art = computed(() => normalizeArtwork(props.artwork))
const candidates = computed(() => art.value[tab.value] || [])
const artKind = { poster: 'poster', hero: 'hero', logo: 'thumb', icon: 'icon' }

function pick(id) {
  model.value = { ...model.value, [tab.value]: id }
}

function onTabKey(event, index) {
  const keys = { ArrowRight: 1, ArrowLeft: -1 }
  if (!(event.key in keys)) return
  event.preventDefault()
  const next = props.kinds[(index + keys[event.key] + props.kinds.length) % props.kinds.length]
  tab.value = next
  event.currentTarget.parentElement?.querySelector(`[data-kind="${next}"]`)?.focus()
}
</script>

<template>
  <div class="nv-artpick">
    <div class="nv-artpick__tabs" role="tablist" :aria-label="t('nova.addgames.art_kinds')">
      <button v-for="(k, i) in kinds" :key="k" type="button" role="tab" :data-kind="k"
              :aria-selected="tab === k" :tabindex="tab === k ? 0 : -1"
              :class="['nv-artpick__tab', { 'nv-artpick__tab--on': tab === k }]"
              @click="tab = k" @keydown="onTabKey($event, i)">
        {{ t(`nova.addgames.kind_${k}`) }} <span class="nv-artpick__ratio">{{ t(`nova.addgames.kind_${k}_hint`) }}</span>
      </button>
    </div>

    <div v-if="loading" :class="['nv-artpick__grid', `nv-artpick__grid--${tab}`]" aria-busy="true">
      <span class="nv-visually-hidden" role="status">{{ t('nova.common.loading') }}</span>
      <NvSkeleton v-for="n in 5" :key="n" height="160px" radius="var(--nv-radius-md)" />
    </div>
    <div v-else :class="['nv-artpick__grid', `nv-artpick__grid--${tab}`]" role="radiogroup"
         :aria-label="t(`nova.addgames.kind_${tab}`)">
      <button v-for="c in candidates" :key="c.id" type="button" role="radio" :aria-checked="model[tab] === c.id"
              :class="['nv-artpick__item', { 'nv-artpick__item--on': model[tab] === c.id }]"
              :aria-label="c.label || t(`nova.addgames.kind_${tab}`)" @click="pick(c.id)">
        <NvArt :title="title" :src="candidateUrl(c)" :kind="artKind[tab]" decorative />
        <span v-if="model[tab] === c.id" class="nv-artpick__check" aria-hidden="true"><Check :size="14" /></span>
      </button>
      <button type="button" role="radio" :aria-checked="!model[tab] || model[tab] === 'none'"
              :class="['nv-artpick__item nv-artpick__none', { 'nv-artpick__item--on': !model[tab] || model[tab] === 'none' }]"
              @click="pick('none')">
        {{ t('nova.addgames.no_image') }}
      </button>
    </div>
    <p v-if="!loading && !candidates.length" class="nv-artpick__empty">{{ t('nova.addgames.no_candidates') }}</p>
    <slot name="footnote" />
  </div>
</template>

<style>
@layer components {
  .nv-artpick {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
  }

  .nv-artpick__tabs {
    display: flex;
    gap: var(--nv-space-1);
    border-bottom: 1px solid var(--nv-divider);
    overflow-x: auto;
  }

  .nv-artpick__tab {
    min-height: 40px;
    padding: 0 var(--nv-space-3);
    border: 0;
    border-bottom: 2px solid transparent;
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    font-weight: 500;
    white-space: nowrap;
    cursor: pointer;
  }

  .nv-artpick__tab--on {
    color: var(--nv-text);
    border-bottom-color: var(--nv-accent-text);
  }

  .nv-artpick__ratio {
    color: var(--nv-text-muted);
    font-size: var(--nv-text-xs);
    font-weight: 400;
  }

  .nv-artpick__grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(112px, 1fr));
    gap: var(--nv-space-3);
  }

  .nv-artpick__grid--hero,
  .nv-artpick__grid--logo {
    grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  }

  .nv-artpick__grid--icon {
    grid-template-columns: repeat(auto-fill, minmax(80px, 1fr));
  }

  .nv-artpick__item {
    position: relative;
    padding: 0;
    border: 2px solid transparent;
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-sm);
    cursor: pointer;
    overflow: hidden;
  }

  .nv-artpick__item--on {
    border-color: var(--nv-accent-text);
  }

  .nv-artpick__none {
    min-height: 80px;
    border: 1px dashed var(--nv-border-strong);
  }

  .nv-artpick__none.nv-artpick__item--on {
    border: 2px solid var(--nv-accent-text);
  }

  .nv-artpick__check {
    position: absolute;
    top: var(--nv-space-2);
    right: var(--nv-space-2);
    width: 22px;
    height: 22px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: var(--nv-radius-pill);
    background: var(--nv-accent);
    color: var(--nv-on-accent);
  }

  .nv-artpick__empty {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }
}
</style>
