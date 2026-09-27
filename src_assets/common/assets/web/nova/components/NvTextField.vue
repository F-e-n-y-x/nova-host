<script setup>
/**
 * Labelled text input with optional hint and error text (both linked via aria-describedby).
 * Extra attributes (inputmode, spellcheck, name, maxlength, @blur, …) go to the <input>.
 *
 * v-model: string.
 * Props: label (required; hideLabel shows it to screen readers only), hint, error,
 *        type ('text' | 'password' | 'search' | 'url' | …; 'password' gets a show/hide toggle),
 *        placeholder, id, disabled, required, autocomplete, mono, hideLabel, autofocus, multiline.
 * Exposes: focus(), select().
 */
import { computed, onMounted, ref, useId } from 'vue'
import { useI18n } from 'vue-i18n'
import { Eye, EyeOff } from '@lucide/vue'

defineOptions({ inheritAttrs: false })

const model = defineModel({ type: String, default: '' })
const props = defineProps({
  label: { type: String, required: true },
  hint: { type: String, default: '' },
  error: { type: String, default: '' },
  type: { type: String, default: 'text' },
  placeholder: { type: String, default: '' },
  id: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
  required: { type: Boolean, default: false },
  autocomplete: { type: String, default: null },
  mono: { type: Boolean, default: false },
  hideLabel: { type: Boolean, default: false },
  autofocus: { type: Boolean, default: false },
  multiline: { type: Boolean, default: false },
})

const { t } = useI18n()
const autoId = useId()
const input = ref(null)
const revealed = ref(false)
const inputId = computed(() => props.id || `nv-text-${autoId}`)
const hintId = computed(() => `${inputId.value}-hint`)
const errorId = computed(() => `${inputId.value}-error`)
const describedBy = computed(() => [props.hint && hintId.value, props.error && errorId.value].filter(Boolean).join(' ') || null)
const isPassword = computed(() => props.type === 'password')
const effectiveType = computed(() => (isPassword.value && revealed.value ? 'text' : props.type))

onMounted(() => {
  if (props.autofocus) input.value?.focus()
})

defineExpose({
  focus: (options) => input.value?.focus(options),
  select: () => input.value?.select(),
})
</script>

<template>
  <div class="nv-field" :class="$attrs.class">
    <label :for="inputId" :class="['nv-field__label', { 'nv-visually-hidden': hideLabel }]">{{ label }}</label>
    <textarea v-if="multiline" :id="inputId" ref="input" v-model="model" v-bind="{ ...$attrs, class: undefined }"
              :placeholder="placeholder" :disabled="disabled" :required="required" :aria-describedby="describedBy"
              :aria-invalid="error ? 'true' : null"
              :class="['nv-input', { 'nv-mono': mono, 'nv-input--invalid': error }]"></textarea>
    <div v-else :class="{ 'nv-input-wrap': isPassword }">
      <input :id="inputId" ref="input" v-model="model" v-bind="{ ...$attrs, class: undefined }" :type="effectiveType"
             :placeholder="placeholder" :disabled="disabled" :required="required" :autocomplete="autocomplete"
             :aria-describedby="describedBy" :aria-invalid="error ? 'true' : null"
             :class="['nv-input', { 'nv-mono': mono, 'nv-input--invalid': error }]">
      <button v-if="isPassword" type="button" class="nv-field__reveal nv-input-wrap__action"
              :aria-label="revealed ? t('nova.common.hide_password') : t('nova.common.show_password')"
              :aria-pressed="revealed ? 'true' : 'false'" :disabled="disabled" @click="revealed = !revealed">
        <EyeOff v-if="revealed" :size="16" aria-hidden="true" />
        <Eye v-else :size="16" aria-hidden="true" />
      </button>
    </div>
    <p v-if="hint" :id="hintId" class="nv-field__hint">{{ hint }}</p>
    <p v-if="error" :id="errorId" class="nv-field__error">{{ error }}</p>
  </div>
</template>

<style>
@layer components {
  .nv-field__reveal {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 30px;
    height: 30px;
    padding: 0;
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-field__reveal:hover:not(:disabled) {
    background: var(--nv-raised);
    color: var(--nv-text);
  }
}
</style>
