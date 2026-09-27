<script setup>
/**
 * "How it works" card for the Pair page: three numbered steps naming this host, a note about
 * trust, and the address to type in when the host isn't discovered automatically.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'

const props = defineProps({
  hostName: { type: String, default: '' },
})

const { t } = useI18n()
const host = computed(() => props.hostName || t('nova.pair.this_pc'))
const address = computed(() => (typeof window !== 'undefined' ? window.location.hostname : ''))
const steps = computed(() => [
  { title: t('nova.pair.how_step1_title'), desc: t('nova.pair.how_step1_desc') },
  { title: t('nova.pair.how_step2_title', { host: host.value }), desc: t('nova.pair.how_step2_desc') },
  { title: t('nova.pair.how_step3_title'), desc: t('nova.pair.how_step3_desc') },
])
</script>

<template>
  <section class="nv-how" aria-labelledby="nv-how-title">
    <h2 id="nv-how-title" class="nv-how__title">{{ t('nova.pair.how_title') }}</h2>
    <ol class="nv-how__steps">
      <li v-for="(step, i) in steps" :key="i" class="nv-how__step">
        <span class="nv-how__num" aria-hidden="true">{{ i + 1 }}</span>
        <span class="nv-how__text">
          <span class="nv-how__step-title">{{ step.title }}</span>
          <span class="nv-how__step-desc">{{ step.desc }}</span>
        </span>
      </li>
    </ol>
    <p class="nv-how__note">{{ t('nova.pair.warning_desc') }}</p>
    <p v-if="address" class="nv-how__foot">
      {{ t('nova.pair.not_listed', { host }) }} <code class="nv-mono">{{ address }}</code>
    </p>
  </section>
</template>

<style scoped>
@layer components {
  .nv-how {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    padding: var(--nv-space-6);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-xl);
    background: var(--nv-surface);
  }

  .nv-how__title {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-how__steps {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-how__step {
    display: flex;
    gap: var(--nv-space-3);
  }

  .nv-how__num {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: 24px;
    height: 24px;
    border-radius: var(--nv-radius-pill);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-xs);
    font-variant-numeric: tabular-nums;
  }

  .nv-how__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }

  .nv-how__step-title {
    font-weight: 500;
  }

  .nv-how__step-desc,
  .nv-how__note {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-how__note {
    margin: 0;
  }

  .nv-how__foot {
    margin: 0;
    padding-top: var(--nv-space-4);
    border-top: 1px solid var(--nv-divider);
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
    overflow-wrap: anywhere;
  }

  .nv-how__foot code {
    color: var(--nv-text);
  }
}
</style>
