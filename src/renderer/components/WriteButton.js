import React from 'react'
import Button from '@material-ui/core/Button'

class WriteButton extends React.Component {
  constructor (props) {
    super(props)

    this.state = {}

    this.writeButtonEnabled = this.writeButtonEnabled.bind(this)
    this.imageWillFitOnCard = this.imageWillFitOnCard.bind(this)
  }

  // returns true if sd/os not chosen yet
  imageWillFitOnCard () {
    if (this.props.sd === null || this.props.os === null) return true
    const osSize = this.props.os.size
    const sdSize = this.props.sd.size
    return osSize <= sdSize
  }

  writeButtonEnabled () {
    const osChosen = this.props.os !== null
    const sdChosen = this.props.sd !== null
    const willFit = this.imageWillFitOnCard()
    const res = osChosen && sdChosen && willFit
    return res
  }

  render () {
    return (
      <Button
        variant='contained'
        color='primary'
        size='large'
        disabled={!this.writeButtonEnabled()}
        onClick={this.props.onClick}
      >
        Write
      </Button>
    )
  }
}

export default WriteButton
