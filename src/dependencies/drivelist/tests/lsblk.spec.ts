/*
 * Copyright 2018 Balena.io
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

import { expect } from 'chai';
import { readFileSync } from 'fs';
import { join } from 'path';
import * as util from 'util';

import { transform as transformJSON } from '../lib/lsblk/json';
import { parse as parsePairs } from '../lib/lsblk/pairs';

function inspect(value: any) {
	console.log(
		util.inspect(value, {
			colors: true,
			depth: null,
		}),
	);
}

describe('Drivelist', () => {
	context('lsblk', () => {
		it('can handle --pairs output on Ubuntu 14.04', () => {
			const listData = readFileSync(
				join(__dirname, 'data', 'lsblk', 'ubuntu-14.04-1.txt'),
				'utf8',
			);
			const devices = parsePairs(listData);

			const expected = [
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sda',
					devicePath: null,
					raw: '/dev/sda',
					description: '(/boot/efi, /, [SWAP], /home)',
					error: null,
					size: 1024209543168,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [
						{
							path: '/boot/efi',
							label: undefined,
						},
						{
							path: '/',
							label: undefined,
						},
						{
							path: '[SWAP]',
							label: undefined,
						},
						{
							path: '/home',
							label: undefined,
						},
					],
					isReadOnly: false,
					isSystem: true,
					isVirtual: null,
					isRemovable: false,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
			];

			inspect(parsePairs(listData));

			expect(devices).to.deep.equal(expected);
		});

		it('can handle --pairs output on Ubuntu 14.04, sample 2', () => {
			const listData = readFileSync(
				join(__dirname, 'data', 'lsblk', 'ubuntu-14.04-2.txt'),
				'utf8',
			);
			const devices = parsePairs(listData);

			const expected = [
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/fd0',
					devicePath: null,
					raw: '/dev/fd0',
					description: 'fd0',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [],
					isReadOnly: false,
					isSystem: false,
					isVirtual: null,
					isRemovable: true,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sda',
					devicePath: null,
					raw: '/dev/sda',
					description: '(/, [SWAP])',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [
						{
							path: '/',
							label: undefined,
						},
						{
							path: '[SWAP]',
							label: undefined,
						},
					],
					isReadOnly: false,
					isSystem: true,
					isVirtual: null,
					isRemovable: false,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
			];

			inspect(parsePairs(listData));

			expect(devices).to.deep.equal(expected);
		});

		it('can handle mountpoints on root devices', () => {
			const listData = require('./data/lsblk/no-children-mountpoints.json');
			const actual = transformJSON(listData);

			const expected = [
				{
					enumerator: 'lsblk:json',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sda',
					devicePath: null,
					raw: '/dev/sda',
					description: '',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [
						{
							path: '/media/jwentz/Temp',
							label: undefined,
						},
					],
					isReadOnly: false,
					isSystem: false,
					isVirtual: null,
					isRemovable: true,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
				{
					enumerator: 'lsblk:json',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/nvme0n1',
					devicePath: null,
					raw: '/dev/nvme0n1',
					description: '([SWAP], /boot/efi, /)',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [
						{
							path: '[SWAP]',
							label: undefined,
						},
						{
							path: '/boot/efi',
							label: undefined,
						},
						{
							path: '/',
							label: undefined,
						},
					],
					isReadOnly: false,
					isSystem: true,
					isVirtual: null,
					isRemovable: false,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
			];

			inspect(actual);

			expect(actual).to.deep.equal(expected);
		});

		it('can handle empty mountpoints in lsblk --pairs output', () => {
			const listData = readFileSync(
				join(__dirname, 'data', 'lsblk', 'ubuntu-14.04-3.txt'),
				'utf8',
			);
			const actual = parsePairs(listData);
			const expected = [
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sda',
					devicePath: null,
					raw: '/dev/sda',
					description: 'sda',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [],
					isReadOnly: false,
					isSystem: true,
					isVirtual: null,
					isRemovable: false,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sdb',
					devicePath: null,
					raw: '/dev/sdb',
					description: 'sdb',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [],
					isReadOnly: false,
					isSystem: false,
					isVirtual: null,
					isRemovable: true,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sdc',
					devicePath: null,
					raw: '/dev/sdc',
					description: 'sdc',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [],
					isReadOnly: false,
					isSystem: false,
					isVirtual: null,
					isRemovable: true,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/sdd',
					devicePath: null,
					raw: '/dev/sdd',
					description: '(/media/<username>/85CA-6700)',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [
						{
							path: '/media/<username>/85CA-6700',
							label: undefined,
						},
					],
					isReadOnly: false,
					isSystem: false,
					isVirtual: null,
					isRemovable: true,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
				{
					enumerator: 'lsblk:pairs',
					busType: 'UNKNOWN',
					busVersion: null,
					device: '/dev/nvme0n1',
					devicePath: null,
					raw: '/dev/nvme0n1',
					description: '(/)',
					error: null,
					size: null,
					blockSize: 512,
					logicalBlockSize: 512,
					mountpoints: [
						{
							path: '/',
							label: undefined,
						},
					],
					isReadOnly: false,
					isSystem: true,
					isVirtual: null,
					isRemovable: false,
					isCard: null,
					isSCSI: null,
					isUSB: null,
					isUAS: null,
				},
			];

			inspect(actual);

			expect(actual).to.deep.equal(expected);
		});

		it('can handle mountpoints on root devices in --pairs output', () => {
			const listData = readFileSync(
				join(__dirname, 'data', 'lsblk', 'no-partition-table.txt'),
				'utf8',
			);
			const devices = parsePairs(listData);

			const expected = [
				{
					blockSize: 512,
					busType: 'UNKNOWN',
					busVersion: null,
					description: '(/boot/efi, /boot)',
					device: '/dev/sda',
					devicePath: null,
					enumerator: 'lsblk:pairs',
					error: null,
					isCard: null,
					isReadOnly: false,
					isRemovable: false,
					isSCSI: null,
					isSystem: true,
					isUAS: null,
					isUSB: null,
					isVirtual: null,
					logicalBlockSize: 512,
					mountpoints: [
						{
							label: undefined,
							path: '/boot/efi',
						},
						{
							label: undefined,
							path: '/boot',
						},
					],
					raw: '/dev/sda',
					size: 240065183744,
				},
				{
					blockSize: 512,
					busType: 'UNKNOWN',
					busVersion: null,
					description: 'sdb',
					device: '/dev/sdb',
					devicePath: null,
					enumerator: 'lsblk:pairs',
					error: null,
					isCard: null,
					isReadOnly: false,
					isRemovable: true,
					isSCSI: null,
					isSystem: false,
					isUAS: null,
					isUSB: null,
					isVirtual: null,
					logicalBlockSize: 512,
					mountpoints: [],
					raw: '/dev/sdb',
					size: null,
				},
				{
					blockSize: 512,
					busType: 'UNKNOWN',
					busVersion: null,
					description: '(/run/media/DA2E4172/BUILD)',
					device: '/dev/sdc',
					devicePath: null,
					enumerator: 'lsblk:pairs',
					error: null,
					isCard: null,
					isReadOnly: false,
					isRemovable: true,
					isSCSI: null,
					isSystem: false,
					isUAS: null,
					isUSB: null,
					isVirtual: null,
					logicalBlockSize: 512,
					mountpoints: [
						{
							label: undefined,
							path: '/run/media/DA2E4172/BUILD',
						},
					],
					raw: '/dev/sdc',
					size: 31457280000,
				},
			];

			inspect(parsePairs(listData));

			expect(devices).to.deep.equal(expected);
		});
	});
});
