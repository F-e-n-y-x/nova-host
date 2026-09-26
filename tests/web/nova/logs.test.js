import { describe, expect, it } from 'vitest'

import { detectEncoders, healthChecks, parseLogs } from '../../../src_assets/common/assets/web/nova/logs.js'

const log = [
  '[2026-09-27 03:31:46.528]: Info: Zenith version: 2026.730.002631-0710b321 commit: ',
  '[2026-09-27 03:31:47.280]: Error: Could not open codec [h264_nvenc]: Function not implemented',
  '[2026-09-27 03:31:49.935]: Info: Found H.264 encoder: h264_nvenc [nvenc]',
  '[2026-09-27 03:31:49.935]: Info: Found HEVC encoder: hevc_nvenc [nvenc]',
  '[2026-09-27 03:31:50.000]: Warning: clipboard: could not watch the local clipboard; host-to-client sync is off',
].join('\n')

describe('parseLogs', () => {
  it('splits entries with timestamp, level and message', () => {
    const entries = parseLogs(log)
    expect(entries).toHaveLength(5)
    expect(entries[1]).toEqual({ timestamp: '2026-09-27 03:31:47.280', level: 'Error', message: 'Could not open codec [h264_nvenc]: Function not implemented' })
  })

  it('returns nothing for empty input', () => {
    expect(parseLogs('')).toEqual([])
  })
})

describe('detectEncoders', () => {
  it('reads the encoders the host settled on', () => {
    expect(detectEncoders(parseLogs(log)).map((e) => [e.codec, e.label, e.hardware])).toEqual([
      ['H.264', 'NVENC', true],
      ['HEVC', 'NVENC', true],
    ])
  })
})

describe('healthChecks', () => {
  it('reports GPU encoding and a clipboard problem', () => {
    const checks = healthChecks(parseLogs(log))
    expect(checks.map((c) => [c.id, c.status])).toEqual([['encoder', 'success'], ['clipboard', 'warning']])
    expect(checks[0].params).toEqual({ encoder: 'NVENC', codecs: 'H.264, HEVC' })
  })

  it('skips the clipboard check when sync is disabled', () => {
    expect(healthChecks(parseLogs(log), { clipboard_sync: 'disabled' }).map((c) => c.id)).toEqual(['encoder'])
  })

  it('warns about CPU encoding and fatal errors', () => {
    const cpu = parseLogs([
      '[2026-09-27 00:02:41.871]: Info: Found H.264 encoder: libx264 [software]',
      '[2026-09-27 00:02:42.000]: Fatal: Something broke',
      '[2026-09-27 00:02:42.100]: Error: Unable to initialize audio capture. The stream will not have audio.',
    ].join('\n'))
    expect(healthChecks(cpu).map((c) => [c.id, c.status])).toEqual([
      ['fatal', 'danger'], ['encoder', 'warning'], ['audio', 'warning'],
    ])
  })

  it('flags a missing encoder only when the log has content', () => {
    expect(healthChecks([])).toEqual([])
    expect(healthChecks(parseLogs('[2026-09-27 00:00:00.000]: Info: started')).map((c) => c.status)).toEqual(['danger'])
  })
})
