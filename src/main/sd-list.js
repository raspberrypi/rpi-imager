import { ipcMain } from 'electron'
import { INCLUDE_SYSTEM } from '../common/consts'

const sdk = require('etcher-sdk') // for some reason import doesn't work?

ipcMain.on('sd-list-listener', event => {
  const adapters = [new sdk.scanner.adapters.BlockDeviceAdapter(() => INCLUDE_SYSTEM)]
  const scanner = new sdk.scanner.Scanner(adapters)

  let drives = []

  scanner.on('attach', drive => {
    drives.push(drive.drive)
    event.reply('sd-list-update', drives)
  })

  scanner.on('detach', drive => {
    drives = drives.filter(d => {
      return d.devicePath !== drive.devicePath
    })
    event.reply('sd-list-update', drives)
  })

  scanner.on('error', error => {
    event.reply('sd-list-error', error)
  })

  scanner.start()
  event.reply('sd-list-update', drives)
})
