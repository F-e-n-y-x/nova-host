<script setup>
/**
 * 4-digit PIN entry drawn as four boxes (SPEC Pair). It is one real <input> laid over the boxes,
 * so typing, pasting, one-time-code autofill and screen readers all see a single field; the
 * boxes just mirror its digits and highlight where the next digit goes.
 *
 * v-model: the digits typed so far (string).
 * Props: id (for the <label>), invalid, describedby, disabled.
 */
import { computed, shallowRef } from 'vue'

const model = defineModel({ type: String, default: '' })
defineProps({
  id: { type: String, required: true },
  invalid: { type: Boolean, default: false },
  describedby: { type: String, default: null },
  disabled: { type: Boolean, default: false },
})

const focused = shallowRef(false)
const digits = computed(() => Array.from({ length: 4 }, (_, i) => model.value[i] || ''))
const cursor = computed(() => Math.min(model.value.length, 3))

/**
 * Keep only digits, at most four.
 *
 * @param {Event} event Input event.
 */
function onInput(event) {
  const clean = event.target.value.replace(/\D/g, '').slice(0, 4)
  event.target.value = clean
  model.value = clean
}
</script>

<template>
  <div :class="['nv-pin', { 'nv-pin--invalid': invalid, 'nv-pin--disabled': disabled }]">
    <input :id="id" :value="model" class="nv-pin__input" type="text" inputmode="numeric" autocomplete="one-time-code"
           maxlength="4" pattern="\d{4}" required spellcheck="false" :disabled="disabled"
           :aria-invalid="invalid ? 'true' : null" :aria-describedby="describedby"
           @input="onInput" @focus="focused = true" @blur="focused = false">
    <span v-for="(digit, i) in digits" :key="i" aria-hidden="true"
          :class="['nv-pin__box', { 'nv-pin__box--active': focused && i === cursor }]">{{ digit }}</span>
  </div>
</template>

<style scoped>
@layer components {
  .nv-pin {
    position: relative;
    display: inline-flex;
    gap: var(--nv-space-3);
  }

  .nv-pin__input {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    padding: 0;
    border: 0;
    opacity: 0;
    font-size: 16px;
    cursor: text;
  }

  .nv-pin__box {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 64px;
    height: 72px;
    border: 1px solid var(--nv-border-strong);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-bg);
    color: var(--nv-text);
    font-family: var(--nv-font-mono);
    font-size: 28px;
    font-variant-numeric: tabular-nums;
  }

  .nv-pin__box--active {
    border-color: var(--nv-accent-text);
    box-shadow: 0 0 0 1px var(--nv-accent-text);
  }

  .nv-pin--invalid .nv-pin__box {
    border-color: var(--nv-danger);
  }

  .nv-pin--disabled .nv-pin__box {
    opacity: 0.5;
  }

  @media (max-width: 599px) {
    .nv-pin__box {
      width: 56px;
      height: 64px;
    }
  }
}
</style>
