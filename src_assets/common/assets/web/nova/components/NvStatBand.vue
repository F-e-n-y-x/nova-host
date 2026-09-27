<script setup>
/**
 * Status band (SPEC §5): equal cells separated by 1px borders inside one card. Each cell: 12px muted
 * label with a 14px icon, 20/600 value (optionally with a status dot), 13px secondary sub-line.
 * 5 cells in a row on desktop, 2 columns on phone.
 *
 * Props: cells — array of { key, label, icon?, value, sub?, status? ('success'|'warning'|'danger'),
 *        to? (RouterLink target for the whole cell), loading? }, label (region name).
 */
defineProps({
  cells: { type: Array, required: true },
  label: { type: String, default: '' },
})
</script>

<template>
  <section class="nv-band" :aria-label="label || null" :style="{ '--nv-band-cols': cells.length }">
    <component :is="cell.to ? 'RouterLink' : 'div'" v-for="cell in cells" :key="cell.key || cell.label"
               :to="cell.to || null" class="nv-band__cell" :aria-busy="cell.loading ? 'true' : null">
      <span class="nv-band__label">
        <component :is="cell.icon" v-if="cell.icon" :size="14" aria-hidden="true" />{{ cell.label }}
      </span>
      <span v-if="cell.loading" class="nv-band__value nv-band__value--loading" aria-hidden="true"></span>
      <span v-else class="nv-band__value">
        <span v-if="cell.status" :class="['nv-band__dot', `nv-band__dot--${cell.status}`]" aria-hidden="true"></span>{{ cell.value }}
      </span>
      <span v-if="cell.sub" class="nv-band__sub">{{ cell.sub }}</span>
    </component>
  </section>
</template>

<style>
@layer components {
  .nv-band {
    display: grid;
    grid-template-columns: repeat(var(--nv-band-cols), minmax(0, 1fr));
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
    overflow: hidden;
  }

  .nv-band__cell {
    display: flex;
    flex-direction: column;
    gap: 6px;
    min-width: 0;
    padding: var(--nv-space-4) var(--nv-space-5);
    border-left: 1px solid var(--nv-border);
    color: var(--nv-text);
    text-decoration: none;
  }

  .nv-band__cell:first-child {
    border-left: 0;
  }

  a.nv-band__cell:hover {
    background: var(--nv-raised);
    text-decoration: none;
  }

  .nv-band__label {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: var(--nv-text-xs);
    line-height: 16px;
    color: var(--nv-text-muted);
  }

  .nv-band__value {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-width: 0;
    font-size: var(--nv-text-2xl);
    font-weight: 600;
    line-height: 26px;
    letter-spacing: -0.01em;
    font-variant-numeric: tabular-nums;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-band__value--loading {
    width: 60%;
    height: 26px;
    border-radius: var(--nv-radius-sm);
    background: var(--nv-raised);
  }

  .nv-band__dot {
    width: 8px;
    height: 8px;
    flex-shrink: 0;
    border-radius: 4px;
  }

  .nv-band__dot--success {
    background: var(--nv-success);
    box-shadow: 0 0 0 4px var(--nv-success-tint);
  }

  .nv-band__dot--warning {
    background: var(--nv-warning);
  }

  .nv-band__dot--danger {
    background: var(--nv-danger);
  }

  .nv-band__sub {
    font-size: var(--nv-text-sm);
    line-height: 18px;
    color: var(--nv-text-secondary);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  @media (max-width: 1023px) {
    .nv-band {
      grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    }

    .nv-band__cell {
      border-left: 0;
      border-top: 1px solid var(--nv-border);
      margin-top: -1px;
      box-shadow: -1px 0 0 var(--nv-border);
    }
  }

  @media (max-width: 767px) {
    .nv-band {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .nv-band__cell {
      padding: var(--nv-space-3) var(--nv-space-4);
    }

    .nv-band__value {
      font-size: var(--nv-text-lg);
      line-height: 20px;
    }
  }
}
</style>
