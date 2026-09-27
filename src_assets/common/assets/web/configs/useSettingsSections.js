/**
 * @file What the Settings page shows: sections with the options that apply on this
 * platform with the current values, filtered by the search query, with encoder groups
 * split into the one in use and "other encoders".
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { GROUPS, OPTIONS, SECTIONS } from './settings_schema.js'
import {
  encoderGroupKeys, encoderGroupsFor, matchesSearch, optionApplies, primaryEncoderGroups,
} from './settings_model.js'
import { useOptionText } from './useOptionText.js'

/**
 * @param {object} form useSettingsForm() result.
 * @param {import('vue').Ref<string>} query Search text.
 * @returns {{sections: import('vue').ComputedRef<object[]>, matchCount: import('vue').ComputedRef<number>,
 *            searching: import('vue').ComputedRef<boolean>}}
 */
export function useSettingsSections(form, query) {
  const { t } = useI18n()
  const text = useOptionText(form.platform)
  const searching = computed(() => query.value.trim() !== '')

  /** Text searched for an option, cached per locale since it doesn't depend on values. */
  const searchText = computed(() => {
    const map = {}
    for (const key of Object.keys(OPTIONS)) map[key] = [text.label(key), ...text.description(key)]
    return map
  })

  function include(key, sectionTitle) {
    const config = form.config.value
    if (!optionApplies(key, config, form.platform.value)) return false
    return !searching.value || matchesSearch(query.value, key, [...(searchText.value[key] || []), sectionTitle])
  }

  function fieldItems(keys, sectionTitle) {
    const items = []
    let group = null
    for (const key of keys) {
      if (!include(key, sectionTitle)) continue
      const next = OPTIONS[key]?.group || null
      if (next && next !== group) items.push({ kind: 'heading', title: t(GROUPS[next]) })
      group = next
      items.push({ kind: 'field', key })
    }
    return items
  }

  const sections = computed(() => {
    if (!form.config.value) return []
    const platform = form.platform.value
    return SECTIONS.map((section) => {
      const title = t(`nova.settings.sections.${section.id}.title`)
      const items = fieldItems(section.options, title)
      let primaryGroups = []
      let otherGroups = []
      if (section.encoders) {
        const primary = primaryEncoderGroups(form.config.value, form.detectedEncoders.value, platform)
        const groups = encoderGroupsFor(platform).map((g) => {
          const groupTitle = t(g.titleKey)
          return {
            id: g.id,
            title: groupTitle,
            shortTitle: groupTitle.replace(/\s+Encoder$/i, ''),
            detected: g.encoders.some((e) => form.detectedEncoders.value.includes(e)),
            primary: primary.has(g.id),
            items: fieldItems(encoderGroupKeys(g.id), `${title} ${groupTitle}`),
          }
        }).filter((g) => g.items.length)
        primaryGroups = groups.filter((g) => g.primary)
        otherGroups = groups.filter((g) => !g.primary)
      }
      const count = [...items, ...primaryGroups.flatMap((g) => g.items), ...otherGroups.flatMap((g) => g.items)]
        .filter((i) => i.kind === 'field').length
      return {
        id: section.id,
        title,
        summary: t(`nova.settings.sections.${section.id}.summary`),
        panel: section.panel || null,
        items,
        primaryGroups,
        otherGroups,
        otherMatches: searching.value && otherGroups.length > 0,
        count,
      }
    }).filter((s) => s.count > 0)
  })

  const matchCount = computed(() => sections.value.reduce((n, s) => n + s.count, 0))

  return { sections, matchCount, searching }
}
