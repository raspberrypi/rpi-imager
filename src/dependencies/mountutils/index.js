/*
 * Copyright 2017 resin.io
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @module mountutils
 */

'use strict';

/**
 * @summary Unmount a whole disk
 * @function
 * @public
 * @static
 * @name unmountDisk
 *
 * @param {String} device - device
 * @param {Function} callback - callback (error)
 *
 * @example
 * // macOS
 * const drive = '/dev/disk2';
 *
 * // GNU/Linux
 * const drive = '/dev/sdb';
 *
 * // Windows
 * const drive = '\\\\.\\PHYSICALDRIVE2';
 *
 * mountutils.unmountDisk(drive, (error) => {
 *   if (error) {
 *     throw error;
 *   }
 *
 *   console.log('Done!');
 * });
 */

module.exports = require('bindings')({
  bindings: 'MountUtils',
  /* eslint-disable camelcase */
  module_root: __dirname
  /* eslint-enable camelcase */
});
