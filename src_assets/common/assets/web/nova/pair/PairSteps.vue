<script setup>
/**
 * How pairing works, naming this host when its name is known.
 */
import { useI18n } from 'vue-i18n'
import NvCard from '../components/NvCard.vue'
import NvAlert from '../components/NvAlert.vue'

defineProps({
  hostName: { type: String, default: '' },
})

const { t } = useI18n()
</script>

<template>
  <NvCard :title="t('nova.pair.steps_title')">
    <ol class="nv-pair-steps">
      <li>{{ hostName ? t('nova.pair.step1_named', { host: hostName }) : t('nova.pair.step1') }}</li>
      <li>{{ t('nova.pair.step2') }}</li>
      <li>{{ t('nova.pair.step3') }}</li>
    </ol>
    <NvAlert variant="warning" :title="t('nova.pair.warning_title')">{{ t('nova.pair.warning_desc') }}</NvAlert>
  </NvCard>
</template>

<style scoped>
@layer components {
  .nv-pair-steps {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    margin: 0 0 var(--nv-space-5);
    padding-left: var(--nv-space-5);
  }
}
</style>
