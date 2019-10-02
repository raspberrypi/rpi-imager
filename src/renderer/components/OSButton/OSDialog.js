import { ipcRenderer } from 'electron'
import React from 'react'
import prettyBytes from 'pretty-bytes'

import Avatar from '@material-ui/core/Avatar'
import List from '@material-ui/core/List'
import ListItem from '@material-ui/core/ListItem'
import ListItemAvatar from '@material-ui/core/ListItemAvatar'
import ListItemText from '@material-ui/core/ListItemText'
import DialogTitle from '@material-ui/core/DialogTitle'
import Dialog from '@material-ui/core/Dialog'

import ComputerIcon from '@material-ui/icons/Computer'

import {
  OS_CACHE_TYPE
} from 'common/consts'

class OSDialog extends React.Component {
  constructor (props) {
    super(props)

    this.handleListItemClick = this.handleListItemClick.bind(this)
    this.handleCustomItemClick = this.handleCustomItemClick.bind(this)
  }

  handleListItemClick (value) {
    this.props.onClose(value)
  }

  handleCustomItemClick () {
    // request the main process show a file chooser
    ipcRenderer.on('custom-os-result', (event, fileData) => {
      this.handleListItemClick(fileData)
    })
    ipcRenderer.send('custom-os-dialog')
  }

  render () {
    const options = this.props.options === null ? [] : this.props.options
    const osList = options.map((os, idx) => {
      const name = os.name
      const onlineOrCached = os.type === OS_CACHE_TYPE ? 'Cached on your computer' : `Online - ${prettyBytes(os.download_size)} download`
      const description = (
        <>
          {os.description}
          <br />
          Released: {os.release_date}
          <br />
          {onlineOrCached}
        </>
      )
      return (
        <ListItem button onClick={() => this.handleListItemClick(os)} key={idx}>
          <ListItemAvatar>
            <img src={os.icon} />
          </ListItemAvatar>
          <ListItemText primary={name} secondary={description} />
        </ListItem>
      )
    })

    const offlineText = this.props.online ? null : (
      <ListItem disabled button key='offline'>
        <ListItemText secondary="The app couldn't connect to RaspberryPi.org, therefore downloading online images isn't available right now. You'll still be able to use cached images, or custom .img files you have on your computer." />
      </ListItem>
    )

    return (
      <Dialog aria-labelledby='simple-dialog-title' open={this.props.open} onBackdropClick={() => this.props.onClose()}>
        <DialogTitle id='simple-dialog-title'>Operating System</DialogTitle>
        <List>
          {offlineText}

          {osList}

          <ListItem button onClick={() => this.handleCustomItemClick()} key='custom'>
            <ListItemAvatar>
              <Avatar>
                <ComputerIcon />
              </Avatar>
            </ListItemAvatar>
            <ListItemText primary='Choose custom' secondary='Select a custom .img file from your computer' />
          </ListItem>
        </List>
      </Dialog>
    )
  }
}

export default OSDialog
