import { ipcRenderer } from 'electron'
import React from 'react'
import ReactDOM from 'react-dom'

import { createMuiTheme } from '@material-ui/core/styles'
import { ThemeProvider } from '@material-ui/styles'
import Grid from '@material-ui/core/Grid'

import { getStatic } from 'common/static'

import OSButton from './components/OSButton'
import SDButton from './components/SDButton'
import WriteButton from './components/WriteButton'
import WriteProgress from './components/WriteProgress'
import Dialog from './components/Dialog'

import {
  PROGRESS_START_TYPE
} from 'common/consts'

import './app.css'

const raspiTheme = createMuiTheme({
  palette: {
    primary: {
      main: '#f7dfe8'
    },
    secondary: {
      main: '#6cc04a'
    }
  }
})

class App extends React.Component {
  constructor (props) {
    super(props)

    this.state = {
      os: {
        list: null,
        chosen: null,
        online: null
      },
      sd: {
        list: null,
        chosen: null
      },
      writing: false,
      dialog: {
        show: false,
        title: '',
        body: '',
        quit: false,
        dismiss: false
      },
      writeProgress: {
        type: PROGRESS_START_TYPE,
        progress: null
      }
    }

    this.handleOsChange = this.handleOsChange.bind(this)
    this.handleSdChange = this.handleSdChange.bind(this)
    this.handleWriteClicked = this.handleWriteClicked.bind(this)
    this.setupListeners = this.setupListeners.bind(this)
    this.dialogShow = this.dialogShow.bind(this)
    this.handleDialogClose = this.dialogClose.bind(this)
    this.resetState = this.resetState.bind(this)

    this.setupListeners()
  }

  componentDidMount () {
    // get the os and sd lists
    ipcRenderer.send('get-os-list')
    ipcRenderer.send('sd-list-listener')
  }

  setupListeners () {
    // receiver for getting the os list
    ipcRenderer.on('os-list', (event, list, online) => {
      this.setState(prevState => ({
        os: {
          ...prevState.os,
          list: list,
          online: online
        }
      }))
    })

    // os list error receiver
    ipcRenderer.on('os-list-error', (event, error) => {
      this.dialogShow('Error getting OS list', error, 'Quit App', 'Continue')
    })

    // sd list receiver
    ipcRenderer.on('sd-list-update', (event, list) => {
      this.setState(prevState => ({
        sd: {
          ...prevState.sd,
          list: list
        }
      }))
    })

    // sd list error receiver
    ipcRenderer.on('sd-list-error', (event, error) => {
      this.dialogShow('Error getting SD cards', error, 'Quit App', 'Continue')
    })

    // write progress receiver
    ipcRenderer.on('write-progress', (event, type, progress) => {
      this.setState({
        writeProgress: {
          type: type,
          progress: progress
        }
      })
    })

    // write error receiver
    ipcRenderer.on('write-error', (event, error, drive) => {
      const errorMsg = drive ? `${error} - on ${drive}` : error
      this.dialogShow('Write Error', errorMsg, 'Quit App', 'Continue')
      this.resetState()
    })

    // write success receiver
    ipcRenderer.on('write-success', (event, bytesWritten) => {
      const sdName = this.state.sd.chosen.description
      const osName = this.state.os.chosen.name
      this.dialogShow('Write Successful', `${osName} has been written to ${sdName}`, false, 'Continue')
      this.resetState()
    })
  }

  resetState () {
    // re-get the os list, as we may have newly cached items
    ipcRenderer.send('get-os-list')

    // set the state back to a good state
    this.setState(prevState => ({
      writing: false,
      sd: {
        ...prevState.sd,
        chosen: null
      },
      os: {
        ...prevState.os,
        chosen: null
      },
      writeProgress: {
        type: PROGRESS_START_TYPE,
        progress: null
      }
    }))
  }

  handleOsChange (os) {
    this.setState(prevState => ({
      os: {
        ...prevState.os,
        chosen: os
      }
    }))
  }

  handleSdChange (sd) {
    this.setState(prevState => ({
      sd: {
        ...prevState.sd,
        chosen: sd
      }
    }))
  }

  handleWriteClicked (status) {
    ipcRenderer.send('write', this.state.os.chosen, this.state.sd.chosen)
    this.setState({
      writing: true
    })
  }

  dialogClose () {
    this.setState(prevState => ({
      dialog: {
        ...prevState.dialog,
        show: false
      }
    }))
  }

  dialogShow (title, body, quit, dismiss) {
    this.setState({
      dialog: {
        show: true,
        title: title,
        body: body,
        quit: quit,
        dismiss: dismiss
      }
    })
  }

  render () {
    const writeButton = (
      <WriteButton
        sd={this.state.sd.chosen}
        os={this.state.os.chosen}
        onClick={this.handleWriteClicked}
      />
    )

    const writeProgress = (
      <WriteProgress
        type={this.state.writeProgress.type}
        progress={this.state.writeProgress.progress}
      />
    )

    return (
      <ThemeProvider theme={raspiTheme}>
        <Grid container spacing={3}>
          <Grid item xs={12}>
            <img className='rpi-logo' src={getStatic('rpi.png')} />
          </Grid>
        </Grid>
        <Grid container spacing={3} className="grid">
          <Grid item xs={4}>
            <OSButton
              list={this.state.os.list}
              value={this.state.os.chosen}
              onChange={this.handleOsChange}
              writing={this.state.writing}
              online={this.state.os.online}
            />
          </Grid>
          <Grid item xs={4}>
            <SDButton
              list={this.state.sd.list}
              value={this.state.sd.chosen}
              onChange={this.handleSdChange}
              writing={this.state.writing}
            />
          </Grid>
          <Grid item xs={4}>
            {this.state.writing ? writeProgress : writeButton}
          </Grid>
        </Grid>
        <Dialog
          show={this.state.dialog.show}
          onClose={this.handleDialogClose}
          title={this.state.dialog.title}
          body={this.state.dialog.body}
          quitButton={this.state.dialog.quit}
          dismissButton={this.state.dialog.dismiss}
          className="dialog"
        />
      </ThemeProvider>
    )
  }
}

ReactDOM.render(<App />, document.getElementById('app'))
