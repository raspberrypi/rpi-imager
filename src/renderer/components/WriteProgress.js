import React from 'react'

import LinearProgress from '@material-ui/core/LinearProgress'
import FormHelperText from '@material-ui/core/FormHelperText'
import FormControl from '@material-ui/core/FormControl'

import {
  PROGRESS_START_TYPE,
  PROGRESS_VERIFY_TYPE
} from 'common/consts'

class WriteProgress extends React.Component {
  render () {
    // if we're waiting on start, show indeterminate progress
    const writeVariant = this.props.type === PROGRESS_START_TYPE ? 'indeterminate' : 'determinate'
    const writeValue = this.props.type === PROGRESS_VERIFY_TYPE ? 100 : this.props.progress
    const verifyValue = this.props.type === PROGRESS_VERIFY_TYPE ? this.props.progress : 0

    return (
      <div>
        <FormControl className='form-selector'>
          <FormHelperText>Writing... {Math.round(writeValue)}%</FormHelperText>
          <LinearProgress variant={writeVariant} value={writeValue} />
        </FormControl>
        <FormControl className='form-selector'>
          <FormHelperText>Verifying... {Math.round(verifyValue)}%</FormHelperText>
          <LinearProgress color='secondary' variant='determinate' value={verifyValue} />
        </FormControl>
      </div>
    )
  }
}

export default WriteProgress
