/**
 * @file Save a poster onto the host for an app that's already in the library.
 * The host's /api/covers/upload stores PNG data (Moonlight only accepts PNG box art), so any
 * image is redrawn to PNG in the browser first, scaled to at most 600×900.
 */
import { postJson } from '../../api'

/** Largest poster size stored (the size Moonlight clients show). */
export const MAX_POSTER = { w: 600, h: 900 }

/**
 * Scale a size down to fit inside a box, keeping its aspect ratio.
 *
 * @param {number} w Source width.
 * @param {number} h Source height.
 * @param {{w: number, h: number}} [box] Box to fit.
 * @returns {{w: number, h: number}} Target size (never larger than the source).
 */
export function fitSize(w, h, box = MAX_POSTER) {
  const scale = Math.min(1, box.w / w, box.h / h)
  return { w: Math.max(1, Math.round(w * scale)), h: Math.max(1, Math.round(h * scale)) }
}

/**
 * Load an image URL and return it as base64 PNG data (no data: prefix).
 *
 * @param {string} url Image URL (same origin or blob:).
 * @returns {Promise<string>} Base64 PNG.
 */
export function imageToPngBase64(url) {
  return new Promise((resolve, reject) => {
    const img = new Image()
    img.onload = () => {
      const { w, h } = fitSize(img.naturalWidth, img.naturalHeight)
      const canvas = document.createElement('canvas')
      canvas.width = w
      canvas.height = h
      canvas.getContext('2d').drawImage(img, 0, 0, w, h)
      resolve(canvas.toDataURL('image/png').split(',')[1])
    }
    img.onerror = () => reject(new Error('image failed to load'))
    img.src = url
  })
}

/**
 * Store PNG data as a cover on the host.
 *
 * @param {string} key File name stem on the host.
 * @param {string} data Base64 PNG.
 * @returns {Promise<string>} The stored path, for the app's `image-path`.
 */
export async function uploadCoverData(key, data) {
  const body = await postJson('./api/covers/upload', { key, data })
  return body.path
}
