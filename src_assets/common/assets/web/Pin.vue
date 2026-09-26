<script setup>
/**
 * Pair a device page: how-to steps next to the approval form. Pairing requests are
 * polled every 2 seconds while the page is visible.
 */
import { onMounted, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import NvPage from './nova/components/NvPage.vue'
import NvCard from './nova/components/NvCard.vue'
import NvButton from './nova/components/NvButton.vue'
import PairSteps from './nova/pair/PairSteps.vue'
import PairForm from './nova/pair/PairForm.vue'
import { getConfig } from './nova/api'
import { usePendingPairings } from './nova/devices/usePendingPairings'

const { t } = useI18n()
const hostName = shallowRef('')
const { requests, loaded, failed, refresh } = usePendingPairings({ interval: 2000 })

onMounted(async () => {
  try {
    hostName.value = (await getConfig()).sunshine_name || ''
  } catch {
    hostName.value = ''
  }
})
</script>

<template>
  <NvPage :title="t('nova.pair.title')">
    <template #subtitle><span>{{ t('nova.pair.intro') }}</span></template>
    <template #actions><NvButton variant="secondary" to="/devices">{{ t('nova.pair.view_devices') }}</NvButton></template>

    <div class="nv-pair">
      <PairSteps :host-name="hostName" />
      <NvCard :title="t('nova.pair.form_title')">
        <PairForm :requests="requests" :loaded="loaded" :failed="failed" @changed="refresh" @retry="refresh" />
      </NvCard>
    </div>
  </NvPage>
</template>

<style scoped>
@layer components {
  .nv-pair {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: var(--nv-space-5);
    align-items: start;
  }

  @media (max-width: 899px) {
    .nv-pair {
      grid-template-columns: minmax(0, 1fr);
    }
  }
}
</style>
