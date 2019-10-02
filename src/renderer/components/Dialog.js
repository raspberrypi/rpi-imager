import { ipcRenderer } from 'electron'
import React from 'react'

import Button from '@material-ui/core/Button'
import Dialog from '@material-ui/core/Dialog'
import DialogActions from '@material-ui/core/DialogActions'
import DialogContent from '@material-ui/core/DialogContent'
import DialogContentText from '@material-ui/core/DialogContentText'
import DialogTitle from '@material-ui/core/DialogTitle'

class DialogClass extends React.Component {
  constructor (props) {
    super(props)

    this.handleQuitButton = this.handleQuitButton.bind(this)
  }

  handleQuitButton () {
    ipcRenderer.send('quit-app')
  }

  render () {
    const quitButton = !this.props.quitButton ? null : (
      <Button onClick={this.handleQuitButton} className='disable-focus' color='secondary'>
        {this.props.quitButton}
      </Button>
    )
    const dismissButton = !this.props.dismissButton ? null : (
      <Button onClick={this.props.onClose} className='disable-focus' color='primary'>
        {this.props.dismissButton}
      </Button>
    )

    return (
      <Dialog
        open={this.props.show}
        onClose={this.props.onClose}
        aria-labelledby='alert-dialog-title'
        aria-describedby='alert-dialog-description'
        maxWidth='xs'
        disableBackdropClick
        disableEscapeKeyDown
      >
        <DialogTitle id='alert-dialog-title'>{this.props.title}</DialogTitle>
        <DialogContent>
          <DialogContentText id='alert-dialog-description'>
            {this.props.body}
          </DialogContentText>
        </DialogContent>
        <DialogActions>
          {quitButton}
          {dismissButton}
        </DialogActions>
      </Dialog>
    )
  }
}

export default DialogClass
