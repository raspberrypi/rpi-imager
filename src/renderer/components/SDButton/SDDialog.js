import React from 'react'

import List from '@material-ui/core/List'
import ListItem from '@material-ui/core/ListItem'
import ListItemAvatar from '@material-ui/core/ListItemAvatar'
import ListItemText from '@material-ui/core/ListItemText'
import DialogTitle from '@material-ui/core/DialogTitle'
import Dialog from '@material-ui/core/Dialog'

import SdStorage from '@material-ui/icons/SdStorage'
import Usb from '@material-ui/icons/Usb'

class SDDialog extends React.Component {
  constructor (props) {
    super(props)

    this.handleListItemClick = this.handleListItemClick.bind(this)
  }

  handleListItemClick (value) {
    this.props.onClose(value)
  }

  render () {
    const options = this.props.options === null ? [] : this.props.options
    const sdList = options.map((sd, idx) => {
      const icon = sd.isUSB ? <Usb /> : <SdStorage />
      const description = 'Mounted as ' + sd.mountpoints.map(m => m.path).join(', ')
      return (
        <ListItem button onClick={() => this.handleListItemClick(sd)} key={sd.device}>
          <ListItemAvatar>
            {icon}
          </ListItemAvatar>
          <ListItemText primary={sd.description} secondary={description} />
        </ListItem>
      )
    })

    const noneFoundText = options.length > 0 ? null : (
      <ListItem disabled button key='none'>
        <ListItemText secondary='No SD cards found.' />
      </ListItem>
    )

    return (
      <Dialog aria-labelledby='simple-dialog-title' open={this.props.open} onBackdropClick={() => this.props.onClose()}>
        <DialogTitle id='simple-dialog-title'>SD Card</DialogTitle>
        <List>
          {noneFoundText}

          {sdList}
        </List>
      </Dialog>
    )
  }
}

export default SDDialog
