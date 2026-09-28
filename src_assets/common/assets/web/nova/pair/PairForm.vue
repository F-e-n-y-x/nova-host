<script setup>
/**
 * Pair card: approve a waiting pairing request with the PIN the device shows. A single request is
 * picked automatically (with Decline); several get a chooser. The name defaults to the host's
 * suggestion ("Nebula from Ayush's S25 Ultra", built from the app, owner and device the client
 * reported) or else the name the device sent, and stays editable. Emits `paired(name)` after the device finishes the handshake
 * and `changed` whenever the list of requests should be refreshed.
 */
import { computed, shallowRef, useId, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { MonitorSmartphone, Radar, Smartphone, Tablet, Tv } from '@lucide/vue'
import NvButton from '../components/NvButton.vue'
import NvSelect from '../components/NvSelect.vue'
import NvTextField from '../components/NvTextField.vue'
import NvAlert from '../components/NvAlert.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import PinBoxes from './PinBoxes.vue'
import { approvePairing, declinePairing } from '../devices/deviceApi'
import { isValidDeviceName } from '../devices/format'
import { toast } from '../toast'

const props = defineProps({
  requests: { type: Array, required: true },
  loaded: { type: Boolean, default: false },
  failed: { type: Boolean, default: false },
  hostName: { type: String, default: '' },
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

const host = computed(() => props.hostName || t('nova.pair.this_pc'))
const FORM_ICONS = { phone: Smartphone, tablet: Tablet, tv: Tv }

/**
 * Name to pre-fill for a request: the host's suggestion, else what the device reported.
 *
 * @param {object} [request] Pending request.
 * @returns {string} Name, possibly empty.
 */
function suggestedName(request) {
  return (request?.suggested_name || request?.name || '').trim()
}

const options = computed(() => [
  { value: '', label: t('nova.pair.request_placeholder'), disabled: true },
  ...props.requests.map((r) => ({
    value: r.id,
    label: `${suggestedName(r) || t('nova.pair.unknown_device')} — ${r.address || t('nova.pair.unknown_address')}`,
  })),
])
const selected = computed(() => props.requests.find((r) => r.id === selectedId.value) || null)
const waiting = computed(() => props.loaded && !props.failed && !props.requests.length)
const formIcon = computed(() => FORM_ICONS[selected.value?.form] || MonitorSmartphone)
const formLabel = computed(() => (FORM_ICONS[selected.value?.form] ? t(`nova.pair.form_${selected.value.form}`) : ''))

// Keep the selection valid as requests come and go; pick the only one automatically.
watch(() => props.requests, (requests) => {
  if (!requests.some((r) => r.id === selectedId.value)) {
    selectedId.value = requests.length === 1 ? requests[0].id : ''
  }
}, { immediate: true })

// Pre-fill the suggested name, unless the user already typed their own.
watch(selected, (request, previous) => {
  if (request && (!name.value || name.value === suggestedName(previous))) name.value = suggestedName(request)
}, { immediate: true })

/** Check the PIN when the boxes lose focus (only once something was typed). */
function checkPin() {
  if (pin.value) pinError.value = /^\d{4}$/.test(pin.value) ? '' : t('nova.pair.pin_invalid')
}

/** Check the name when the field loses focus (an empty name is fine). */
function checkName() {
  const value = name.value.trim()
  nameError.value = !value || isValidDeviceName(value) ? '' : t('nova.pair.name_invalid')
}

/**
 * Check the fields, setting their errors.
 *
 * @returns {boolean} Whether the form can be sent.
 */
function validate() {
  pinError.value = /^\d{4}$/.test(pin.value) ? '' : t('nova.pair.pin_invalid')
  checkName()
  if (pinError.value) document.getElementById(pinId)?.focus()
  return !pinError.value && !nameError.value
}

/** Send the PIN; the host replies once the device finishes pairing. */
async function submit() {
  result.value = null
  if (!selectedId.value || !validate()) return
  submitting.value = true
  const deviceName = name.value.trim() || suggestedName(selected.value) || t('nova.pair.unknown_device')
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
  const requestName = suggestedName(request) || t('nova.pair.unknown_device')
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
  <section class="nv-pairform" aria-labelledby="nv-pairform-title">
    <header class="nv-pairform__head">
      <h2 id="nv-pairform-title" class="nv-pairform__title">{{ t('nova.pair.card_title') }}</h2>
      <p class="nv-pairform__desc">{{ t('nova.pair.card_desc', { host }) }}</p>
    </header>

    <NvAlert v-if="failed" variant="danger" :title="t('nova.pair.load_failed')">
      <template #actions><NvButton size="sm" variant="secondary" @click="emit('retry')">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <NvEmptyState v-if="waiting" compact :title="t('nova.pair.waiting_title')" :description="t('nova.pair.waiting_desc')">
      <template #icon><Radar :size="28" /></template>
    </NvEmptyState>

    <form v-else-if="requests.length" class="nv-pairform__form" novalidate @submit.prevent="submit">
      <div v-if="requests.length > 1" class="nv-pairform__request">
        <div class="nv-grow">
          <NvSelect v-model="selectedId" :label="t('nova.pair.request_label')" :options="options" />
        </div>
        <NvButton variant="secondary" :disabled="!selectedId" :loading="declining" @click="decline">{{ t('nova.pair.decline') }}</NvButton>
      </div>
      <div v-else-if="selected" class="nv-pairform__from" data-testid="pair-request">
        <span class="nv-pairform__icon" :aria-label="formLabel || undefined" :aria-hidden="formLabel ? undefined : 'true'"
              :role="formLabel ? 'img' : undefined" :data-form="selected.form || 'unknown'">
          <component :is="formIcon" :size="20" aria-hidden="true" />
        </span>
        <div class="nv-pairform__who">
          <span class="nv-pairform__wants">{{ t('nova.pair.wants_to_pair') }}</span>
          <span class="nv-pairform__device">{{ suggestedName(selected) || t('nova.pair.unknown_device') }}</span>
          <span v-if="selected.address" class="nv-mono nv-pairform__addr">{{ selected.address }}</span>
        </div>
        <NvButton size="sm" variant="ghost" :loading="declining" @click="decline">{{ t('nova.pair.decline') }}</NvButton>
      </div>

      <div class="nv-field">
        <label :for="pinId" class="nv-field__label">{{ t('nova.pair.pin_label') }}</label>
        <PinBoxes :id="pinId" v-model="pin" :invalid="!!pinError" :describedby="`${pinId}-msg`" @focusout="checkPin" />
        <p v-if="!pinError" :id="`${pinId}-msg`" class="nv-field__hint">{{ t('nova.pair.pin_hint') }}</p>
        <p v-else :id="`${pinId}-msg`" class="nv-field__error">{{ pinError }}</p>
      </div>

      <NvTextField v-model="name" :label="t('nova.pair.device_name_label')" :hint="t('nova.pair.device_name_hint')"
                   :placeholder="t('nova.pair.device_name_placeholder')" :error="nameError" autocomplete="off"
                   spellcheck="false" @blur="checkName" />

      <div class="nv-pairform__submit">
        <NvButton type="submit" variant="primary" :loading="submitting" :disabled="!selectedId">{{ t('nova.pair.submit_device') }}</NvButton>
        <span class="nv-pairform__expire" role="status">{{ submitting ? t('nova.pair.submitting') : t('nova.pair.expires') }}</span>
      </div>
    </form>

    <p class="nv-visually-hidden" role="status">{{ waiting ? t('nova.pair.waiting_title') : '' }}</p>

    <NvAlert v-if="result" :variant="result.variant" :title="result.title" live dismissible @dismiss="result = null">
      {{ result.text }}
      <template v-if="result.variant === 'success'" #actions>
        <NvButton size="sm" variant="secondary" to="/devices">{{ t('nova.pair.view_devices') }}</NvButton>
      </template>
    </NvAlert>
  </section>
</template>

<style scoped>
@layer components {
  .nv-pairform {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
    padding: var(--nv-space-7);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-xl);
    background: var(--nv-surface);
  }

  .nv-pairform__title {
    margin: 0;
    font-size: var(--nv-text-lg);
    font-weight: 600;
  }

  .nv-pairform__desc {
    margin: var(--nv-space-1) 0 0;
    color: var(--nv-text-secondary);
  }

  .nv-pairform__form {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
  }

  .nv-pairform__request {
    display: flex;
    align-items: flex-end;
    gap: var(--nv-space-3);
  }

  .nv-pairform__from {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-2) var(--nv-space-3);
    margin: 0;
    padding: var(--nv-space-3) var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    font-size: var(--nv-text-sm);
  }

  .nv-pairform__icon {
    display: inline-flex;
    flex: none;
    align-items: center;
    justify-content: center;
    width: 40px;
    height: 40px;
    border-radius: var(--nv-radius-md);
    background: var(--nv-surface);
    color: var(--nv-text-secondary);
  }

  .nv-pairform__who {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .nv-pairform__wants {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-pairform__device {
    font-weight: 600;
    overflow-wrap: anywhere;
  }

  .nv-pairform__addr {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-pairform__from > :last-child {
    margin-left: auto;
  }

  .nv-pairform__submit {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-3);
  }

  .nv-pairform__expire {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  @media (max-width: 599px) {
    .nv-pairform {
      padding: var(--nv-space-5);
    }

    .nv-pairform__request {
      flex-direction: column;
      align-items: stretch;
    }
  }
}
</style>
