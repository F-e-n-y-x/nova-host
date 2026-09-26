<script setup>
/**
 * Virtual HID Driver license: state, stats, account links, activation and deactivation.
 *
 * Props: license (status from the local service), busy, error.
 * Emits: validate, deactivate, activate(key) — the parent clears the key via the returned promise.
 */
import { computed, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { ExternalLink, KeyRound, RefreshCw, Trash2 } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'
import NvBadge from '../../components/NvBadge.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvDataList from '../../components/NvDataList.vue'
import NvTextField from '../../components/NvTextField.vue'

const props = defineProps({
  license: { type: Object, required: true },
  busy: { type: Boolean, default: false },
  error: { type: String, default: '' },
  activate: { type: Function, required: true },
})
defineEmits(['validate', 'deactivate'])

const { t } = useI18n()
const vi = (key) => t(`nova.logs.vi.${key}`)
const key = shallowRef('')

const stateVariant = computed(() => {
  if (props.license.state === 'licensed') return 'success'
  if (['expired', 'disabled', 'invalid'].includes(props.license.state)) return 'danger'
  return 'neutral'
})

const stats = computed(() => {
  const l = props.license
  const items = []
  if (l.plan_name) items.push({ term: vi('virtualhid_license_plan'), value: l.plan_name })
  items.push({
    term: vi('virtualhid_license_activations'),
    value: l.activation_limit ? `${l.activation_usage} / ${l.activation_limit}` : vi('virtualhid_license_not_reported'),
  })
  items.push({ term: vi('virtualhid_license_active_devices'), value: String(l.active_devices) })
  if (l.customer_email) items.push({ term: vi('virtualhid_license_customer'), value: l.customer_email })
  return items
})

const unavailable = computed(() => props.busy || !props.license.service_available)

async function submit() {
  if (await props.activate(key.value)) key.value = ''
}
</script>

<template>
  <section class="nv-vi__section" aria-labelledby="nv-vi-license">
    <div class="nv-vi__section-head">
      <div>
        <h3 id="nv-vi-license" class="nv-vi__h3">{{ vi('virtualhid_license') }}</h3>
        <p class="nv-secondary">{{ vi('virtualhid_license_desc') }}</p>
      </div>
      <NvBadge :variant="stateVariant">{{ vi(`virtualhid_license_state_${license.state}`) }}</NvBadge>
    </div>

    <NvAlert v-if="error" variant="danger" live>{{ error }}</NvAlert>
    <NvAlert v-else-if="!license.service_available" variant="warning">
      {{ license.message || vi('virtualhid_license_unavailable') }}
    </NvAlert>

    <NvDataList :items="stats" term-width="200px" />
    <p v-if="license.message && license.service_available" class="nv-secondary">{{ license.message }}</p>

    <div class="nv-row nv-vi__toolbar">
      <NvButton variant="secondary" :loading="busy" :disabled="!license.service_available" @click="$emit('validate')">
        <RefreshCw :size="16" aria-hidden="true" />{{ vi('virtualhid_license_refresh') }}
      </NvButton>
      <NvButton v-if="license.manage_account_url" variant="secondary" :href="license.manage_account_url" target="_blank" rel="noopener noreferrer">
        <ExternalLink :size="16" aria-hidden="true" />{{ vi('virtualhid_license_manage') }}
      </NvButton>
      <NvButton v-if="license.purchase_url && !license.licensed" variant="primary" :href="license.purchase_url" target="_blank" rel="noopener noreferrer">
        {{ vi('virtualhid_license_buy') }}
      </NvButton>
      <NvButton v-if="license.licensed" variant="danger" :disabled="unavailable" @click="$emit('deactivate')">
        <Trash2 :size="16" aria-hidden="true" />{{ vi('virtualhid_license_deactivate') }}
      </NvButton>
    </div>

    <form v-if="!license.licensed" class="nv-vi__activate" @submit.prevent="submit">
      <div class="nv-grow">
        <NvTextField v-model="key" type="password" :label="vi('virtualhid_license_key')"
                     :placeholder="vi('virtualhid_license_key_placeholder')" :hint="vi('virtualhid_license_key_desc')"
                     :disabled="unavailable" autocomplete="off" mono />
      </div>
      <NvButton type="submit" variant="primary" :disabled="unavailable || !key.trim()">
        <KeyRound :size="16" aria-hidden="true" />{{ vi('virtualhid_license_activate') }}
      </NvButton>
    </form>
    <NvAlert v-else variant="success" :title="vi('virtualhid_license_machine_activated')">
      {{ vi('virtualhid_license_machine_activated_desc') }}
    </NvAlert>
  </section>
</template>
