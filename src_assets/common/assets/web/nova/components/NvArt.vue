<script setup>
/**
 * Game / app artwork with a designed placeholder (SPEC §1): shows `src` when it loads, otherwise a
 * hue gradient derived from the title with the title on the dark lower third. The only place Nova
 * chrome uses gradients.
 *
 * Props: title (required; alt text / placeholder label), src, kind ('poster' 2:3 | 'hero' 10:3 |
 *        'thumb' 16:9 | 'icon' 1:1 | 'fill' (fills its box)), showTitle (placeholder label; default
 *        true for poster/hero), radius (CSS length), hue (0–360 override), decorative (alt="").
 */
import { computed, ref, watch } from 'vue'

const props = defineProps({
  title: { type: String, required: true },
  src: { type: String, default: '' },
  kind: { type: String, default: 'poster' },
  showTitle: { type: Boolean, default: null },
  radius: { type: String, default: '' },
  hue: { type: Number, default: null },
  decorative: { type: Boolean, default: false },
})

const failed = ref(false)
watch(() => props.src, () => { failed.value = false })

const computedHue = computed(() => {
  if (props.hue !== null) return props.hue
  let h = 0
  for (const ch of props.title) h = (h * 31 + ch.codePointAt(0)) % 360
  return h
})
const labelVisible = computed(() => props.showTitle ?? (props.kind === 'poster' || props.kind === 'hero'))
const initial = computed(() => (props.title.trim().charAt(0) || '?').toUpperCase())
const style = computed(() => ({ '--nv-art-h': computedHue.value, ...(props.radius ? { borderRadius: props.radius } : {}) }))
</script>

<template>
  <span :class="['nv-art', `nv-art--${kind}`]" :style="style">
    <img v-if="src && !failed" :src="src" :alt="decorative ? '' : title" loading="lazy" decoding="async"
         class="nv-art__img" @error="failed = true">
    <span v-else class="nv-art__placeholder" :role="decorative ? null : 'img'" :aria-label="decorative ? null : title">
      <span v-if="kind === 'icon' || kind === 'thumb'" class="nv-art__initial" aria-hidden="true">{{ initial }}</span>
      <span v-if="labelVisible" class="nv-art__title" aria-hidden="true">{{ title }}</span>
    </span>
  </span>
</template>

<style>
@layer components {
  .nv-art {
    position: relative;
    display: block;
    flex-shrink: 0;
    overflow: hidden;
    border-radius: var(--nv-radius-lg);
    background: var(--nv-raised);
  }

  .nv-art--poster {
    aspect-ratio: 2 / 3;
  }

  .nv-art--hero {
    aspect-ratio: 10 / 3;
    border-radius: var(--nv-radius-lg) var(--nv-radius-lg) 0 0;
  }

  .nv-art--thumb {
    aspect-ratio: 16 / 9;
    border-radius: var(--nv-radius-md);
  }

  .nv-art--icon {
    aspect-ratio: 1;
    border-radius: var(--nv-radius-md);
  }

  .nv-art--fill {
    width: 100%;
    height: 100%;
  }

  .nv-art__img {
    display: block;
    width: 100%;
    height: 100%;
    object-fit: cover;
  }

  .nv-art__placeholder {
    display: flex;
    flex-direction: column;
    justify-content: flex-end;
    width: 100%;
    height: 100%;
    background: linear-gradient(160deg, hsl(var(--nv-art-h) 50% 38%), hsl(calc(var(--nv-art-h) + 30) 60% 13%));
    color: #FFFFFF;
  }

  .nv-art__initial {
    margin: auto;
    font-size: 15px;
    font-weight: 600;
    opacity: 0.9;
  }

  .nv-art--thumb .nv-art__initial {
    font-size: 12px;
  }

  .nv-art__title {
    display: -webkit-box;
    -webkit-box-orient: vertical;
    -webkit-line-clamp: 2;
    line-clamp: 2;
    overflow: hidden;
    padding: 28px 10px 10px;
    background: linear-gradient(to top, rgba(0, 0, 0, 0.72), rgba(0, 0, 0, 0));
    font-size: var(--nv-text-sm);
    font-weight: 600;
    line-height: 17px;
  }

  .nv-art--hero .nv-art__title {
    padding: 40px 20px 16px;
    font-size: var(--nv-text-xl);
    line-height: 22px;
  }
}
</style>
