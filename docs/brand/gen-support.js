// README UPI support card in the Nova / Nebula style: graphite card, the four-point star mark,
// Geist outlined to paths, purple accent. Vector QR with the owner's Google Pay/UPI payload on a
// light field (scanners need dark modules on light).
//   node gen-support.js <fonts dir> <project name> <out.svg>
const opentype = require('opentype.js'), QR = require('qrcode'), fs = require('fs'), path = require('path')
const [fontsDir, NAME, out] = process.argv.slice(2)
const PAYLOAD = 'upi://pay?pa=ayushsoni2911@okaxis&pn=Ayush%20Soni&aid=uGICAgIDQlsPbRQ'
const UPI_ID = 'ayushsoni2911@okaxis'
const sans = opentype.loadSync(path.join(fontsDir, 'geist_regular.ttf'))
const sansB = opentype.loadSync(path.join(fontsDir, 'geist_semibold.ttf'))
const mono = opentype.loadSync(path.join(fontsDir, 'geist_mono_medium.ttf'))
const C = { bg: '#0A0A0B', edge: '#25222D', text: '#ECEAF2', dim: '#8D8A99', accent: '#A855F7', star: '#B7A2FF', field: '#15131A', paper: '#F4F2F8', ink: '#0A0A0B' }
const W = 760, H = 300
let body = ''
const width = (font, s, size) => [...s].reduce((a, ch) => a + font.charToGlyph(ch).advanceWidth * size / font.unitsPerEm, 0)
function text(x, y, s, size, fill, font = sans) {
  let d = '', cx = x
  for (const ch of s) { const g = font.charToGlyph(ch); d += g.getPath(cx, y, size).toPathData(1); cx += g.advanceWidth * size / font.unitsPerEm }
  body += `<path d="${d}" fill="${fill}"/>`
}
body += `<defs><radialGradient id="glow" cx="0.85" cy="0" r="0.9"><stop offset="0" stop-color="${C.accent}" stop-opacity="0.22"/><stop offset="1" stop-color="${C.accent}" stop-opacity="0"/></radialGradient></defs>`
body += `<rect x="1" y="1" width="${W - 2}" height="${H - 2}" rx="18" fill="${C.bg}" stroke="${C.edge}" stroke-width="2"/>`
body += `<rect x="1" y="1" width="${W - 2}" height="${H - 2}" rx="18" fill="url(#glow)"/>`
const qr = QR.create(PAYLOAD, { errorCorrectionLevel: 'M' })
const n = qr.modules.size, quiet = 3, box = 236, bx = 32, by = (H - box) / 2, cell = box / (n + quiet * 2)
body += `<rect x="${bx}" y="${by}" width="${box}" height="${box}" rx="14" fill="${C.paper}"/>`
let d = ''
for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) if (qr.modules.get(r, c)) {
  const x = bx + (c + quiet) * cell, y = by + (r + quiet) * cell
  d += `M${x.toFixed(2)} ${y.toFixed(2)}h${cell.toFixed(2)}v${cell.toFixed(2)}h-${cell.toFixed(2)}z`
}
body += `<path d="${d}" fill="${C.ink}" shape-rendering="crispEdges"/>`
const tx = bx + box + 40
body += `<g transform="translate(${tx} 56) scale(${32 / 28})"><rect width="28" height="28" rx="7" fill="#1D1830"/><path fill="${C.star}" d="M14 3.5c.84 5.2 5.3 9.66 10.5 10.5-5.2.84-9.66 5.3-10.5 10.5-.84-5.2-5.3-9.66-10.5-10.5C8.7 13.16 13.16 8.7 14 3.5z"/></g>`
text(tx + 44, 80, `Support ${NAME}`, 24, C.text, sansB)
text(tx, 122, `${NAME} is free and open source, built in spare time.`, 15, C.dim)
text(tx, 144, 'If it saves you time, a small UPI tip helps.', 15, C.dim)
text(tx, 186, 'UPI ID', 13, C.dim, sansB)
body += `<rect x="${tx - 2}" y="196" width="${width(mono, UPI_ID, 19) + 26}" height="36" rx="9" fill="${C.field}" stroke="${C.edge}"/>`
text(tx + 11, 221, UPI_ID, 19, C.star, mono)
text(tx, 262, 'Scan with any UPI app: Google Pay, PhonePe, Paytm, BHIM', 13, C.dim)
fs.writeFileSync(out, `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-label="Support ${NAME} by UPI: ${UPI_ID}"><title>Support ${NAME} - UPI ${UPI_ID}</title>${body}</svg>\n`)
console.log(out, fs.statSync(out).size, 'modules', n)
