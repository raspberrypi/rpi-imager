import axios from 'axios'
import { app, ipcMain, BrowserWindow, dialog } from 'electron'
import { promises as fs } from 'fs'
import path from 'path'

import {
  OS_LIST_URL,
  OS_HTTP_TYPE,
  OS_CACHE_TYPE,
  OS_CUSTOM_TYPE,
  LOCAL_JSON_NAME
} from 'common/consts'

function mapOs (os, type) {
  return {
    type: type,
    name: os.os_name,
    description: os.description,
    release_date: os.release_date,
    icon: os.icon,
    path: os.path,
    hash: os.hash,
    size: os.extract_size,
    download_size: os.image_download_size
  }
}

ipcMain.on('get-os-list', event => {
  // check for cached data
  const localJsonPath = path.join(app.getPath('userData'), LOCAL_JSON_NAME)
  const cacheDataPromise = fs.readFile(localJsonPath, 'utf-8')

  // also check for online file
  const request = {
    url: OS_LIST_URL,
    method: 'GET',
    timeout: 10000,
    responseType: 'json'
  }
  const onlineDataPromise = axios(request)

  // run the promises
  Promise.allSettled([cacheDataPromise, onlineDataPromise]).then(([cacheDataRes, onlineDataRes]) => {
    let list = []

    // check the cache data first
    if (cacheDataRes.status === 'fulfilled') {
      const data = JSON.parse(cacheDataRes.value).os_list
      list = data.map(os => mapOs(os, OS_CACHE_TYPE))
    }

    // merge in the online list
    const online = onlineDataRes.status === 'fulfilled'
    if (online) {
      const names = list.map(os => os.name)
      onlineDataRes.value.data.os_list.forEach(os => {
        // see if this os is in the cache list
        const index = names.indexOf(os.os_name)
        const exists = index !== -1

        if (!exists) {
          // if doesn't exist, append to end of list
          list.push(mapOs(os, OS_HTTP_TYPE))
        } else {
          // if it exists, is the online one newer?
          const cachedOsDate = Date.parse(list[index].release_date)
          const onlineOsDate = Date.parse(os.release_date)
          const newer = onlineOsDate > cachedOsDate

          if (newer) {
            // if newer, replace
            list[index] = mapOs(os, OS_HTTP_TYPE)
          }
        }
      })
    }

    event.reply('os-list', list, online)
  }).catch(error => {
    const errorMessage = error.toString()
    event.reply('os-list-error', errorMessage)
  })
})

ipcMain.on('custom-os-dialog', event => {
  const window = BrowserWindow.fromWebContents(event.sender)
  dialog.showOpenDialog(window, {
    title: 'Select OS file',
    message: 'Select a Raspberry Pi operating system image file',
    properties: ['openFile'],
    filters: [
      {
        name: 'OS file',
        extensions: ['img']
      }
    ]
  }).then(chosenFile => {
    if (chosenFile.canceled) return

    const filePath = chosenFile.filePaths[0]
    return new Promise((resolve, reject) => {
      fs.stat(filePath, (error, statResult) => {
        if (error) return reject(error)

        return resolve({ filePath: filePath, size: statResult.size })
      })
    })
  }).then(result => {
    const { filePath, size } = result
    event.reply('custom-os-result', {
      type: OS_CUSTOM_TYPE,
      name: path.basename(filePath),
      path: filePath,
      size: size
    })
  }).catch(err => {
    console.error(err)
  })
})
