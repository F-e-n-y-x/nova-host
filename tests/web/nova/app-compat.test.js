import { describe, expect, it } from 'vitest'

import {
  buildPayload, compatForm, compatPayload, formFromApp, isWindowsGame,
} from '../../../src_assets/common/assets/web/nova/pages/apps/appForm.js'

describe('Windows game compatibility options', () => {
  it('recognises Windows games Nova launches itself', () => {
    expect(isWindowsGame({ cmd: '"/usr/lib/x86_64-linux-gnu/nova-host/nova-proton-run" "/g/GTA5.exe"' })).toBe(true)
    expect(isWindowsGame({ cmd: 'umu-run "/g/Game.exe"' })).toBe(true)
    expect(isWindowsGame({ cmd: 'x', 'nova-exe': '/g/Game.exe' })).toBe(true)
    expect(isWindowsGame({ cmd: 'env LUTRIS_SKIP_INIT=1 lutris lutris:rungameid/3' })).toBe(false)
    expect(isWindowsGame({ cmd: 'steam steam://rungameid/1' })).toBe(false)
  })

  it('round-trips options and drops defaults', () => {
    const stored = { prefix: '~/Games/nova/gta-v', proton_version: 'GE-Proton11-7', extra_env: ['A=1'] }
    expect(compatPayload(compatForm(stored))).toEqual(stored)
    expect(compatPayload(compatForm())).toBeNull()
    expect(compatPayload({ ...compatForm(), proton_version: 'latest', extra_env: 'bad\n1X=2\nOK=1' })).toEqual({ extra_env: ['OK=1'] })
  })

  it('moves the legacy FSR, frame cap and MangoHud options into the performance profile', () => {
    const app = { name: 'GTA V', cmd: '"/w/nova-proton-run" "/g/GTA5.exe"', 'nova-exe': '/g/GTA5.exe', 'nova-compat': { prefix: '/p', fsr: 2, fps_cap: 60, mangohud: true } }
    const payload = buildPayload(formFromApp(app, 0, 'linux'))
    expect(payload['nova-compat']).toEqual({ prefix: '/p' })
    expect(payload['nova-perf']).toEqual({ fsr: 2, fps_cap: 60, mangohud: true })
  })

  it('stores nova-compat only when something is set', () => {
    const app = { name: 'GTA V', cmd: '"/w/nova-proton-run" "/g/GTA5.exe"', 'nova-exe': '/g/GTA5.exe', 'nova-compat': { prefix: '/p' } }
    const form = formFromApp(app, 0, 'linux')
    expect(form['nova-perf'].fsr).toBe('0')
    expect(buildPayload(form)['nova-compat']).toEqual({ prefix: '/p' })
    form['nova-compat'].prefix = ''
    expect(buildPayload(form)).not.toHaveProperty('nova-compat')
    expect(formFromApp({ name: 'Desktop', cmd: '' }, 1, 'linux')).not.toHaveProperty('nova-compat')
  })
})
