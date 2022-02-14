/*
 * Copyright 2019 balena.io
 * Copyright 2018 Robin Andersson <me@robinwassen.com>
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

#ifndef SRC_DARWIN_REDISKLIST_H_
#define SRC_DARWIN_REDISKLIST_H_

#import <Foundation/Foundation.h>

/**
 * Class to return a list of disks synchronously
 * To use the class, just init an instance of it and
 * it will populate the disks property with NSStrings
 *
 * @author Robin Andersson
 */
@interface REDiskList : NSObject

/**
 * NSArray of disks and partitions
 * Disks are in the format disk0, disk1 etc
 * Partitions are in the format disk0s1, disk1s1 etc
 */
@property(readonly) NSArray *disks;

@end

#endif  // SRC_DARWIN_REDISKLIST_H_
