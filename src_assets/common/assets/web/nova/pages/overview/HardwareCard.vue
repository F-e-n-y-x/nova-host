<script setup>
/**
 * Hardware card (Final_Dashboard): GPU, driver, AV1 support and capture, from /api/host/info
 * (falls back to encoders found in the log on hosts without it).
 *
 * Props: info (/api/host/info or null), logEncoders (detectEncoders() result), loading.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvCard from '../../components/NvCard.vue'
import NvDataList from '../../components/NvDataList.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { captureLabel, encoderLabel } from './format'

const props = defineProps({
  info: { type: Object, default: null },
  logEncoders: { type: Array, default: () => [] },
  loading: { type: Boolean, default: false },
})

const { t } = useI18n()

const items = computed(() => {
  const info = props.info
  if (info) {
    const gpu = info.gpu?.[0]
    const rows = []
    if (gpu) {
      rows.push({ term: t('nova.overview.gpu'), value: gpu.name || gpu.vendor })
      if (gpu.driver_version || gpu.driver) rows.push({ term: t('nova.overview.driver'), value: gpu.driver_version || gpu.driver, mono: true })
    }
    const enc = info.encoders || {}
    rows.push({ term: t('nova.overview.encoder'), value: enc.active ? `${encoderLabel(enc.active)} · ${(enc.codecs || []).join(', ')}` : t('nova.overview.none_detected'), muted: !enc.active })
    rows.push({ term: 'AV1', value: enc.av1 ? t('nova.overview.supported') : t('nova.overview.not_supported'), muted: !enc.av1 })
    if (info.capture?.method) {
      rows.push({ term: t('nova.overview.capture'), value: `${captureLabel(info.capture.method)}${info.capture.zero_copy ? ` · ${t('nova.overview.zero_copy')}` : ''}` })
    }
    if (info.os_pretty) rows.push({ term: t('nova.overview.system'), value: info.os_pretty })
    return rows
  }
  const hw = props.logEncoders.filter((e) => e.hardware)
  const chosen = hw.length ? hw : props.logEncoders
  return [
    { term: t('nova.overview.encoder'), value: chosen.length ? [...new Set(chosen.map((e) => e.label))].join(', ') : t('nova.overview.none_detected'), muted: !chosen.length },
    { term: t('nova.overview.codecs'), value: chosen.length ? chosen.map((e) => e.codec).join(', ') : '—', muted: !chosen.length },
  ]
})
</script>

<template>
  <NvCard :title="t('nova.overview.hardware_title')" class="nv-hw">
    <template #actions><RouterLink to="/settings#encoder">{{ t('nova.overview.encoder_settings') }}</RouterLink></template>
    <div v-if="loading" aria-busy="true"><span class="nv-visually-hidden">{{ t('nova.common.loading') }}</span><NvSkeleton :lines="4" /></div>
    <NvDataList v-else :items="items" term-width="96px" />
  </NvCard>
</template>
