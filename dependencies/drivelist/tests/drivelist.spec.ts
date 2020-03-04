/*
 * Copyright 2016 Balena.io
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

import { ok } from 'assert';
import { expect } from 'chai';
import * as os from 'os';
import { stub } from 'sinon';

import { list } from '../lib';

describe('Drivelist', () => {
	describe('.list()', () => {
		it('should yield results', async () => {
			const devices = await list();
			devices.forEach(device => {
				ok(device.enumerator, `Invalid enumerator: ${device.enumerator}`);
				ok(device.busType, `Invalid busType: ${device.busType}`);
				ok(device.device, `Invalid device: ${device.device}`);
				ok(device.raw, `Invalid raw: ${device.raw}`);
				ok(device.description, `Invalid description: ${device.description}`);
				ok(device.error === null, `Invalid error: ${device.error}`);
				ok(
					device.size === null || Number.isFinite(device.size),
					`Invalid size: ${device.size}`,
				);
				ok(
					Number.isFinite(device.blockSize),
					`Invalid blockSize: ${device.blockSize}`,
				);
				ok(
					Number.isFinite(device.logicalBlockSize),
					`Invalid logicalBlockSize: ${device.logicalBlockSize}`,
				);
				ok(
					Array.isArray(device.mountpoints),
					`Invalid mountpoints: ${device.mountpoints}`,
				);
				ok(
					device.isReadOnly === null || typeof device.isReadOnly === 'boolean',
					`Invalid isReadOnly flag: ${device.isReadOnly}`,
				);
				ok(
					device.isSystem === null || typeof device.isSystem === 'boolean',
					`Invalid isSystem flag: ${device.isSystem}`,
				);
				ok(
					device.isVirtual === null || typeof device.isVirtual === 'boolean',
					`Invalid isVirtual flag: ${device.isVirtual}`,
				);
				ok(
					device.isRemovable === null ||
						typeof device.isRemovable === 'boolean',
					`Invalid isRemovable flag: ${device.isRemovable}`,
				);
				ok(
					device.isCard === null || typeof device.isCard === 'boolean',
					`Invalid isCard flag: ${device.isCard}`,
				);
				ok(
					device.isSCSI === null || typeof device.isSCSI === 'boolean',
					`Invalid isSCSI flag: ${device.isSCSI}`,
				);
				ok(
					device.isUSB === null || typeof device.isUSB === 'boolean',
					`Invalid isUSB flag: ${device.isUSB}`,
				);
				ok(
					device.isUAS === null || typeof device.isUAS === 'boolean',
					`Invalid isUAS flag: ${device.isUAS}`,
				);
			});
		});

		describe('given an unsupported os', () => {
			beforeEach(() => {
				// @ts-ignore
				this.osPlatformStub = stub(os, 'platform');
				// @ts-ignore
				this.osPlatformStub.returns('foobar');
			});

			afterEach(() => {
				// @ts-ignore
				this.osPlatformStub.restore();
			});

			it('should yield an unsupported error', async () => {
				try {
					await list();
				} catch (error) {
					expect(error).to.be.an.instanceof(Error);
					expect(error.message).to.equal(
						'Your OS is not supported by this module: foobar',
					);
					return;
				}
				ok(false, 'Expected error not thrown');
			});
		});
	});
});
