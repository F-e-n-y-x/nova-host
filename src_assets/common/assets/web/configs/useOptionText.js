/**
 * @file Translated text for settings: labels, descriptions (platform-specific where the
 * locale has them) and choice labels.
 */
import { useI18n } from 'vue-i18n'
import { OPTIONS } from './settings_schema.js'

/**
 * Pick a per-platform entry from `{ windows: …, linux: …, default: … }`, or return the value.
 *
 * @param {*} value Plain value or per-platform map.
 * @param {string} platform Host platform.
 * @returns {*} The entry for this platform.
 */
export function forPlatform(value, platform) {
  if (value && typeof value === 'object' && !Array.isArray(value) && 'default' in value) {
    return value[platform] ?? value.default
  }
  return value
}

/**
 * @param {import('vue').Ref<string>} platform Host platform.
 * @returns Text helpers bound to the current locale.
 */
export function useOptionText(platform) {
  const { t, te } = useI18n()

  /** Label of an option. */
  function label(key) {
    return te(`config.${key}`) ? t(`config.${key}`) : key
  }

  /**
   * Description paragraphs of an option.
   *
   * @param {string} key Option name.
   * @returns {string[]} Paragraphs (may be empty).
   */
  function description(key) {
    const option = OPTIONS[key] || {}
    const keys = forPlatform(option.descKeys, platform.value)
    if (Array.isArray(keys)) return keys.filter((k) => te(k)).map((k) => t(k))
    if (option.platformDesc) {
      const candidates = [`config.${key}_desc_${platform.value}`]
      if (platform.value !== 'windows') candidates.push(`config.${key}_desc_unix`, `config.${key}_desc_linux`)
      candidates.push(`config.${key}_desc`)
      const found = candidates.find((k) => te(k))
      return found ? [t(found)] : []
    }
    return te(`config.${key}_desc`) ? [t(`config.${key}_desc`)] : []
  }

  /**
   * Choices of a choice option with translated labels.
   *
   * @param {string} key Option name.
   * @param {object} config Working configuration (some choice lists depend on it).
   * @returns {{value: string, label: string, disabled?: boolean}[]} Choices.
   */
  function choices(key, config) {
    const option = OPTIONS[key] || {}
    const list = typeof option.choices === 'function' ? option.choices(platform.value, config) : option.choices || []
    return list.map((c) => {
      let text = c.text ?? t(c.label)
      if (c.suffix) text = `${text} ${t(c.suffix)}`
      return { value: String(c.value), label: text, disabled: c.disabled }
    })
  }

  /** Placeholder for text-like options. */
  function placeholder(key) {
    return forPlatform(OPTIONS[key]?.placeholder, platform.value) ?? ''
  }

  return { label, description, choices, placeholder, t }
}
