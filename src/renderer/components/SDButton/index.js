import React from 'react'

import Button from '@material-ui/core/Button'
import FormHelperText from '@material-ui/core/FormHelperText'
import FormControl from '@material-ui/core/FormControl'

import SDDialog from './SDDialog'

class SDButton extends React.Component {
  constructor (props) {
    super(props)

    this.state = {
      open: false
    }

    this.handleDialogClose = this.handleDialogClose.bind(this)
  }

  handleDialogClose (val) {
    this.setState({
      open: false
    })

    if (typeof val !== 'undefined') {
      this.props.onChange(val)
    }
  }

  render () {
    const loaded = this.props.list !== null
    const placeholder = loaded ? 'Choose Card' : <i>Loading...</i>
    const buttonText = this.props.value === null ? placeholder : this.props.value.description
    const buttonDisabled = !loaded || this.props.writing

    return (
      <FormControl className='form-selector'>
        <FormHelperText className='form-helper'>SD Card</FormHelperText>
        <Button className='hidden-overflow' variant='contained' color='primary' disabled={buttonDisabled} onClick={() => this.setState({ open: true })}>
          {buttonText}
        </Button>
        <SDDialog options={this.props.list} open={this.state.open} onClose={this.handleDialogClose} />
      </FormControl>
    )
  }
}

export default SDButton
