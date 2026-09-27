<script setup>
/**
 * List of title match candidates (from matchCandidates()): small poster, name, year, edition,
 * source and id, type ("DLC", "Not sold any more") and confidence, with a "Use" button each.
 * Shows 10 at a time with "Show more".
 *
 * Props: candidates, busy (disables the buttons), pageSize.
 * Emits: choose(candidate).
 */
import { computed, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import NvArt from '../../components/NvArt.vue'
import NvBadge from '../../components/NvBadge.vue'
import NvButton from '../../components/NvButton.vue'
import { candidateUrl } from './libraryApi'

const props = defineProps({
  candidates: { type: Array, required: true },
  busy: { type: Boolean, default: false },
  pageSize: { type: Number, default: 10 },
})
const emit = defineEmits(['choose'])
const { t } = useI18n()

const shown = shallowRef(props.pageSize)
watch(() => props.candidates, () => { shown.value = props.pageSize })
const visible = computed(() => props.candidates.slice(0, shown.value).map((c) => ({
  ...c,
  percent: Math.round((c.confidence ?? 0) * 100),
  facts: [c.year, c.edition, `${t(`nova.library.meta_source_${c.source}`)} ${c.id}`].filter(Boolean).join(' · '),
  typeLabel: c.type && c.type !== 'game' ? t(`nova.library.meta_type_${c.type}`) : '',
})))
</script>

<template>
  <div class="nv-matches">
    <p class="nv-matches__count">{{ t('nova.library.meta_results', { n: candidates.length }) }}</p>
    <ul class="nv-matches__list">
      <li v-for="c in visible" :key="c.key" class="nv-matches__item" :data-key="c.key">
        <NvArt class="nv-matches__poster" :title="c.name" :src="c.poster ? candidateUrl({ id: c.poster }) : ''" kind="poster"
               :show-title="false" decorative />
        <div class="nv-matches__text">
          <span class="nv-matches__name">{{ c.name }}</span>
          <span class="nv-matches__facts">{{ c.facts }}</span>
          <span class="nv-matches__badges">
            <NvBadge :variant="c.percent >= 85 ? 'success' : c.percent >= 60 ? 'warning' : 'neutral'">
              {{ t('nova.library.meta_confidence', { percent: c.percent }) }}
            </NvBadge>
            <NvBadge v-if="c.typeLabel">{{ c.typeLabel }}</NvBadge>
            <NvBadge v-if="c.unlisted">{{ t('nova.library.meta_unlisted') }}</NvBadge>
          </span>
        </div>
        <NvButton size="sm" :disabled="busy" @click="emit('choose', c)">{{ t('nova.library.meta_use') }}</NvButton>
      </li>
    </ul>
    <NvButton v-if="candidates.length > shown" size="sm" variant="ghost" @click="shown += pageSize">{{ t('nova.library.meta_more') }}</NvButton>
  </div>
</template>

<style>
@layer components {
  .nv-matches {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-2);
  }

  .nv-matches__count {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-matches__list {
    align-self: stretch;
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-raised);
  }

  .nv-matches__item {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    padding: var(--nv-space-2) var(--nv-space-3);
  }

  .nv-matches__item + .nv-matches__item {
    border-top: 1px solid var(--nv-border);
  }

  .nv-matches__poster {
    width: 40px;
    border-radius: var(--nv-radius-sm, 4px);
  }

  .nv-matches__text {
    flex-grow: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nv-matches__name {
    font-weight: 600;
    overflow-wrap: anywhere;
  }

  .nv-matches__facts {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-matches__badges {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-1);
  }
}
</style>
