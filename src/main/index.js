import { dialog, app, BrowserWindow, ipcMain } from 'electron'
import * as path from 'path'
import { format as formatUrl } from 'url'

import elevate from './elevate'
import './os-list'
import './sd-list'
import './writer'

// global reference to mainWindow (necessary to prevent window from being garbage collected)
let mainWindow

function createMainWindow () {
  const window = new BrowserWindow({
    width: 640,
    height: 357 + 22, // add on mac titlebar
    resizable: false,
    maximizable: false,
    fullscreenable: false,
    title: 'Raspberry Pi Imaging Utility',
    autoHideMenuBar: true,
    webPreferences: {
      nodeIntegration: true
    }
  })

  window.loadURL(formatUrl({
    pathname: path.join(__dirname, '../renderer/index.html'),
    protocol: 'file',
    slashes: true
  }))

  window.on('closed', () => {
    mainWindow = null
  })

  window.webContents.on('devtools-opened', () => {
    window.focus()
    setImmediate(() => {
      window.focus()
    })
  })

  return window
}

// quit application when all windows are closed
app.on('window-all-closed', () => {
  app.quit()
})

app.on('activate', () => {
  // on macOS it is common to re-create a window even after all windows have been closed
  if (mainWindow === null) {
    mainWindow = createMainWindow()
  }
})

// create main BrowserWindow when electron is ready
app.on('ready', () => {
  elevate(app, function (error) {
    if (error) {
      dialog.showErrorBox('Elevation Error', error.message)
      return process.exit(1)
    }

    mainWindow = createMainWindow()
  })
})

//
ipcMain.on('quit-app', event => {
  app.quit()
})
