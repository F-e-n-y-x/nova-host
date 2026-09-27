<script setup>
/**
 * Data table (SPEC §5 Table row): real <table> semantics, 56px rows (rowHeight to change), 20px side
 * padding, divider between rows, hover `raised`, selected row `raised` + 2px accent-text left edge.
 * Keep ≤ 2 visible actions per row and put the rest in NvActionMenu in the `actions` cell.
 * Below 768px the table becomes stacked rows (each cell shows its column label).
 *
 * Props: columns — array of { key, label, sortable?, align? ('start'|'end'), width?, mono?, hideBelow?
 *        (px; hide the column under that width), srOnly? (visually hidden header, e.g. actions) },
 *        rows (array), rowKey (field name or function), selectedKey, caption (screen-reader caption),
 *        rowHeight (px), loading (skeleton rows), skeletonRows.
 * v-model:sort — { key, dir: 'asc' | 'desc' } (sortable headers toggle it; the parent sorts rows).
 * Slots: `cell-<key>` ({ row, value }) per column, `empty` (shown when rows is empty and not loading).
 * Emits: row-click(row) — for rows that open a detail panel; keep a real link/button in a cell too.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { ArrowDown, ArrowUp, ArrowUpDown } from '@lucide/vue'

const sort = defineModel('sort', { type: Object, default: null })
const props = defineProps({
  columns: { type: Array, required: true },
  rows: { type: Array, default: () => [] },
  rowKey: { type: [String, Function], default: 'id' },
  selectedKey: { type: [String, Number], default: null },
  caption: { type: String, default: '' },
  rowHeight: { type: Number, default: 56 },
  loading: { type: Boolean, default: false },
  skeletonRows: { type: Number, default: 4 },
})
defineEmits(['row-click'])

const { t } = useI18n()

const keyOf = (row, i) => (typeof props.rowKey === 'function' ? props.rowKey(row) : row?.[props.rowKey] ?? i)

function toggleSort(col) {
  if (!col.sortable) return
  const dir = sort.value?.key === col.key && sort.value.dir === 'asc' ? 'desc' : 'asc'
  sort.value = { key: col.key, dir }
}

function ariaSort(col) {
  if (!col.sortable) return null
  if (sort.value?.key !== col.key) return 'none'
  return sort.value.dir === 'asc' ? 'ascending' : 'descending'
}

const style = computed(() => ({ '--nv-row-h': `${props.rowHeight}px` }))
</script>

<template>
  <div class="nv-table-wrap" :style="style" :aria-busy="loading ? 'true' : null">
    <table class="nv-table">
      <caption v-if="caption" class="nv-visually-hidden">{{ caption }}</caption>
      <thead>
        <tr>
          <th v-for="col in columns" :key="col.key" scope="col" :aria-sort="ariaSort(col)"
              :class="[`nv-table__th--${col.align || 'start'}`, col.hideBelow ? `nv-table--hide-${col.hideBelow}` : '']"
              :style="col.width ? { width: col.width } : null">
            <span v-if="col.srOnly" class="nv-visually-hidden">{{ col.label }}</span>
            <button v-else-if="col.sortable" type="button" class="nv-table__sort" @click="toggleSort(col)">
              {{ col.label }}
              <ArrowUp v-if="sort?.key === col.key && sort.dir === 'asc'" :size="12" aria-hidden="true" />
              <ArrowDown v-else-if="sort?.key === col.key" :size="12" aria-hidden="true" />
              <ArrowUpDown v-else :size="12" aria-hidden="true" class="nv-table__sort-idle" />
            </button>
            <template v-else>{{ col.label }}</template>
          </th>
        </tr>
      </thead>
      <tbody>
        <template v-if="loading">
          <tr v-for="n in skeletonRows" :key="`sk-${n}`" class="nv-table__row" aria-hidden="true">
            <td v-for="col in columns" :key="col.key"><span class="nv-table__skeleton"></span></td>
          </tr>
        </template>
        <template v-else>
          <tr v-for="(row, i) in rows" :key="keyOf(row, i)"
              :class="['nv-table__row', { 'nv-table__row--selected': selectedKey !== null && keyOf(row, i) === selectedKey }]"
              :aria-selected="selectedKey !== null ? String(keyOf(row, i) === selectedKey) : null"
              @click="$emit('row-click', row)">
            <td v-for="col in columns" :key="col.key" :data-label="col.srOnly ? null : col.label"
                :class="[`nv-table__td--${col.align || 'start'}`, { 'nv-mono': col.mono }, col.hideBelow ? `nv-table--hide-${col.hideBelow}` : '']">
              <slot :name="`cell-${col.key}`" :row="row" :value="row?.[col.key]">{{ row?.[col.key] }}</slot>
            </td>
          </tr>
        </template>
      </tbody>
    </table>
    <div v-if="!loading && rows.length === 0 && $slots.empty" class="nv-table__empty"><slot name="empty" /></div>
    <span v-if="loading" class="nv-visually-hidden" role="status">{{ t('nova.common.loading') }}</span>
  </div>
</template>

<style>
@layer components {
  .nv-table-wrap {
    width: 100%;
    overflow-x: auto;
  }

  .nv-table {
    width: 100%;
    border-collapse: collapse;
    font-size: var(--nv-text-md);
  }

  .nv-table th {
    height: 36px;
    padding: 0 var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-text-muted);
    text-align: left;
    white-space: nowrap;
  }

  .nv-table__th--end,
  .nv-table__td--end {
    text-align: right;
  }

  .nv-table__sort {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    padding: 0;
    border: 0;
    background: transparent;
    color: inherit;
    font: inherit;
    cursor: pointer;
  }

  .nv-table__sort:hover {
    color: var(--nv-text);
  }

  .nv-table__sort-idle {
    opacity: 0.5;
  }

  .nv-table td {
    height: var(--nv-row-h);
    padding: 0 var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
    color: var(--nv-text-secondary);
    vertical-align: middle;
  }

  .nv-table td.nv-mono {
    font-size: var(--nv-text-sm);
  }

  .nv-table__row:last-child td {
    border-bottom: 0;
  }

  .nv-table__row:hover td {
    background: var(--nv-raised);
  }

  .nv-table__row--selected td {
    background: var(--nv-raised);
  }

  .nv-table__row--selected td:first-child {
    box-shadow: inset 2px 0 0 var(--nv-accent-text);
  }

  .nv-table__skeleton {
    display: block;
    width: 70%;
    height: 12px;
    border-radius: var(--nv-radius-sm);
    background: var(--nv-raised);
  }

  .nv-table__empty {
    border-top: 1px solid var(--nv-divider);
  }

  @media (max-width: 1279px) {
    .nv-table--hide-1280 {
      display: none;
    }
  }

  @media (max-width: 1023px) {
    .nv-table--hide-1024 {
      display: none;
    }
  }

  @media (max-width: 767px) {
    .nv-table,
    .nv-table tbody,
    .nv-table tr,
    .nv-table td {
      display: block;
      width: 100%;
    }

    .nv-table thead {
      position: absolute;
      width: 1px;
      height: 1px;
      overflow: hidden;
      clip: rect(0 0 0 0);
    }

    .nv-table__row {
      padding: var(--nv-space-3) var(--nv-space-4);
      border-bottom: 1px solid var(--nv-divider);
    }

    .nv-table td {
      height: auto;
      padding: 2px 0;
      border: 0;
      text-align: left;
    }

    .nv-table td[data-label]:not(:first-child)::before {
      content: attr(data-label) " · ";
      color: var(--nv-text-muted);
      font-size: var(--nv-text-xs);
    }

    .nv-table__row--selected td:first-child {
      box-shadow: none;
    }

    .nv-table__row--selected {
      box-shadow: inset 2px 0 0 var(--nv-accent-text);
    }

    .nv-table--hide-768 {
      display: none;
    }
  }
}
</style>
