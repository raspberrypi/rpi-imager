import path from 'path'
import url from 'url'

const isDevelopment = process.env.NODE_ENV !== 'production'

// see https://github.com/electron-userland/electron-webpack/issues/99#issuecomment-459251702
export function getStatic (val) {
  if (isDevelopment) {
    return new url.URL(val, window.location.origin)
  }
  return path.resolve(__static, val) // eslint-disable-line no-undef
}
