<script setup>
/**
 * Approve a pairing request: choose the request, type the PIN the device shows, name it.
 * Emits `paired(name)` after the device finishes the handshake and `changed` whenever the
 * list of requests should be refreshed.
 */
import { computed, shallowRef, useId, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Radar } from '@lucide/vue'
import NvButton from '../components/NvButton.vue'
import NvSelect from '../components/NvSelect.vue'
import NvTextField from '../components/NvTextField.vue'
import NvAlert from '../components/NvAlert.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import { approvePairing, declinePairing } from '../devices/deviceApi'
import { isValidDeviceName } from '../devices/format'
import { toast } from '../toast'

const props = defineProps({
  requests: { type: Array, required: true },
  loaded: { type: Boolean, default: false },
  failed: { type: Boolean, default: false },
})
const emit = defineEmits(['paired', 'changed', 'retry'])

const { t } = useI18n()
const pinId = `nv-pin-${useId()}`

const selectedId = shallowRef('')
const pin = shallowRef('')
const name = shallowRef('')
const pinError = shallowRef('')
const nameError = shallowRef('')
const submitting = shallowRef(false)
const declining = shallowRef(false)
const result = shallowRef(null)

const options = computed(() => [
  { value: '', label: t('nova.pair.request_placeholder'), disabled: true },
  ...props.requests.map((r) => ({
    value: r.id,
    label: `${r.name || t('nova.pair.unknown_device')} — ${r.address || t('nova.pair.unknown_address')}`,
  })),
])
const selected = computed(() => props.requests.find((r) => r.id === selectedId.value) || null)
const waiting = computed(() => props.loaded && !props.failed && !props.requests.length)

// Keep the selection valid as requests come and go; pick the only one automatically.
watch(() => props.requests, (requests) => {
  if (!requests.some((r) => r.id === selectedId.value)) {
    selectedId.value = requests.length === 1 ? requests[0].id : ''
  }
}, { immediate: true })

// Suggest the name the device reported, unless the user already typed one.
watch(selected, (request, previous) => {
  if (request && (!name.value || name.value === (previous?.name || ''))) name.value = request.name || ''
}, { immediate: true })

/**
 * Check the fields, setting their errors.
 *
 * @returns {boolean} Whether the form can be sent.
 */
function validate() {
  pinError.value = /^\d{4}$/.test(pin.value) ? '' : t('nova.pair.pin_invalid')
  nameError.value = isValidDeviceName(name.value.trim()) ? '' : t('nova.pair.name_invalid')
  return !pinError.value && !nameError.value
}

/** Send the PIN; the host replies once the device finishes pairing. */
async function submit() {
  result.value = null
  if (!selectedId.value || !validate()) return
  submitting.value = true
  const deviceName = name.value.trim()
  try {
    await approvePairing(selectedId.value, pin.value, deviceName)
    result.value = { variant: 'success', title: t('nova.pair.success_title', { name: deviceName }), text: t('nova.pair.success_desc') }
    pin.value = ''
    name.value = ''
    emit('paired', deviceName)
  } catch (e) {
    const reason = e?.status === 400 && e.message ? e.message : t('nova.pair.failure_desc')
    result.value = { variant: 'danger', title: t('nova.pair.failure_title', { name: deviceName }), text: reason }
  } finally {
    submitting.value = false
    emit('changed')
  }
}

/** Decline the selected request. */
async function decline() {
  const request = selected.value
  if (!request) return
  declining.value = true
  const requestName = request.name || t('nova.pair.unknown_device')
  try {
    await declinePairing(request.id)
    toast.success(t('nova.pair.declined', { name: requestName }))
  } catch {
    toast.danger(t('nova.pair.decline_failed', { name: requestName }))
  } finally {
    declining.value = false
    emit('changed')
  }
}
</script>

<template>
  <div class="nv-stack">
    <NvAlert v-if="failed" variant="danger" :title="t('nova.pair.load_failed')">
      <template #actions><NvButton size="sm" variant="secondary" @click="emit('retry')">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <template v-if="waiting">
      <NvEmptyState compact :title="t('nova.pair.waiting_title')" :description="t('nova.pair.waiting_desc')">
        <template #icon><Radar :size="28" /></template>
      </NvEmptyState>
    </template>

    <form v-else-if="requests.length" class="nv-stack" novalidate @submit.prevent="submit">
      <div class="nv-pair-form__request">
        <div class="nv-grow">
          <NvSelect v-model="selectedId" :label="t('nova.pair.request_label')" :options="options" />
        </div>
        <NvButton variant="secondary" :disabled="!selectedId" :loading="declining" @click="decline">{{ t('nova.pair.decline') }}</NvButton>
      </div>

      <div class="nv-field">
        <label :for="pinId" class="nv-field__label">{{ t('nova.pair.pin_label') }}</label>
        <input :id="pinId" v-model="pin" class="nv-input nv-mono nv-pair-form__pin" type="text" inputmode="numeric"
               autocomplete="one-time-code" maxlength="4" pattern="\d{4}" required
               :aria-invalid="pinError ? 'true' : null" :aria-describedby="pinError ? `${pinId}-error` : `${pinId}-hint`">
        <p v-if="!pinError" :id="`${pinId}-hint`" class="nv-field__hint">{{ t('nova.pair.pin_hint') }}</p>
        <p v-else :id="`${pinId}-error`" class="nv-field__error">{{ pinError }}</p>
      </div>

      <NvTextField v-model="name" :label="t('nova.pair.name_label')" :hint="t('nova.pair.name_hint')"
                   :error="nameError" autocomplete="off" required />

      <div class="nv-row">
        <NvButton type="submit" variant="primary" :loading="submitting" :disabled="!selectedId">{{ t('nova.pair.submit') }}</NvButton>
        <span v-if="submitting" class="nv-pair-form__wait" role="status">{{ t('nova.pair.submitting') }}</span>
      </div>
    </form>

    <p class="nv-visually-hidden" role="status">{{ waiting ? t('nova.pair.waiting_title') : '' }}</p>

    <NvAlert v-if="result" :variant="result.variant" :title="result.title" live dismissible @dismiss="result = null">
      {{ result.text }}
      <template v-if="result.variant === 'success'" #actions>
        <NvButton size="sm" variant="secondary" to="/devices">{{ t('nova.pair.view_devices') }}</NvButton>
      </template>
    </NvAlert>
  </div>
</template>

<style scoped>
@layer components {
  .nv-pair-form__request {
    display: flex;
    align-items: flex-end;
    gap: var(--nv-space-3);
  }

  .nv-pair-form__pin {
    width: 140px;
    font-size: var(--nv-text-lg);
    letter-spacing: 0.3em;
    font-variant-numeric: tabular-nums;
  }

  .nv-pair-form__wait {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  @media (max-width: 599px) {
    .nv-pair-form__request {
      flex-direction: column;
      align-items: stretch;
    }
  }
}
</style>
