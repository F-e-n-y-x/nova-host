<script setup>
/**
 * One Settings section: heading and summary, then a card of settings. The Encoder
 * section also lists per-encoder settings — the encoder in use first, the rest behind
 * "Other encoders".
 *
 * Props: section ({ id, title, summary, items, primaryGroups, otherGroups }) where items are
 *        [{ kind: 'field', key } | { kind: 'heading', title }], groups are
 *        [{ id, title, detected, items }]; config, platform, errors, otherOpen.
 * Emits: update(key, value), reset(key), touch(key).
 */
import { shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronDown } from '@lucide/vue'
import NvBadge from '../../nova/components/NvBadge.vue'
import SettingField from './SettingField.vue'

const props = defineProps({
  section: { type: Object, required: true },
  config: { type: Object, required: true },
  platform: { type: String, required: true },
  errors: { type: Object, default: () => ({}) },
  otherOpen: { type: Boolean, default: false },
})
const emit = defineEmits(['update', 'reset', 'touch'])
const { t } = useI18n()

const showOther = shallowRef(props.otherOpen)
watch(() => props.otherOpen, (open) => { if (open) showOther.value = true })
</script>

<template>
  <section :id="section.id" class="nv-settings-section" :aria-labelledby="`${section.id}-title`" tabindex="-1">
    <div class="nv-settings-section__head">
      <h2 :id="`${section.id}-title`" class="nv-settings-section__title">{{ section.title }}</h2>
      <p v-if="section.summary" class="nv-settings-section__summary">{{ section.summary }}</p>
    </div>

    <div v-if="section.items.length" class="nv-settings-card">
      <template v-for="item in section.items" :key="item.kind === 'field' ? item.key : `h-${item.title}`">
        <h3 v-if="item.kind === 'heading'" class="nv-settings-card__subhead">{{ item.title }}</h3>
        <SettingField v-else :option-key="item.key" :value="config[item.key]" :config="config" :platform="platform"
                      :error="errors[item.key] || null" @update="(k, v) => emit('update', k, v)"
                      @reset="(k) => emit('reset', k)" @touch="(k) => emit('touch', k)" />
      </template>
    </div>

    <div v-for="group in section.primaryGroups || []" :key="group.id" class="nv-settings-group">
      <div class="nv-settings-group__head">
        <h3 class="nv-settings-group__title">{{ group.title }}</h3>
        <NvBadge v-if="group.detected" variant="success">{{ t('nova.settings.in_use') }}</NvBadge>
      </div>
      <div class="nv-settings-card">
        <SettingField v-for="item in group.items" :key="item.key" :option-key="item.key" :value="config[item.key]"
                      :config="config" :platform="platform" :error="errors[item.key] || null"
                      @update="(k, v) => emit('update', k, v)" @reset="(k) => emit('reset', k)" @touch="(k) => emit('touch', k)" />
      </div>
    </div>

    <div v-if="section.otherGroups?.length" class="nv-settings-other">
      <button type="button" class="nv-settings-other__toggle" :aria-expanded="showOther ? 'true' : 'false'"
              :aria-controls="`${section.id}-other`" @click="showOther = !showOther">
        <ChevronDown :size="16" aria-hidden="true" :class="{ 'nv-settings-other__chevron--open': showOther }" />
        <span>{{ t('nova.settings.other_encoders') }}</span>
        <span class="nv-settings-other__names">{{ section.otherGroups.map((g) => g.shortTitle).join(' · ') }}</span>
      </button>
      <div v-show="showOther" :id="`${section.id}-other`" class="nv-settings-other__body">
        <div v-for="group in section.otherGroups" :key="group.id" class="nv-settings-group">
          <h3 class="nv-settings-group__title">{{ group.title }}</h3>
          <div class="nv-settings-card">
            <SettingField v-for="item in group.items" :key="item.key" :option-key="item.key" :value="config[item.key]"
                          :config="config" :platform="platform" :error="errors[item.key] || null"
                          @update="(k, v) => emit('update', k, v)" @reset="(k) => emit('reset', k)" @touch="(k) => emit('touch', k)" />
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<style>
@layer components {
  .nv-settings-section {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    scroll-margin-top: var(--nv-space-6);
  }

  .nv-settings-section:focus {
    outline: none;
  }

  .nv-settings-section__head {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nv-settings-section__title {
    margin: 0;
    font-size: var(--nv-text-lg);
    font-weight: 600;
  }

  .nv-settings-section__summary {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-settings-card {
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
    border: 1px solid var(--nv-border);
  }

  .nv-settings-card__subhead {
    margin: 0;
    padding: var(--nv-space-4) var(--nv-space-5) var(--nv-space-2);
    border-bottom: 1px solid var(--nv-border);
    font-size: var(--nv-text-sm);
    font-weight: 600;
    color: var(--nv-text-secondary);
  }

  .nv-settings-group {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    margin-top: var(--nv-space-2);
  }

  .nv-settings-group__head {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-settings-group__title {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-settings-other {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    margin-top: var(--nv-space-2);
  }

  .nv-settings-other__toggle {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: var(--nv-control-height);
    padding: 0 var(--nv-space-4);
    border: 1px dashed var(--nv-border-strong);
    border-radius: var(--nv-radius-lg);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-weight: 500;
    text-align: left;
    cursor: pointer;
  }

  .nv-settings-other__toggle:hover {
    background: var(--nv-raised);
  }

  .nv-settings-other__chevron--open {
    transform: rotate(180deg);
  }

  .nv-settings-other__names {
    margin-left: auto;
    font-size: var(--nv-text-sm);
    font-weight: 400;
    color: var(--nv-text-secondary);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-settings-other__body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  @media (max-width: 699px) {
    .nv-settings-card__subhead {
      padding: var(--nv-space-3) var(--nv-space-4) var(--nv-space-2);
    }

    .nv-settings-other__names {
      display: none;
    }
  }
}
</style>
