<script setup>
/**
 * Windows virtual input card: description, Virtual HID Driver benefits, driver status and
 * license. Windows hosts only.
 *
 * Props: gamepadDriver (config value: '', 'virtualhid', 'vigembus' or 'all').
 */
import { computed, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import VirtualInputDrivers from './VirtualInputDrivers.vue'
import VirtualInputLicense from './VirtualInputLicense.vue'
import { VIGEMBUS_RELEASES, releaseState, useVirtualInput } from './useVirtualInput'

const props = defineProps({
  gamepadDriver: { type: String, default: '' },
})

const { t } = useI18n()
const vi = (key) => t(`nova.logs.vi.${key}`)
const input = useVirtualInput({ gamepadDriver: () => props.gamepadDriver, translate: vi })

const BENEFITS = ['gamepads', 'mouse', 'features', 'maintained']
const benefits = computed(() => BENEFITS.map((id) => ({
  id, title: vi(`virtualhid_benefit_${id}_title`), text: vi(`virtualhid_benefit_${id}`),
})))
const showBenefits = computed(() => !(input.virtualhid.value.installed && input.license.value.licensed))

const descriptionKey = computed(() => {
  if (!props.gamepadDriver) return 'virtual_gamepad_unset_desc'
  if (props.gamepadDriver === 'vigembus') return 'virtual_gamepad_vigembus_desc'
  if (input.virtualhid.value.installed && input.license.value.licensed) {
    return props.gamepadDriver === 'all' ? 'virtual_gamepad_licensed_all_desc' : 'virtual_gamepad_licensed_desc'
  }
  return 'virtual_gamepad_desc'
})

function driverStatus(driver) {
  if (!driver.installed) return { statusVariant: 'neutral', statusText: vi('driver_status_not_installed') }
  return driver.version_compatible
    ? { statusVariant: 'success', statusText: vi('driver_status_compatible') }
    : { statusVariant: 'danger', statusText: vi('driver_status_unsupported') }
}

function driverFacts(driver, latest) {
  return [
    { term: vi('driver_installed_version'), value: driver.installed ? (driver.version || vi('driver_version_unknown')) : vi('driver_not_installed'), mono: true },
    { term: vi('driver_latest_version'), value: latest },
    { term: vi('driver_supported_versions'), value: driver.supported_versions || '—', mono: true },
  ]
}

const drivers = computed(() => {
  const list = []
  const release = input.release.value
  if (input.showVirtualhid.value) {
    const driver = input.virtualhid.value
    const version = release.loading ? vi('driver_release_checking') : (release.version || vi('driver_release_unavailable'))
    list.push({
      id: 'virtualhid', name: vi('virtualhid_driver'), url: release.url, ...driverStatus(driver),
      facts: driverFacts(driver, `${version} · ${vi(`driver_release_${releaseState(driver, release)}`)}`),
    })
  }
  if (input.showVigembus.value) {
    const driver = input.vigembus.value
    list.push({
      id: 'vigembus', name: vi('vigembus_driver'), url: VIGEMBUS_RELEASES, ...driverStatus(driver),
      facts: driverFacts(driver, vi('driver_release_eol')),
    })
  }
  return list
})

onMounted(() => {
  input.refreshDrivers()
  if (input.showVirtualhid.value) input.refreshLicense()
})
</script>

<template>
  <NvCard :title="vi('virtual_gamepad')">
    <p class="nv-secondary">{{ vi(descriptionKey) }}</p>
    <div v-if="gamepadDriver === 'vigembus'">
      <NvButton variant="primary" to="/settings#gamepad_driver">{{ vi('change_gamepad_driver') }}</NvButton>
    </div>

    <ul v-if="showBenefits" class="nv-vi__benefits" :aria-label="vi('virtualhid_benefits_title')">
      <li v-for="benefit in benefits" :key="benefit.id" class="nv-vi__item">
        <p class="nv-vi__item-title">{{ benefit.title }}</p>
        <p class="nv-secondary nv-vi__small">{{ benefit.text }}</p>
      </li>
    </ul>

    <VirtualInputDrivers :drivers="drivers" :refreshing="input.release.value.loading" @refresh="input.refreshDrivers()" />

    <VirtualInputLicense v-if="input.showVirtualhid.value" :license="input.license.value" :busy="input.licenseBusy.value"
                         :error="input.licenseError.value" :activate="(key) => input.updateLicense('activate', key)"
                         @validate="input.updateLicense('validate')" @deactivate="input.updateLicense('deactivate')" />
  </NvCard>
</template>

<style>
@layer components {
  .nv-vi__benefits,
  .nv-vi__drivers {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    gap: var(--nv-space-3);
  }

  .nv-vi__benefits {
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  }

  .nv-vi__item {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    padding: var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
  }

  .nv-vi__item-title {
    margin: 0;
    font-weight: 600;
  }

  .nv-vi__small {
    margin: 0;
    font-size: var(--nv-text-sm);
  }

  .nv-vi__section {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding-top: var(--nv-space-4);
    border-top: 1px solid var(--nv-border);
  }

  .nv-vi__section-head {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-start;
    justify-content: space-between;
    gap: var(--nv-space-3);
  }

  .nv-vi__h3 {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-vi__toolbar {
    flex-wrap: wrap;
  }

  .nv-vi__activate {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-vi__activate > .nv-btn {
    margin-top: 26px;
  }
}
</style>
