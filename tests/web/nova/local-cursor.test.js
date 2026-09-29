import { describe, expect, it } from 'vitest'
import { OPTIONS, SECTIONS } from '../../../src_assets/common/assets/web/configs/settings_schema.js'
import tabs from '../../../src_assets/common/assets/web/configs/config_tabs.json'
import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'

describe('local cursor setting', () => {
  it('is an input switch, on by default, next to the mouse options', () => {
    expect(OPTIONS.local_cursor).toEqual({ type: 'bool' })
    const input = SECTIONS.find((s) => s.id === 'input').options
    expect(input).toContain('local_cursor')
    expect(input.indexOf('local_cursor')).toBeGreaterThan(input.indexOf('mouse'))
    const inputTab = tabs.find((t) => t.options && 'mouse' in t.options)
    expect(inputTab.options.local_cursor).toBe('enabled')
  })

  it('has a label and says the video leaves the cursor out', () => {
    expect(en.config.local_cursor).toBe('Local cursor')
    expect(en.config.local_cursor_desc).toMatch(/leaves it out of the video/)
    expect(en.config.local_cursor_desc).toMatch(/Virtual display/)
  })
})
