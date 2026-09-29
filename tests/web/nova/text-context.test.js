import { describe, expect, it } from 'vitest'
import { OPTIONS, SECTIONS } from '../../../src_assets/common/assets/web/configs/settings_schema.js'
import tabs from '../../../src_assets/common/assets/web/configs/config_tabs.json'
import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'

describe('remote text context setting', () => {
  it('is an input switch, on by default, shown only while the mouse is on', () => {
    expect(OPTIONS.remote_text_context.type).toBe('bool')
    expect(OPTIONS.remote_text_context.when({ mouse: 'enabled' })).toBe(true)
    expect(OPTIONS.remote_text_context.when({ mouse: 'disabled' })).toBe(false)
    const input = SECTIONS.find((s) => s.id === 'input').options
    expect(input).toContain('remote_text_context')
    expect(input.indexOf('remote_text_context')).toBeGreaterThan(input.indexOf('mouse'))
    const inputTab = tabs.find((t) => t.options && 'mouse' in t.options)
    expect(inputTab.options.remote_text_context).toBe('enabled')
  })

  it('has a label and says what is read and what never is', () => {
    expect(en.config.remote_text_context).toBe('Auto keyboard for text fields')
    const desc = en.config.remote_text_context_desc
    expect(desc).toMatch(/never reads what you type/)
    expect(desc).toMatch(/Virtual display/)
    expect(desc).toMatch(/Mirror/)
    expect(desc).toMatch(/AT-SPI/)
  })
})
