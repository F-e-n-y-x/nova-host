import { readFileSync, readdirSync } from 'node:fs'
import { resolve } from 'node:path'
import { describe, expect, it } from 'vitest'

const css = readFileSync(resolve(process.cwd(), 'src_assets/common/assets/web/nova/nova.css'), 'utf8')

/**
 * Collect `--nv-*: #hex` declarations from the block that follows `selector`.
 */
function palette(selector) {
  const start = css.indexOf(selector)
  expect(start, `missing ${selector}`).toBeGreaterThan(-1)
  const open = css.indexOf('{', start)
  const close = css.indexOf('}', open)
  const tokens = {}
  for (const [, name, hex] of css.slice(open, close).matchAll(/--nv-([a-z-]+):\s*(#[0-9A-Fa-f]{6})\s*;/g)) {
    tokens[name] = hex
  }
  return tokens
}

function luminance(hex) {
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255)
    .map((c) => (c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4))
  return 0.2126 * r + 0.7152 * g + 0.0722 * b
}

function contrast(a, b) {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x)
  return (hi + 0.05) / (lo + 0.05)
}

const light = palette(':root,\n  :root[data-nv-theme="light"]')
const dark = palette(':root[data-nv-theme="dark"]')
const darkFromOs = palette(':root:not([data-nv-theme="light"])')

const surfaces = ['bg', 'sidebar', 'surface', 'raised', 'panel']

// [foreground, backgrounds, minimum ratio]
const pairs = [
  ['text', surfaces, 4.5],
  ['text-secondary', surfaces, 4.5],
  ['text-muted', surfaces, 4.5],
  ['accent-text', [...surfaces, 'accent-tint'], 4.5],
  ['text-secondary', ['accent-tint'], 4.5],
  ['success', ['surface', 'raised', 'success-tint'], 4.5],
  ['warning', ['surface', 'raised', 'warning-tint'], 4.5],
  ['danger', ['surface', 'raised', 'danger-tint', 'danger-zone'], 4.5],
  ['info', ['surface', 'raised'], 4.5],
  ['text', ['danger-zone'], 4.5],
  ['text', ['success-tint', 'warning-tint', 'danger-tint', 'accent-tint'], 4.5],
  ['on-accent', ['accent', 'accent-hover'], 4.5],
  ['border-strong', surfaces, 3],
  ['focus', surfaces, 3],
]

describe('Nova tokens', () => {
  it('keeps both copies of the dark palette identical', () => {
    expect(Object.keys(dark).length).toBeGreaterThan(15)
    expect(darkFromOs).toEqual(dark)
  })

  it('defines the same tokens in light and dark', () => {
    expect(Object.keys(light).sort()).toEqual(Object.keys(dark).sort())
  })

  for (const [name, theme] of [['light', light], ['dark', dark]]) {
    it(`meets WCAG AA for every text and control pair in ${name}`, () => {
      const failures = []
      for (const [fg, bgs, min] of pairs) {
        for (const bg of bgs) {
          const ratio = contrast(theme[fg], theme[bg])
          if (!(ratio >= min)) failures.push(`${fg} on ${bg}: ${ratio.toFixed(2)} < ${min}`)
        }
      }
      expect(failures).toEqual([])
    })

    it(`keeps white text readable on danger fills in ${name}`, () => {
      expect(contrast('#FFFFFF', theme['danger-fill'])).toBeGreaterThanOrEqual(4.5)
    })
  }

  it('uses no !important and no gradients in Nova styles', () => {
    const root = resolve(process.cwd(), 'src_assets/common/assets/web/nova')
    const files = [resolve(root, 'nova.css'), resolve(root, 'AppShell.vue'),
      ...['components', 'pages'].flatMap((dir) => readdirSync(resolve(root, dir), { recursive: true })
        .filter((f) => /\.(vue|css|js)$/.test(f)).map((f) => resolve(root, dir, f)))]
    for (const file of files) {
      const text = readFileSync(file, 'utf8')
      expect(text, file).not.toMatch(/!important/)
      // Gradients are allowed only for game-art placeholders (SPEC §1).
      if (!file.endsWith('NvArt.vue')) expect(text, file).not.toMatch(/gradient\(/)
    }
  })
})
