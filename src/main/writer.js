import { powerSaveBlocker, app, ipcMain, BrowserWindow } from 'electron'
import path from 'path'
import { promises as fs } from 'fs'

import {
  OS_HTTP_TYPE,
  VALIDATE_WRITE,
  INCLUDE_SYSTEM,
  LOCAL_JSON_NAME
} from '../common/consts'

const arch = require('process').arch
const sdk = require('etcher-sdk')

const BITS = arch === 'x64' || arch === 'aarch64' ? 64 : 32

// promise that verifies the download stream. currently uses etcher's internal
// hashing methods, but it might be a good idea to switch to blake2 - or other -
// in the furure.
function downloadVerifier (source, expectedChecksum) {
  return new Promise((resolve, reject) => {
    source.createReadStream().then(stream => {
      const hasher = new sdk.sourceDestination.ProgressHashStream(sdk.constants.XXHASH_SEED, BITS, 'buffer')
      hasher.on('finish', () => {
        sdk.utils.streamToBuffer(hasher).then(buffer => {
          const checksum = buffer.toString('hex')
          if (checksum !== expectedChecksum) {
            return reject(new sdk.errors.ChecksumVerificationError(
              `Source and destination checksums do not match: ${expectedChecksum} !== ${checksum}`,
              checksum,
              expectedChecksum
            ))
          }
          return resolve()
        })
      })
      hasher.on('error', reject)
      stream.pipe(hasher)
    })
  })
}

function getCachedOsList (filepath) {
  return fs.readFile(filepath, 'utf-8').then(data => {
    return JSON.parse(data).os_list
  }).catch(() => {
    return []
  })
}

function updateJson (filepath, newOs) {
  return getCachedOsList(filepath).then(osList => {
    // get the old os if it exists
    const names = osList.map(os => os.os_name)
    const index = names.indexOf(newOs.os_name)
    const exists = index !== -1

    // if it exists, delete it from the filesystem
    const deletePromise = exists ? fs.unlink(osList[index].path) : null

    // filter the old os out of the list, if it exists
    osList = osList.filter(os => os.os_name !== newOs.os_name)

    // add in the new os
    osList.push(newOs)

    // write the file back
    const outData = {
      os_list: osList
    }
    const writePromise = fs.writeFile(filepath, JSON.stringify(outData))

    return Promise.all([deletePromise, writePromise])
  })
}

ipcMain.on('write', (event, os, sd) => {
  // prevent the window from being closed
  const window = BrowserWindow.fromWebContents(event.sender)
  window.setClosable(false)

  // prevent the computer from going to sleep
  const blocker = powerSaveBlocker.start('prevent-display-sleep')

  // only verify for http streams
  const verifyStream = os.type === OS_HTTP_TYPE

  // http requires http handler, cache and custom require file handler
  const source = os.type === OS_HTTP_TYPE
    ? new sdk.sourceDestination.Http(os.path)
    : new sdk.sourceDestination.File(os.path, sdk.sourceDestination.File.OpenFlags.Read)

  const onProgress = state => {
    // send to rendere process
    event.reply('write-progress', state.type, state.percentage)
    // update dock/tray progress bar
    window.setProgressBar(state.percentage / 100)
  }
  const onError = (destination, error) => {
    const errorMessage = error.toString()
    event.reply('write-error', errorMessage, destination)
    window.setProgressBar(-1)
  }

  let innerSource

  const adapters = [new sdk.scanner.adapters.BlockDeviceAdapter(() => INCLUDE_SYSTEM)]
  const scanner = new sdk.scanner.Scanner(adapters)
  return new Promise((resolve, reject) => {
    scanner.on('ready', resolve)
    scanner.on('error', reject)
    scanner.start()
  }).then(() => {
    return source.getInnerSource()
  }).then((source) => {
    innerSource = source

    const dests = [scanner.getBy('device', sd.device) || scanner.getBy('devicePath', sd.devicePath)]

    // if http, save to the cache dir
    if (os.type === OS_HTTP_TYPE) {
      const userDataPath = app.getPath('userData')
      const cachePath = path.join(userDataPath, source.metadata.name)
      dests.push(
        new sdk.sourceDestination.File(cachePath, sdk.sourceDestination.File.OpenFlags.ReadWrite)
      )
    }

    const writer = sdk.multiWrite.pipeSourceToDestinations(
      source,
      dests,
      onError,
      onProgress,
      VALIDATE_WRITE
    )

    if (verifyStream) {
      const verifier = downloadVerifier(source, os.hash)
      return Promise.all([writer, verifier])
    }
    return Promise.all([writer])
  }).then(results => {
    // throw out the result of the verifier - if they even exist
    const { failures, bytesWritten } = results[0]
    console.log('written', bytesWritten, 'bytes')

    if (failures.size > 0) {
      throw Error(failures)
    }

    // if necessary, write to the json file
    const promises = []
    if (os.type === OS_HTTP_TYPE) {
      const userDataPath = app.getPath('userData')
      const cachePath = path.join(userDataPath, innerSource.metadata.name)
      const jsonPath = path.join(userDataPath, LOCAL_JSON_NAME)

      const data = {
        path: cachePath,
        hash: os.hash,
        extract_size: os.size,
        description: os.description,
        icon: os.icon,
        os_name: os.name,
        release_date: os.release_date
      }

      promises.push(updateJson(jsonPath, data))
    }

    return Promise.all(promises)
  }).then(() => {
    event.reply('write-success', true)
  }).catch(error => {
    const errorMessage = error.toString()
    event.reply('write-error', errorMessage, null)
  }).finally(() => {
    // allow window to be closed, and computer to sleep
    window.setClosable(true)
    powerSaveBlocker.stop(blocker)
    // remove progress bar on dock/tray
    window.setProgressBar(-1)
  })
})
