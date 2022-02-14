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

#import "REDiskList.h"
#import <DiskArbitration/DiskArbitration.h>

@implementation REDiskList

- (id)init {
  self = [super init];

  if (self) {
    _disks = [[NSMutableArray alloc] init];
    [self populateDisksBlocking];
    [_disks sortUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
  }

  return self;
}

-(void)dealloc {
  [_disks release];
  [super dealloc];
}

void appendDisk(DADiskRef disk, void *context) {
  NSMutableArray *_disks = (__bridge NSMutableArray*)context;
  const char *bsdName = DADiskGetBSDName(disk);
  if (bsdName != nil) {
    [_disks addObject:[NSString stringWithUTF8String:bsdName]];
  }
}

- (void)populateDisksBlocking {
  DASessionRef session = DASessionCreate(kCFAllocatorDefault);
  if (session) {
    DARegisterDiskAppearedCallback(session, NULL, appendDisk, (void*)_disks);
    CFRunLoopRef runLoop = [[NSRunLoop currentRunLoop] getCFRunLoop];
    DASessionScheduleWithRunLoop(session, runLoop, kCFRunLoopDefaultMode);
    CFRunLoopStop(runLoop);
    CFRunLoopRunInMode((CFStringRef)NSDefaultRunLoopMode, 0.05, NO);
    DAUnregisterCallback(session, appendDisk, (void*)_disks);
    CFRelease(session);
  }
}

@end
