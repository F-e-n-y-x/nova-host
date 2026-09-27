<script setup>
/**
 * "Needs attention" row (SPEC §5): 48px, warning tint + border, icon + title in warning, one
 * sentence in text, optional command chip (mono, copy button) and one specific action link.
 * Only render it when something is wrong. With several issues it shows the first and an
 * "N things need attention" toggle that expands the rest.
 *
 * Props: issues — array of { id, title, detail, command?, action?: { label, to? , href?, onClick? },
 *        severity? ('warning' | 'danger') }.
 */
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronDown, CircleAlert, Copy, TriangleAlert } from '@lucide/vue'
import { toast } from '../toast'

const props = defineProps({
  issues: { type: Array, required: true },
})

const { t } = useI18n()
const expanded = ref(false)
const visible = computed(() => (expanded.value ? props.issues : props.issues.slice(0, 1)))

async function copy(text) {
  try {
    await navigator.clipboard.writeText(text)
    toast.success(t('nova.common.copied'))
  } catch {
    toast.danger(t('nova.common.copy_failed'))
  }
}
</script>

<template>
  <section v-if="issues.length" class="nv-attention" :aria-label="t('nova.attention.label')">
    <div v-for="issue in visible" :key="issue.id" :class="['nv-attention__row', `nv-attention__row--${issue.severity || 'warning'}`]">
      <component :is="issue.severity === 'danger' ? CircleAlert : TriangleAlert" :size="16" aria-hidden="true" class="nv-attention__icon" />
      <span class="nv-attention__title">{{ issue.title }}</span>
      <span class="nv-attention__detail">{{ issue.detail }}</span>
      <span v-if="issue.command" class="nv-attention__cmd">
        <code>{{ issue.command }}</code>
        <button type="button" class="nv-attention__copy" :aria-label="t('nova.common.copy_command')" @click="copy(issue.command)">
          <Copy :size="14" aria-hidden="true" />
        </button>
      </span>
      <template v-if="issue.action">
        <RouterLink v-if="issue.action.to" :to="issue.action.to" class="nv-attention__action">{{ issue.action.label }}</RouterLink>
        <a v-else-if="issue.action.href" :href="issue.action.href" class="nv-attention__action" target="_blank" rel="noopener">{{ issue.action.label }}</a>
        <button v-else type="button" class="nv-attention__action nv-attention__action--btn" @click="issue.action.onClick?.()">{{ issue.action.label }}</button>
      </template>
    </div>
    <button v-if="issues.length > 1" type="button" class="nv-attention__more" :aria-expanded="expanded ? 'true' : 'false'"
            @click="expanded = !expanded">
      {{ expanded ? t('nova.attention.show_less') : t('nova.attention.count', { n: issues.length }, issues.length) }}
      <ChevronDown :size="14" aria-hidden="true" :class="{ 'nv-attention__chev--open': expanded }" />
    </button>
  </section>
</template>

<style>
@layer components {
  .nv-attention {
    display: flex;
    flex-direction: column;
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-warning-border);
    background: var(--nv-warning-tint);
    overflow: hidden;
  }

  .nv-attention__row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-2) var(--nv-space-3);
    min-height: 48px;
    padding: var(--nv-space-2) var(--nv-space-4);
    font-size: var(--nv-text-sm);
    line-height: 18px;
  }

  .nv-attention__row + .nv-attention__row {
    border-top: 1px solid var(--nv-warning-border);
  }

  .nv-attention__icon,
  .nv-attention__title {
    color: var(--nv-warning);
  }

  .nv-attention__row--danger .nv-attention__icon,
  .nv-attention__row--danger .nv-attention__title {
    color: var(--nv-danger);
  }

  .nv-attention__icon {
    flex-shrink: 0;
  }

  .nv-attention__title {
    font-weight: 500;
  }

  .nv-attention__detail {
    flex-grow: 1;
    min-width: 12ch;
    color: var(--nv-text);
  }

  .nv-attention__cmd {
    display: inline-flex;
    align-items: center;
    gap: 2px;
    padding: 2px 2px 2px var(--nv-space-2);
    border-radius: var(--nv-radius-sm);
    background: var(--nv-bg);
    color: var(--nv-text);
  }

  .nv-attention__cmd code {
    font-size: var(--nv-text-xs);
  }

  .nv-attention__copy {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 26px;
    height: 26px;
    padding: 0;
    border: 0;
    border-radius: 4px;
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-attention__copy:hover {
    color: var(--nv-text);
  }

  .nv-attention__action {
    font-weight: 500;
    color: var(--nv-warning);
    white-space: nowrap;
  }

  .nv-attention__action--btn {
    padding: 0;
    border: 0;
    background: transparent;
    font: inherit;
    font-weight: 500;
    cursor: pointer;
  }

  .nv-attention__action--btn:hover {
    text-decoration: underline;
  }

  .nv-attention__more {
    display: flex;
    align-items: center;
    gap: var(--nv-space-1);
    min-height: 36px;
    padding: 0 var(--nv-space-4);
    border: 0;
    border-top: 1px solid var(--nv-warning-border);
    background: transparent;
    color: var(--nv-warning);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
    text-align: left;
  }

  .nv-attention__chev--open {
    transform: rotate(180deg);
  }
}
</style>
